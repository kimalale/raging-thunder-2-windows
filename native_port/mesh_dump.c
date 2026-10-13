#include "mesh_dump.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

/* Mesh dumper
 *
 * Hooks every glDrawArrays / glDrawElements and, the first time a given
 * piece of 3D geometry is drawn, exports it as a Wavefront OBJ (positions,
 * unit-0 texcoords, normals) together with its texture as a TGA. Meshes
 * are identified by a content hash of their vertex and index data, so the
 * same hash can later be used to look up replacement models.
 *
 * Geometry may live in client memory (the engine's own address space) or in
 * VBOs; VBO contents are read back with glGetBufferSubData. All array state
 * is queried from GL at draw time, so no pointer tracking is needed.
 *
 * Filtered out: points/lines, orthographic draws (2D menus and HUD) and
 * meshes below MIN_TRIANGLES (particles, sprites, quads). */

/* GL 1.3+ / ES constants missing from the Windows GL 1.1 header */
#define MD_GL_FIXED                              0x140C
#define MD_GL_TEXTURE0                           0x84C0
#define MD_GL_ACTIVE_TEXTURE                     0x84E0
#define MD_GL_CLIENT_ACTIVE_TEXTURE              0x84E1
#define MD_GL_ARRAY_BUFFER                       0x8892
#define MD_GL_ELEMENT_ARRAY_BUFFER               0x8893
#define MD_GL_ARRAY_BUFFER_BINDING               0x8894
#define MD_GL_ELEMENT_ARRAY_BUFFER_BINDING       0x8895
#define MD_GL_VERTEX_ARRAY_BUFFER_BINDING        0x8896
#define MD_GL_NORMAL_ARRAY_BUFFER_BINDING        0x8897
#define MD_GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING 0x889A

#define MIN_TRIANGLES      8
#define MAX_DUMPED_MESHES  20000
#define MAX_MESH_VERTICES  (1u << 20)

typedef void (APIENTRY *PFN_BINDBUFFER)(GLenum target, GLuint buffer);
typedef void (APIENTRY *PFN_GETBUFFERSUBDATA)(GLenum target, ptrdiff_t offset, ptrdiff_t size, void *data);
typedef void (APIENTRY *PFN_ACTIVETEXTURE)(GLenum texture);

static PFN_BINDBUFFER pfn_BindBuffer = NULL;
static PFN_GETBUFFERSUBDATA pfn_GetBufferSubData = NULL;
static PFN_ACTIVETEXTURE pfn_ActiveTexture = NULL;
static PFN_ACTIVETEXTURE pfn_ClientActiveTexture = NULL;

int g_mesh_dump_enabled = 0;

static char g_out_dir[MAX_PATH];
static FILE *g_index = NULL;
static unsigned g_mesh_count = 0;
static unsigned g_tex_count = 0;

/* ------------------------------------------------------------- */
/* HASHING                                                       */
/* ------------------------------------------------------------- */

#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME  0x100000001b3ULL

static uint64_t fnv1a(uint64_t h, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

static uint64_t fnv1a_u32(uint64_t h, uint32_t v) {
    return fnv1a(h, &v, sizeof(v));
}

/* Open-addressing set of 64-bit keys */
typedef struct {
    uint64_t *slots;
    uint32_t cap;
    uint32_t used;
} hset_t;

static hset_t g_seen_draws;    /* cheap per-draw-call keys */
static hset_t g_seen_meshes;   /* mesh content hashes */
static hset_t g_seen_textures; /* texture content hashes */

static void hset_put(hset_t *s, uint64_t key) {
    uint32_t mask = s->cap - 1;
    uint32_t i = (uint32_t)(key ^ (key >> 32)) & mask;
    while (s->slots[i] && s->slots[i] != key) i = (i + 1) & mask;
    if (!s->slots[i]) {
        s->slots[i] = key;
        s->used++;
    }
}

/* Returns 1 if the key was newly inserted, 0 if already present. */
static int hset_insert(hset_t *s, uint64_t key) {
    if (key == 0) key = 1; /* 0 marks an empty slot */
    if (!s->slots || (s->used + 1) * 4 >= s->cap * 3) {
        hset_t old = *s;
        s->cap = old.cap ? old.cap * 2 : 4096;
        s->slots = (uint64_t *)calloc(s->cap, sizeof(uint64_t));
        s->used = 0;
        for (uint32_t i = 0; i < old.cap; i++) {
            if (old.slots[i]) hset_put(s, old.slots[i]);
        }
        free(old.slots);
    }
    uint32_t before = s->used;
    hset_put(s, key);
    return s->used != before;
}

/* ------------------------------------------------------------- */
/* VERTEX ARRAY ACCESS                                           */
/* ------------------------------------------------------------- */

typedef struct {
    int enabled;
    GLint size;          /* components per element */
    GLenum type;
    GLsizei stride;      /* effective byte stride */
    const uint8_t *ptr;  /* client pointer, or byte offset into buffer */
    GLuint buffer;       /* real GL buffer name, 0 = client memory */
} attrib_t;

static int type_size(GLenum type) {
    switch (type) {
        case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
        case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
        case GL_FLOAT: case GL_INT: case GL_UNSIGNED_INT: case MD_GL_FIXED: return 4;
        default: return 0;
    }
}

static void query_attrib(attrib_t *a, GLenum array, GLenum size_q, GLint fixed_size,
                         GLenum type_q, GLenum stride_q, GLenum ptr_q, GLenum buffer_q) {
    memset(a, 0, sizeof(*a));
    if (!glIsEnabled(array)) return;

    GLint v = 0;
    if (size_q) {
        glGetIntegerv(size_q, &v);
        a->size = v;
    } else {
        a->size = fixed_size;
    }
    glGetIntegerv(type_q, &v);
    a->type = (GLenum)v;
    int ts = type_size(a->type);
    if (!ts || a->size < 1 || a->size > 4) return;

    glGetIntegerv(stride_q, &v);
    a->stride = v ? v : a->size * ts;
    void *p = NULL;
    glGetPointerv(ptr_q, &p);
    a->ptr = (const uint8_t *)p;
    glGetIntegerv(buffer_q, &v);
    a->buffer = (GLuint)v;
    a->enabled = 1;
}

static size_t attrib_elem_size(const attrib_t *a) {
    return (size_t)a->size * type_size(a->type);
}

/* Returns elements [lo, hi] tightly packed, or NULL. Caller frees. */
static uint8_t *fetch_attrib(const attrib_t *a, uint32_t lo, uint32_t hi) {
    size_t elem = attrib_elem_size(a);
    size_t n = (size_t)hi - lo + 1;
    size_t span = (n - 1) * a->stride + elem;
    const uint8_t *src;
    uint8_t *readback = NULL;

    if (a->buffer) {
        if (!pfn_BindBuffer || !pfn_GetBufferSubData) return NULL;
        readback = (uint8_t *)calloc(1, span);
        if (!readback) return NULL;
        GLint prev = 0;
        glGetIntegerv(MD_GL_ARRAY_BUFFER_BINDING, &prev);
        pfn_BindBuffer(MD_GL_ARRAY_BUFFER, a->buffer);
        pfn_GetBufferSubData(MD_GL_ARRAY_BUFFER, (ptrdiff_t)((uintptr_t)a->ptr + (size_t)lo * a->stride), (ptrdiff_t)span, readback);
        pfn_BindBuffer(MD_GL_ARRAY_BUFFER, (GLuint)prev);
        src = readback;
    } else {
        src = a->ptr + (size_t)lo * a->stride;
    }

    uint8_t *packed = (uint8_t *)malloc(n * elem);
    if (packed) {
        for (size_t i = 0; i < n; i++) {
            memcpy(packed + i * elem, src + i * a->stride, elem);
        }
    }
    free(readback);
    return packed;
}

static float decode_component(const uint8_t *p, GLenum type, int normalized) {
    switch (type) {
        case GL_FLOAT:          { float v; memcpy(&v, p, 4); return v; }
        case MD_GL_FIXED:       { int32_t v; memcpy(&v, p, 4); return v / 65536.0f; }
        case GL_INT:            { int32_t v; memcpy(&v, p, 4); return (float)v; }
        case GL_UNSIGNED_INT:   { uint32_t v; memcpy(&v, p, 4); return (float)v; }
        case GL_SHORT:          { int16_t v; memcpy(&v, p, 2); return normalized ? v / 32767.0f : v; }
        case GL_UNSIGNED_SHORT: { uint16_t v; memcpy(&v, p, 2); return normalized ? v / 65535.0f : v; }
        case GL_BYTE:           { int8_t v = (int8_t)p[0]; return normalized ? v / 127.0f : v; }
        case GL_UNSIGNED_BYTE:  return normalized ? p[0] / 255.0f : p[0];
        default:                return 0.0f;
    }
}

static void decode_elem(const attrib_t *a, const uint8_t *packed, uint32_t i, int normalized, float out[4]) {
    int ts = type_size(a->type);
    const uint8_t *e = packed + (size_t)i * attrib_elem_size(a);
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    for (int c = 0; c < a->size; c++) {
        out[c] = decode_component(e + c * ts, a->type, normalized);
    }
}

/* ------------------------------------------------------------- */
/* OUTPUT                                                        */
/* ------------------------------------------------------------- */

static void make_dirs(const char *path) {
    char buf[MAX_PATH];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = '\0';
            if (buf[0]) CreateDirectoryA(buf, NULL);
            *p = c;
        }
    }
    CreateDirectoryA(buf, NULL);
}

/* 32-bit TGA, bottom-left origin: GL row 0 (t = 0) is the bottom row,
 * matching OBJ texture coordinates. */
static int write_tga(const char *path, int w, int h, const uint8_t *rgba) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint8_t hdr[18] = {0};
    hdr[2] = 2; /* uncompressed true-color */
    hdr[12] = (uint8_t)(w & 0xFF);
    hdr[13] = (uint8_t)(w >> 8);
    hdr[14] = (uint8_t)(h & 0xFF);
    hdr[15] = (uint8_t)(h >> 8);
    hdr[16] = 32;
    hdr[17] = 8; /* 8 alpha bits */
    fwrite(hdr, 1, sizeof(hdr), f);

    uint8_t *row = (uint8_t *)malloc((size_t)w * 4);
    for (int y = 0; y < h; y++) {
        const uint8_t *src = rgba + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            row[x * 4 + 0] = src[x * 4 + 2];
            row[x * 4 + 1] = src[x * 4 + 1];
            row[x * 4 + 2] = src[x * 4 + 0];
            row[x * 4 + 3] = src[x * 4 + 3];
        }
        fwrite(row, 1, (size_t)w * 4, f);
    }
    free(row);
    fclose(f);
    return 1;
}

/* Dumps the texture bound to GL_TEXTURE_2D on the active unit (once per
 * unique content). Returns its content hash, or 0 if unavailable. */
static uint64_t dump_bound_texture(int *out_w, int *out_h) {
    GLint w = 0, h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return 0;

    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
    if (!px) return 0;
    GLint prev_align = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prev_align);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_PACK_ALIGNMENT, prev_align);

    uint64_t hash = fnv1a(FNV_OFFSET, px, (size_t)w * h * 4);
    hash = fnv1a_u32(fnv1a_u32(hash, (uint32_t)w), (uint32_t)h);
    if (hash == 0) hash = 1;

    if (hset_insert(&g_seen_textures, hash)) {
        char path[MAX_PATH + 64];
        snprintf(path, sizeof(path), "%s/tex_%016llx.tga", g_out_dir, (unsigned long long)hash);
        if (write_tga(path, w, h, px)) g_tex_count++;
    }
    free(px);
    *out_w = w;
    *out_h = h;
    return hash;
}

/* ------------------------------------------------------------- */
/* MESH CAPTURE                                                  */
/* ------------------------------------------------------------- */

static int is_triangle_mode(GLenum mode) {
    return mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN;
}

/* Converts an index list to a triangle list. Returns triangle count. */
static uint32_t triangulate(GLenum mode, const uint32_t *idx, uint32_t n, uint32_t *out) {
    uint32_t t = 0;
    if (mode == GL_TRIANGLES) {
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            out[t * 3 + 0] = idx[i];
            out[t * 3 + 1] = idx[i + 1];
            out[t * 3 + 2] = idx[i + 2];
            t++;
        }
        return t;
    }
    for (uint32_t i = 2; i < n; i++) {
        uint32_t a, b, c = idx[i];
        if (mode == GL_TRIANGLE_FAN) {
            a = idx[0];
            b = idx[i - 1];
        } else if (i & 1) {
            a = idx[i - 1];
            b = idx[i - 2];
        } else {
            a = idx[i - 2];
            b = idx[i - 1];
        }
        if (a == b || b == c || a == c) continue; /* strip stitching */
        out[t * 3 + 0] = a;
        out[t * 3 + 1] = b;
        out[t * 3 + 2] = c;
        t++;
    }
    return t;
}

static int projection_is_perspective(void) {
    GLfloat m[16];
    glGetFloatv(GL_PROJECTION_MATRIX, m);
    return m[11] != 0.0f; /* -1 for glFrustum, 0 for glOrtho */
}

/* Texture unit 0 holds the base color texture. These switch the active
 * and client-active units to 0 and back so the engine's state is kept. */
typedef struct {
    GLint active;
    GLint client;
} unit_state_t;

static void select_unit0(unit_state_t *saved) {
    saved->active = saved->client = MD_GL_TEXTURE0;
    if (pfn_ActiveTexture) {
        glGetIntegerv(MD_GL_ACTIVE_TEXTURE, &saved->active);
        pfn_ActiveTexture(MD_GL_TEXTURE0);
    }
    if (pfn_ClientActiveTexture) {
        glGetIntegerv(MD_GL_CLIENT_ACTIVE_TEXTURE, &saved->client);
        pfn_ClientActiveTexture(MD_GL_TEXTURE0);
    }
}

static void restore_units(const unit_state_t *saved) {
    if (pfn_ClientActiveTexture) pfn_ClientActiveTexture((GLenum)saved->client);
    if (pfn_ActiveTexture) pfn_ActiveTexture((GLenum)saved->active);
}

typedef struct {
    attrib_t coords;
    GLfloat matrix[16];
    uint64_t tex_hash;   /* 0 = untextured */
    int tex_w, tex_h;
} unit0_t;

static void query_unit0_coords(unit0_t *u) {
    unit_state_t saved;
    select_unit0(&saved);
    query_attrib(&u->coords, GL_TEXTURE_COORD_ARRAY, GL_TEXTURE_COORD_ARRAY_SIZE, 0,
                 GL_TEXTURE_COORD_ARRAY_TYPE, GL_TEXTURE_COORD_ARRAY_STRIDE,
                 GL_TEXTURE_COORD_ARRAY_POINTER, MD_GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING);
    restore_units(&saved);
}

/* Captures the texture matrix and dumps the bound texture. */
static void capture_unit0_texture(unit0_t *u) {
    unit_state_t saved;
    select_unit0(&saved);
    glGetFloatv(GL_TEXTURE_MATRIX, u->matrix);
    u->tex_hash = 0;
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    if (bound && glIsEnabled(GL_TEXTURE_2D)) {
        u->tex_hash = dump_bound_texture(&u->tex_w, &u->tex_h);
    }
    restore_units(&saved);
}

static void write_mesh(uint64_t hash, const attrib_t *pos, const uint8_t *pos_data,
                       const unit0_t *u0, const uint8_t *tc_data,
                       const attrib_t *nrm, const uint8_t *nrm_data,
                       uint32_t nverts, const uint32_t *tris, uint32_t ntris, uint32_t lo,
                       const GLfloat modelview[16]) {
    char path[MAX_PATH + 64];
    char name[32];
    snprintf(name, sizeof(name), "mesh_%016llx", (unsigned long long)hash);

    if (u0->tex_hash) {
        snprintf(path, sizeof(path), "%s/%s.mtl", g_out_dir, name);
        FILE *m = fopen(path, "w");
        if (m) {
            fprintf(m, "newmtl tex_%016llx\nKa 1 1 1\nKd 1 1 1\nmap_Kd tex_%016llx.tga\n",
                    (unsigned long long)u0->tex_hash, (unsigned long long)u0->tex_hash);
            fclose(m);
        }
    }

    snprintf(path, sizeof(path), "%s/%s.obj", g_out_dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "# Raging Thunder 2 mesh dump\n");
    fprintf(f, "# hash %016llx, %u vertices, %u triangles\n", (unsigned long long)hash, nverts, ntris);
    fprintf(f, "# modelview at capture (column-major):");
    for (int i = 0; i < 16; i++) fprintf(f, " %g", modelview[i]);
    fprintf(f, "\n");
    if (u0->tex_hash) fprintf(f, "mtllib %s.mtl\n", name);
    fprintf(f, "o %s\n", name);

    float v[4];
    for (uint32_t i = 0; i < nverts; i++) {
        decode_elem(pos, pos_data, i, 0, v);
        fprintf(f, "v %g %g %g\n", v[0], v[1], v[2]);
    }
    if (tc_data) {
        const GLfloat *m = u0->matrix;
        for (uint32_t i = 0; i < nverts; i++) {
            decode_elem(&u0->coords, tc_data, i, 0, v);
            float s = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12] * v[3];
            float t = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13] * v[3];
            float q = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3];
            if (q != 0.0f && q != 1.0f) { s /= q; t /= q; }
            fprintf(f, "vt %g %g\n", s, t);
        }
    }
    if (nrm_data) {
        for (uint32_t i = 0; i < nverts; i++) {
            decode_elem(nrm, nrm_data, i, 1, v);
            fprintf(f, "vn %g %g %g\n", v[0], v[1], v[2]);
        }
    }

    if (u0->tex_hash) fprintf(f, "usemtl tex_%016llx\n", (unsigned long long)u0->tex_hash);
    for (uint32_t t = 0; t < ntris; t++) {
        fprintf(f, "f");
        for (int k = 0; k < 3; k++) {
            uint32_t i = tris[t * 3 + k] - lo + 1; /* OBJ is 1-based */
            if (tc_data && nrm_data) fprintf(f, " %u/%u/%u", i, i, i);
            else if (tc_data)        fprintf(f, " %u/%u", i, i);
            else if (nrm_data)       fprintf(f, " %u//%u", i, i);
            else                     fprintf(f, " %u", i);
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

static void dump_mesh(GLenum mode, const uint32_t *idx, uint32_t n, const attrib_t *pos) {
    uint32_t lo = UINT32_MAX, hi = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (idx[i] < lo) lo = idx[i];
        if (idx[i] > hi) hi = idx[i];
    }
    if (hi - lo >= MAX_MESH_VERTICES) return;
    uint32_t nverts = hi - lo + 1;

    uint32_t *tris = (uint32_t *)malloc((size_t)n * 3 * sizeof(uint32_t));
    if (!tris) return;
    uint32_t ntris = triangulate(mode, idx, n, tris);
    if (ntris < MIN_TRIANGLES) {
        free(tris);
        return;
    }

    attrib_t nrm;
    query_attrib(&nrm, GL_NORMAL_ARRAY, 0, 3, GL_NORMAL_ARRAY_TYPE, GL_NORMAL_ARRAY_STRIDE,
                 GL_NORMAL_ARRAY_POINTER, MD_GL_NORMAL_ARRAY_BUFFER_BINDING);
    unit0_t u0;
    memset(&u0, 0, sizeof(u0));
    query_unit0_coords(&u0);

    uint8_t *pos_data = fetch_attrib(pos, lo, hi);
    uint8_t *tc_data = u0.coords.enabled ? fetch_attrib(&u0.coords, lo, hi) : NULL;
    uint8_t *nrm_data = nrm.enabled ? fetch_attrib(&nrm, lo, hi) : NULL;
    if (!pos_data) goto done;

    /* Content hash: vertex formats + vertex data + triangles relative to lo.
     * A replacement loader must compute exactly the same value. */
    uint64_t hash = FNV_OFFSET;
    hash = fnv1a_u32(hash, (uint32_t)pos->type);
    hash = fnv1a_u32(hash, (uint32_t)pos->size);
    hash = fnv1a(hash, pos_data, nverts * attrib_elem_size(pos));
    if (tc_data) {
        hash = fnv1a_u32(hash, (uint32_t)u0.coords.type);
        hash = fnv1a_u32(hash, (uint32_t)u0.coords.size);
        hash = fnv1a(hash, tc_data, nverts * attrib_elem_size(&u0.coords));
    }
    for (uint32_t i = 0; i < ntris * 3; i++) {
        hash = fnv1a_u32(hash, tris[i] - lo);
    }
    if (hash == 0) hash = 1;

    if (!hset_insert(&g_seen_meshes, hash)) goto done;

    /* Texture is read back only now that the mesh is known to be new */
    if (tc_data) capture_unit0_texture(&u0);

    GLfloat modelview[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, modelview);

    write_mesh(hash, pos, pos_data, &u0, tc_data, &nrm, nrm_data, nverts, tris, ntris, lo, modelview);
    g_mesh_count++;

    if (g_index) {
        char tex_name[24] = "";
        if (u0.tex_hash) snprintf(tex_name, sizeof(tex_name), "%016llx", (unsigned long long)u0.tex_hash);
        fprintf(g_index, "%016llx,%u,%u,%s,%d,%d,%u,0x%04X,%g,%g,%g\n",
                (unsigned long long)hash, ntris, nverts, tex_name,
                u0.tex_hash ? u0.tex_w : 0, u0.tex_hash ? u0.tex_h : 0,
                (unsigned)SDL_GetTicks(), (unsigned)pos->type,
                modelview[12], modelview[13], modelview[14]);
        fflush(g_index);
    }
    printf("[DUMP] mesh_%016llx: %u tris, %u verts%s\n",
           (unsigned long long)hash, ntris, nverts, u0.tex_hash ? ", textured" : "");

done:
    free(pos_data);
    free(tc_data);
    free(nrm_data);
    free(tris);
}

/* Common front half of both draw hooks: cheap filtering and dedup on the
 * draw call's parameters before any data is read. */
static int should_capture(GLenum mode, GLsizei count, attrib_t *pos, uint64_t call_key) {
    if (g_mesh_count >= MAX_DUMPED_MESHES) return 0;
    if (!is_triangle_mode(mode) || count < MIN_TRIANGLES + 2) return 0;

    query_attrib(pos, GL_VERTEX_ARRAY, GL_VERTEX_ARRAY_SIZE, 0, GL_VERTEX_ARRAY_TYPE,
                 GL_VERTEX_ARRAY_STRIDE, GL_VERTEX_ARRAY_POINTER, MD_GL_VERTEX_ARRAY_BUFFER_BINDING);
    if (!pos->enabled) return 0;

    uint64_t key = call_key;
    key = fnv1a(key, &pos->ptr, sizeof(pos->ptr));
    key = fnv1a_u32(key, pos->buffer);
    key = fnv1a_u32(key, (uint32_t)pos->stride);
    key = fnv1a_u32(key, (uint32_t)pos->type);
    key = fnv1a_u32(key, (uint32_t)pos->size);
    if (!hset_insert(&g_seen_draws, key)) return 0;

    return projection_is_perspective();
}

void mesh_dump_draw_arrays(GLenum mode, GLint first, GLsizei count) {
    attrib_t pos;
    uint64_t key = FNV_OFFSET;
    key = fnv1a_u32(key, 1);
    key = fnv1a_u32(key, (uint32_t)mode);
    key = fnv1a_u32(key, (uint32_t)first);
    key = fnv1a_u32(key, (uint32_t)count);
    if (!should_capture(mode, count, &pos, key)) return;

    uint32_t *idx = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
    if (!idx) return;
    for (GLsizei i = 0; i < count; i++) idx[i] = (uint32_t)(first + i);
    dump_mesh(mode, idx, (uint32_t)count, &pos);
    free(idx);
}

void mesh_dump_draw_elements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    int isz = (type == GL_UNSIGNED_BYTE) ? 1 : (type == GL_UNSIGNED_SHORT) ? 2 : (type == GL_UNSIGNED_INT) ? 4 : 0;
    if (!isz) return;

    GLint ebo = 0;
    glGetIntegerv(MD_GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);

    attrib_t pos;
    uint64_t key = FNV_OFFSET;
    key = fnv1a_u32(key, 2);
    key = fnv1a_u32(key, (uint32_t)mode);
    key = fnv1a_u32(key, (uint32_t)count);
    key = fnv1a_u32(key, (uint32_t)type);
    key = fnv1a(key, &indices, sizeof(indices));
    key = fnv1a_u32(key, (uint32_t)ebo);
    if (!should_capture(mode, count, &pos, key)) return;

    size_t bytes = (size_t)count * isz;
    uint8_t *raw = (uint8_t *)malloc(bytes);
    uint32_t *idx = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
    if (!raw || !idx) goto done;

    if (ebo) {
        if (!pfn_GetBufferSubData) goto done;
        pfn_GetBufferSubData(MD_GL_ELEMENT_ARRAY_BUFFER, (ptrdiff_t)(uintptr_t)indices, (ptrdiff_t)bytes, raw);
    } else {
        memcpy(raw, indices, bytes);
    }
    for (GLsizei i = 0; i < count; i++) {
        if (isz == 1)      idx[i] = raw[i];
        else if (isz == 2) { uint16_t v; memcpy(&v, raw + i * 2, 2); idx[i] = v; }
        else               { uint32_t v; memcpy(&v, raw + i * 4, 4); idx[i] = v; }
    }
    dump_mesh(mode, idx, (uint32_t)count, &pos);

done:
    free(raw);
    free(idx);
}

/* ------------------------------------------------------------- */
/* LIFECYCLE                                                     */
/* ------------------------------------------------------------- */

void mesh_dump_init(const char *out_dir) {
    snprintf(g_out_dir, sizeof(g_out_dir), "%s", out_dir);
    make_dirs(g_out_dir);

    pfn_BindBuffer = (PFN_BINDBUFFER)SDL_GL_GetProcAddress("glBindBuffer");
    pfn_GetBufferSubData = (PFN_GETBUFFERSUBDATA)SDL_GL_GetProcAddress("glGetBufferSubData");
    pfn_ActiveTexture = (PFN_ACTIVETEXTURE)SDL_GL_GetProcAddress("glActiveTexture");
    pfn_ClientActiveTexture = (PFN_ACTIVETEXTURE)SDL_GL_GetProcAddress("glClientActiveTexture");
    if (!pfn_GetBufferSubData) {
        printf("[DUMP] glGetBufferSubData unavailable: meshes stored in VBOs will be skipped\n");
    }

    char path[MAX_PATH + 64];
    snprintf(path, sizeof(path), "%s/index.csv", g_out_dir);
    g_index = fopen(path, "w");
    if (g_index) {
        fprintf(g_index, "mesh,triangles,vertices,texture,tex_width,tex_height,first_seen_ms,position_type,view_x,view_y,view_z\n");
    }

    g_mesh_dump_enabled = 1;
    printf("[DUMP] Mesh dumping enabled -> %s/\n", g_out_dir);
}

void mesh_dump_shutdown(void) {
    if (!g_mesh_dump_enabled) return;
    g_mesh_dump_enabled = 0;
    if (g_index) {
        fclose(g_index);
        g_index = NULL;
    }
    printf("[DUMP] Wrote %u meshes and %u textures to %s/\n", g_mesh_count, g_tex_count, g_out_dir);
}
