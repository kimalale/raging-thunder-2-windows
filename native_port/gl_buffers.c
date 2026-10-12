#include "gl_buffers.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <SDL2/SDL.h>

typedef void (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef GLboolean (APIENTRY *PFNGLISBUFFERPROC)(GLuint buffer);

static PFNGLGENBUFFERSPROC pfn_glGenBuffers = NULL;
static PFNGLBINDBUFFERPROC pfn_glBindBuffer = NULL;
static PFNGLDELETEBUFFERSPROC pfn_glDeleteBuffers = NULL;
static PFNGLISBUFFERPROC pfn_glIsBuffer = NULL;

typedef struct {
    GLuint key;   /* engine name, 0 = empty slot */
    GLuint value; /* real GL name */
} buf_entry;

static buf_entry *table = NULL;
static uint32_t table_cap = 0;
static uint32_t table_used = 0;

#define TOMBSTONE_VALUE 0xFFFFFFFFu

void gl_buffers_init(void) {
    pfn_glGenBuffers = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
    pfn_glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    pfn_glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
    pfn_glIsBuffer = (PFNGLISBUFFERPROC)SDL_GL_GetProcAddress("glIsBuffer");
}

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352d;
    x ^= x >> 15; x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static buf_entry *find_slot(GLuint key, int for_insert) {
    uint32_t mask = table_cap - 1;
    uint32_t i = hash_u32(key) & mask;
    buf_entry *tomb = NULL;
    for (;;) {
        buf_entry *e = &table[i];
        if (e->key == 0)
            return for_insert && tomb ? tomb : e;
        if (e->key == key && e->value != TOMBSTONE_VALUE)
            return e;
        if (e->value == TOMBSTONE_VALUE && !tomb)
            tomb = e;
        i = (i + 1) & mask;
    }
}

static void grow(void) {
    buf_entry *old = table;
    uint32_t old_cap = table_cap;
    table_cap = old_cap ? old_cap * 2 : 1024;
    table = (buf_entry *)calloc(table_cap, sizeof(buf_entry));
    table_used = 0;
    for (uint32_t i = 0; i < old_cap; i++) {
        if (old[i].key && old[i].value != TOMBSTONE_VALUE) {
            buf_entry *e = find_slot(old[i].key, 1);
            *e = old[i];
            table_used++;
        }
    }
    free(old);
}

static GLuint lookup(GLuint name) {
    if (!name || !table)
        return 0;
    buf_entry *e = find_slot(name, 0);
    return e->key ? e->value : 0;
}

static void insert(GLuint name, GLuint real) {
    if (!table || (table_used + 1) * 4 >= table_cap * 3)
        grow();
    buf_entry *e = find_slot(name, 1);
    if (!e->key)
        table_used++;
    e->key = name;
    e->value = real;
}

static void erase(GLuint name) {
    if (!name || !table)
        return;
    buf_entry *e = find_slot(name, 0);
    if (e->key)
        e->value = TOMBSTONE_VALUE;
}

static GLuint resolve(GLuint name) {
    if (!name)
        return 0;
    GLuint real = lookup(name);
    if (!real) {
        if (pfn_glGenBuffers) {
            pfn_glGenBuffers(1, &real);
        }
        insert(name, real);
    }
    return real;
}

static GLuint next_gen_name = 0x40000000u;

void glGenBuffers_soloader(GLsizei n, GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint name;
        do {
            name = next_gen_name++;
        } while (!name || lookup(name));
        GLuint real = 0;
        if (pfn_glGenBuffers) {
            pfn_glGenBuffers(1, &real);
        }
        insert(name, real);
        buffers[i] = name;
    }
}

void glBindBuffer_soloader(GLenum target, GLuint buffer) {
    if (pfn_glBindBuffer) {
        pfn_glBindBuffer(target, resolve(buffer));
    }
}

void glDeleteBuffers_soloader(GLsizei n, const GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint real = lookup(buffers[i]);
        if (real) {
            if (pfn_glDeleteBuffers) {
                pfn_glDeleteBuffers(1, &real);
            }
            erase(buffers[i]);
        }
    }
}

GLboolean glIsBuffer_soloader(GLuint buffer) {
    return lookup(buffer) ? GL_TRUE : GL_FALSE;
}
