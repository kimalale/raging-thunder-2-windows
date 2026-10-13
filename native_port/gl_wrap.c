#include "gl_wrap.h"
#include "gl_buffers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#undef CDECL
#define CDECL __attribute__((cdecl))
#define FIX2FLT(x) ((float)(x) / 65536.0f)

/* Dynamic extension function pointers (loaded via SDL_GL_GetProcAddress) */
typedef void (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRY *PFNGLCLIENTACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRY *PFNGLCOMPRESSEDTEXIMAGE2DPROC)(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data);
typedef void (APIENTRY *PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data);
typedef void (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, ptrdiff_t size, const void *data, GLenum usage);
typedef void (APIENTRY *PFNGLBUFFERSUBDATAPROC)(GLenum target, ptrdiff_t offset, ptrdiff_t size, const void *data);
typedef void (APIENTRY *PFNGLGETBUFFERPARAMETERIVPROC)(GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4FPROC)(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
typedef void (APIENTRY *PFNGLPOINTPARAMETERFPROC)(GLenum pname, GLfloat param);
typedef void (APIENTRY *PFNGLPOINTPARAMETERFVPROC)(GLenum pname, const GLfloat *params);
typedef void (APIENTRY *PFNGLSAMPLECOVERAGEPROC)(GLclampf value, GLboolean invert);

/* FBO extensions */
typedef void (APIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (APIENTRY *PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef void (APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint *framebuffers);
typedef GLenum (APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRY *PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void (APIENTRY *PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint *renderbuffers);
typedef void (APIENTRY *PFNGLDELETERENDERBUFFERSPROC)(GLsizei n, const GLuint *renderbuffers);
typedef void (APIENTRY *PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void (APIENTRY *PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRY *PFNGLGENERATEMIPMAPPROC)(GLenum target);

static PFNGLACTIVETEXTUREPROC pfn_glActiveTexture = NULL;
static PFNGLCLIENTACTIVETEXTUREPROC pfn_glClientActiveTexture = NULL;
static PFNGLCOMPRESSEDTEXIMAGE2DPROC pfn_glCompressedTexImage2D = NULL;
static PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC pfn_glCompressedTexSubImage2D = NULL;
static PFNGLBUFFERDATAPROC pfn_glBufferData = NULL;
static PFNGLBUFFERSUBDATAPROC pfn_glBufferSubData = NULL;
static PFNGLGETBUFFERPARAMETERIVPROC pfn_glGetBufferParameteriv = NULL;
static PFNGLMULTITEXCOORD4FPROC pfn_glMultiTexCoord4f = NULL;
static PFNGLPOINTPARAMETERFPROC pfn_glPointParameterf = NULL;
static PFNGLPOINTPARAMETERFVPROC pfn_glPointParameterfv = NULL;
static PFNGLSAMPLECOVERAGEPROC pfn_glSampleCoverage = NULL;

static PFNGLBINDFRAMEBUFFERPROC pfn_glBindFramebuffer = NULL;
static PFNGLGENFRAMEBUFFERSPROC pfn_glGenFramebuffers = NULL;
static PFNGLDELETEFRAMEBUFFERSPROC pfn_glDeleteFramebuffers = NULL;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC pfn_glCheckFramebufferStatus = NULL;
static PFNGLFRAMEBUFFERTEXTURE2DPROC pfn_glFramebufferTexture2D = NULL;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC pfn_glFramebufferRenderbuffer = NULL;
static PFNGLGENRENDERBUFFERSPROC pfn_glGenRenderbuffers = NULL;
static PFNGLDELETERENDERBUFFERSPROC pfn_glDeleteRenderbuffers = NULL;
static PFNGLBINDRENDERBUFFERPROC pfn_glBindRenderbuffer = NULL;
static PFNGLRENDERBUFFERSTORAGEPROC pfn_glRenderbufferStorage = NULL;
static PFNGLGENERATEMIPMAPPROC pfn_glGenerateMipmap = NULL;

/* ------------------------------------------------------------- */
/* TEXTURE QUALITY ENHANCEMENTS                                  */
/* ------------------------------------------------------------- */
/* The engine was tuned for phone GPUs and mostly uses plain GL_LINEAR
 * without mipmaps, which shimmers badly at desktop resolutions. When
 * enabled, level-0 uploads of 2D textures get auto-generated mipmaps,
 * GL_LINEAR minification is upgraded to trilinear, and anisotropic
 * filtering is applied. Render targets (NULL-pixel uploads) and
 * glCopyTexImage2D textures are left untouched since their contents
 * change on the GPU and the mip chain would go stale. */

#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#define MAX_TRACKED_TEXTURES 65536

static int g_tex_mipmaps = 0;
static float g_tex_aniso = 0.0f;
static unsigned char g_tex_has_mips[MAX_TRACKED_TEXTURES / 8];

static GLuint bound_texture_2d(void) {
    GLint tex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    return (GLuint)tex;
}

static int tex_has_mips(GLuint tex) {
    if (tex == 0 || tex >= MAX_TRACKED_TEXTURES) return 0;
    return (g_tex_has_mips[tex >> 3] >> (tex & 7)) & 1;
}

static void tex_set_mips(GLuint tex, int on) {
    if (tex == 0 || tex >= MAX_TRACKED_TEXTURES) return;
    if (on) g_tex_has_mips[tex >> 3] |= (unsigned char)(1 << (tex & 7));
    else    g_tex_has_mips[tex >> 3] &= (unsigned char)~(1 << (tex & 7));
}

/* Upgrade GL_LINEAR minification to trilinear on textures we mipmapped. */
static GLint upgrade_min_filter(GLenum target, GLenum pname, GLint param) {
    if (!g_tex_mipmaps || target != GL_TEXTURE_2D || pname != GL_TEXTURE_MIN_FILTER) return param;
    if (param == GL_LINEAR && tex_has_mips(bound_texture_2d())) return GL_LINEAR_MIPMAP_LINEAR;
    return param;
}

/* ------------------------------------------------------------- */
/* __cdecl WRAPPERS FOR DESKTOP OPENGL FUNCTIONS                 */
/* ------------------------------------------------------------- */

static void CDECL wrap_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { glClearColor(r, g, b, a); }
static void CDECL wrap_glClearDepthf(GLclampf depth) { glClearDepth(depth); }
static void CDECL wrap_glDepthRangef(GLclampf n, GLclampf f) { glDepthRange(n, f); }
static void CDECL wrap_glGetFloatv(GLenum pname, GLfloat *params) { glGetFloatv(pname, params); }
static void CDECL wrap_glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) { glGetTexParameterfv(target, pname, params); }
static void CDECL wrap_glLineWidth(GLfloat width) { glLineWidth(width); }
static void CDECL wrap_glPolygonOffset(GLfloat factor, GLfloat units) { glPolygonOffset(factor, units); }
static void CDECL wrap_glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    if (pname == GL_TEXTURE_MIN_FILTER) {
        glTexParameteri(target, pname, upgrade_min_filter(target, pname, (GLint)param));
        return;
    }
    glTexParameterf(target, pname, param);
}
static void CDECL wrap_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (pname == GL_TEXTURE_MIN_FILTER) {
        glTexParameteri(target, pname, upgrade_min_filter(target, pname, (GLint)params[0]));
        return;
    }
    glTexParameterfv(target, pname, params);
}
static void CDECL wrap_glBindTexture(GLenum target, GLuint texture) { glBindTexture(target, texture); }
static void CDECL wrap_glBlendFunc(GLenum sfactor, GLenum dfactor) { glBlendFunc(sfactor, dfactor); }
static void CDECL wrap_glClear(GLbitfield mask) { glClear(mask); }
static void CDECL wrap_glClearStencil(GLint s) { glClearStencil(s); }
static void CDECL wrap_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { glColorMask(r, g, b, a); }
static void CDECL wrap_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
}
static void CDECL wrap_glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
    glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
}
static void CDECL wrap_glCullFace(GLenum mode) { glCullFace(mode); }
static void CDECL wrap_glDeleteTextures(GLsizei n, const GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) tex_set_mips(textures[i], 0);
    glDeleteTextures(n, textures);
}
static void CDECL wrap_glDepthFunc(GLenum func) { glDepthFunc(func); }
static void CDECL wrap_glDepthMask(GLboolean flag) { glDepthMask(flag); }
static void CDECL wrap_glDisable(GLenum cap) { glDisable(cap); }
static void CDECL wrap_glDrawArrays(GLenum mode, GLint first, GLsizei count) { glDrawArrays(mode, first, count); }
static void CDECL wrap_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) { glDrawElements(mode, count, type, indices); }
static void CDECL wrap_glEnable(GLenum cap) { glEnable(cap); }
static void CDECL wrap_glFinish(void) { glFinish(); }
static void CDECL wrap_glFlush(void) { glFlush(); }
static void CDECL wrap_glFrontFace(GLenum mode) { glFrontFace(mode); }
static void CDECL wrap_glGetBooleanv(GLenum pname, GLboolean *params) { glGetBooleanv(pname, params); }
static void CDECL wrap_glGenTextures(GLsizei n, GLuint *textures) { glGenTextures(n, textures); }
static GLenum CDECL wrap_glGetError(void) { return glGetError(); }
static void CDECL wrap_glGetIntegerv(GLenum pname, GLint *params) { glGetIntegerv(pname, params); }
static const GLubyte * CDECL wrap_glGetString(GLenum name) {
    if (name == GL_EXTENSIONS) {
        static char ext_buf[8192] = {0};
        if (!ext_buf[0]) {
            const char *real = (const char *)glGetString(GL_EXTENSIONS);
            snprintf(ext_buf, sizeof(ext_buf),
                "%s GL_OES_point_sprite GL_OES_point_size_array "
                "GL_OES_texture_cube_map GL_OES_read_format "
                "GL_OES_framebuffer_object GL_OES_matrix_palette "
                "GL_OES_byte_coordinates GL_OES_fixed_point "
                "GL_OES_compressed_paletted_texture",
                real ? real : "");
        }
        return (const GLubyte *)ext_buf;
    }
    return glGetString(name);
}
static void CDECL wrap_glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) { glGetTexParameteriv(target, pname, params); }
static void CDECL wrap_glHint(GLenum target, GLenum mode) { glHint(target, mode); }
static GLboolean CDECL wrap_glIsEnabled(GLenum cap) { return glIsEnabled(cap); }
static GLboolean CDECL wrap_glIsTexture(GLuint texture) { return glIsTexture(texture); }
static void CDECL wrap_glPixelStorei(GLenum pname, GLint param) { glPixelStorei(pname, param); }
static void CDECL wrap_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, GLvoid *pixels) { glReadPixels(x, y, w, h, f, t, pixels); }
static void CDECL wrap_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) { glScissor(x, y, width, height); }
static void CDECL wrap_glStencilFunc(GLenum func, GLint ref, GLuint mask) { glStencilFunc(func, ref, mask); }
static void CDECL wrap_glStencilMask(GLuint mask) { glStencilMask(mask); }
static void CDECL wrap_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) { glStencilOp(fail, zfail, zpass); }
static void CDECL wrap_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    if (!g_tex_mipmaps || target != GL_TEXTURE_2D || level != 0) {
        glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
        return;
    }

    GLuint tex = bound_texture_2d();
    int enhance = (pixels != NULL);
    int had_mips = tex_has_mips(tex);

    if (enhance) {
        glTexParameteri(target, GL_GENERATE_MIPMAP, GL_TRUE);
    } else if (had_mips) {
        /* Texture id is being reused as a render target: undo our changes */
        glTexParameteri(target, GL_GENERATE_MIPMAP, GL_FALSE);
    }

    glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    tex_set_mips(tex, enhance);

    GLint min_filter = 0;
    glGetTexParameteriv(target, GL_TEXTURE_MIN_FILTER, &min_filter);
    if (enhance) {
        if (min_filter == GL_LINEAR) glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        if (g_tex_aniso > 1.0f) glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, g_tex_aniso);
    } else if (had_mips) {
        if (min_filter == GL_LINEAR_MIPMAP_LINEAR) glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        if (g_tex_aniso > 1.0f) glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
    }
}
static void CDECL wrap_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    glTexParameteri(target, pname, upgrade_min_filter(target, pname, param));
}
static void CDECL wrap_glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
    if (pname == GL_TEXTURE_MIN_FILTER) {
        glTexParameteri(target, pname, upgrade_min_filter(target, pname, params[0]));
        return;
    }
    glTexParameteriv(target, pname, params);
}
static void CDECL wrap_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}
static void CDECL wrap_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { glViewport(x, y, width, height); }

static void CDECL wrap_glAlphaFunc(GLenum func, GLclampf ref) { glAlphaFunc(func, ref); }
static void CDECL wrap_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { glColor4f(r, g, b, a); }
static void CDECL wrap_glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { glColor4ub(r, g, b, a); }
static void CDECL wrap_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { glColorPointer(size, type, stride, pointer); }
static void CDECL wrap_glDisableClientState(GLenum array) { glDisableClientState(array); }
static void CDECL wrap_glEnableClientState(GLenum array) { glEnableClientState(array); }
static void CDECL wrap_glFogf(GLenum pname, GLfloat param) { glFogf(pname, param); }
static void CDECL wrap_glFogfv(GLenum pname, const GLfloat *params) { glFogfv(pname, params); }
static void CDECL wrap_glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { glFrustum(l, r, b, t, n, f); }
static void CDECL wrap_glGetClipPlanef(GLenum plane, GLfloat *equation) { glGetClipPlane(plane, (GLdouble*)equation); }
static void CDECL wrap_glClipPlanef(GLenum plane, const GLfloat *equation) {
    GLdouble eq[4] = { equation[0], equation[1], equation[2], equation[3] };
    glClipPlane(plane, eq);
}
static void CDECL wrap_glGetLightfv(GLenum light, GLenum pname, GLfloat *params) { glGetLightfv(light, pname, params); }
static void CDECL wrap_glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params) { glGetMaterialfv(face, pname, params); }
static void CDECL wrap_glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params) { glGetTexEnvfv(target, pname, params); }
static void CDECL wrap_glGetTexEnviv(GLenum target, GLenum pname, GLint *params) { glGetTexEnviv(target, pname, params); }
static void CDECL wrap_glLightModelf(GLenum pname, GLfloat param) { glLightModelf(pname, param); }
static void CDECL wrap_glLightModelfv(GLenum pname, const GLfloat *params) { glLightModelfv(pname, params); }
static void CDECL wrap_glLightf(GLenum light, GLenum pname, GLfloat param) { glLightf(light, pname, param); }
static void CDECL wrap_glLightfv(GLenum light, GLenum pname, const GLfloat *params) { glLightfv(light, pname, params); }
static void CDECL wrap_glLoadIdentity(void) { glLoadIdentity(); }
static void CDECL wrap_glLoadMatrixf(const GLfloat *m) { glLoadMatrixf(m); }
static void CDECL wrap_glLogicOp(GLenum opcode) { glLogicOp(opcode); }
static void CDECL wrap_glMaterialf(GLenum face, GLenum pname, GLfloat param) { glMaterialf(face, pname, param); }
static void CDECL wrap_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params) { glMaterialfv(face, pname, params); }
static void CDECL wrap_glMatrixMode(GLenum mode) { glMatrixMode(mode); }
static void CDECL wrap_glMultMatrixf(const GLfloat *m) { glMultMatrixf(m); }
static void CDECL wrap_glNormal3f(GLfloat x, GLfloat y, GLfloat z) { glNormal3f(x, y, z); }
static void CDECL wrap_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) { glNormalPointer(type, stride, pointer); }
static void CDECL wrap_glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { glOrtho(l, r, b, t, n, f); }
static void CDECL wrap_glPointSize(GLfloat size) { glPointSize(size); }
static void CDECL wrap_glPopMatrix(void) { glPopMatrix(); }
static void CDECL wrap_glPushMatrix(void) { glPushMatrix(); }
static void CDECL wrap_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { glRotatef(angle, x, y, z); }
static void CDECL wrap_glScalef(GLfloat x, GLfloat y, GLfloat z) { glScalef(x, y, z); }
static void CDECL wrap_glShadeModel(GLenum mode) { glShadeModel(mode); }
static void CDECL wrap_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { glTexCoordPointer(size, type, stride, pointer); }
static void CDECL wrap_glTexEnvi(GLenum target, GLenum pname, GLint param) { glTexEnvi(target, pname, param); }
static void CDECL wrap_glTexEnvf(GLenum target, GLenum pname, GLfloat param) { glTexEnvf(target, pname, param); }
static void CDECL wrap_glTexEnviv(GLenum target, GLenum pname, const GLint *params) { glTexEnviv(target, pname, params); }
static void CDECL wrap_glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params) { glTexEnvfv(target, pname, params); }
static void CDECL wrap_glTranslatef(GLfloat x, GLfloat y, GLfloat z) { glTranslatef(x, y, z); }
static void CDECL wrap_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { glVertexPointer(size, type, stride, pointer); }

/* Extension wrappers */
static void CDECL wrap_glActiveTexture(GLenum texture) { if (pfn_glActiveTexture) pfn_glActiveTexture(texture); }
static void CDECL wrap_glClientActiveTexture(GLenum texture) { if (pfn_glClientActiveTexture) pfn_glClientActiveTexture(texture); }
static void CDECL wrap_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) {
    if (pfn_glCompressedTexImage2D) pfn_glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}
static void CDECL wrap_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) {
    if (pfn_glCompressedTexSubImage2D) pfn_glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
}
static void CDECL wrap_glBufferData(GLenum target, ptrdiff_t size, const void *data, GLenum usage) {
    if (pfn_glBufferData) pfn_glBufferData(target, size, data, usage);
}
static void CDECL wrap_glBufferSubData(GLenum target, ptrdiff_t offset, ptrdiff_t size, const void *data) {
    if (pfn_glBufferSubData) pfn_glBufferSubData(target, offset, size, data);
}
static void CDECL wrap_glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    if (pfn_glGetBufferParameteriv) pfn_glGetBufferParameteriv(target, pname, params);
}
static void CDECL wrap_glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    if (pfn_glMultiTexCoord4f) pfn_glMultiTexCoord4f(target, s, t, r, q);
}
static void CDECL wrap_glPointParameterf(GLenum pname, GLfloat param) {
    if (pfn_glPointParameterf) pfn_glPointParameterf(pname, param);
}
static void CDECL wrap_glPointParameterfv(GLenum pname, const GLfloat *params) {
    if (pfn_glPointParameterfv) pfn_glPointParameterfv(pname, params);
}
static void CDECL wrap_glSampleCoverage(GLclampf value, GLboolean invert) {
    if (pfn_glSampleCoverage) pfn_glSampleCoverage(value, invert);
}

/* FBO wrappers */
static void CDECL wrap_glBindFramebuffer(GLenum target, GLuint framebuffer) {
    if (pfn_glBindFramebuffer) pfn_glBindFramebuffer(target, framebuffer);
}
static void CDECL wrap_glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
    if (pfn_glGenFramebuffers) pfn_glGenFramebuffers(n, framebuffers);
}
static void CDECL wrap_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    if (pfn_glDeleteFramebuffers) pfn_glDeleteFramebuffers(n, framebuffers);
}
static GLenum CDECL wrap_glCheckFramebufferStatus(GLenum target) {
    return pfn_glCheckFramebufferStatus ? pfn_glCheckFramebufferStatus(target) : 0x8CD5; /* GL_FRAMEBUFFER_COMPLETE */
}
static void CDECL wrap_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (pfn_glFramebufferTexture2D) pfn_glFramebufferTexture2D(target, attachment, textarget, texture, level);
}
static void CDECL wrap_glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    if (pfn_glFramebufferRenderbuffer) pfn_glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}
static void CDECL wrap_glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
    if (pfn_glGenRenderbuffers) pfn_glGenRenderbuffers(n, renderbuffers);
}
static void CDECL wrap_glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
    if (pfn_glDeleteRenderbuffers) pfn_glDeleteRenderbuffers(n, renderbuffers);
}
static void CDECL wrap_glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    if (pfn_glBindRenderbuffer) pfn_glBindRenderbuffer(target, renderbuffer);
}
static void CDECL wrap_glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    if (pfn_glRenderbufferStorage) pfn_glRenderbufferStorage(target, internalformat, width, height);
}
static void CDECL wrap_glGenerateMipmap(GLenum target) {
    if (pfn_glGenerateMipmap) pfn_glGenerateMipmap(target);
}

/* ------------------------------------------------------------- */
/* FIXED-POINT (__cdecl) WRAPPERS                                */
/* ------------------------------------------------------------- */

static void CDECL wrap_glOrthox(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) {
    glOrtho(FIX2FLT(l), FIX2FLT(r), FIX2FLT(b), FIX2FLT(t), FIX2FLT(n), FIX2FLT(f));
}
static void CDECL wrap_glFrustumx(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) {
    glFrustum(FIX2FLT(l), FIX2FLT(r), FIX2FLT(b), FIX2FLT(t), FIX2FLT(n), FIX2FLT(f));
}
static void CDECL wrap_glTranslatex(GLfixed x, GLfixed y, GLfixed z) {
    glTranslatef(FIX2FLT(x), FIX2FLT(y), FIX2FLT(z));
}
static void CDECL wrap_glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z) {
    glRotatef(FIX2FLT(angle), FIX2FLT(x), FIX2FLT(y), FIX2FLT(z));
}
static void CDECL wrap_glScalex(GLfixed x, GLfixed y, GLfixed z) {
    glScalef(FIX2FLT(x), FIX2FLT(y), FIX2FLT(z));
}
static void CDECL wrap_glClearColorx(GLfixed r, GLfixed g, GLfixed b, GLfixed a) {
    glClearColor(FIX2FLT(r), FIX2FLT(g), FIX2FLT(b), FIX2FLT(a));
}
static void CDECL wrap_glClearDepthx(GLclampx depth) {
    glClearDepth(FIX2FLT(depth));
}
static void CDECL wrap_glDepthRangex(GLclampx n, GLclampx f) {
    glDepthRange(FIX2FLT(n), FIX2FLT(f));
}
static void CDECL wrap_glColor4x(GLfixed r, GLfixed g, GLfixed b, GLfixed a) {
    glColor4f(FIX2FLT(r), FIX2FLT(g), FIX2FLT(b), FIX2FLT(a));
}
static void CDECL wrap_glNormal3x(GLfixed x, GLfixed y, GLfixed z) {
    glNormal3f(FIX2FLT(x), FIX2FLT(y), FIX2FLT(z));
}
static void CDECL wrap_glAlphaFuncx(GLenum func, GLclampx ref) {
    glAlphaFunc(func, FIX2FLT(ref));
}
static void CDECL wrap_glLoadMatrixx(const GLfixed *m) {
    float f[16];
    for (int i = 0; i < 16; i++) f[i] = FIX2FLT(m[i]);
    glLoadMatrixf(f);
}
static void CDECL wrap_glMultMatrixx(const GLfixed *m) {
    float f[16];
    for (int i = 0; i < 16; i++) f[i] = FIX2FLT(m[i]);
    glMultMatrixf(f);
}
static void CDECL wrap_glLightx(GLenum light, GLenum pname, GLfixed param) {
    glLightf(light, pname, FIX2FLT(param));
}
static void CDECL wrap_glLightxv(GLenum light, GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_SPOT_EXPONENT || pname == GL_SPOT_CUTOFF ||
                 pname == GL_CONSTANT_ATTENUATION || pname == GL_LINEAR_ATTENUATION ||
                 pname == GL_QUADRATIC_ATTENUATION) ? 1 :
                (pname == GL_SPOT_DIRECTION) ? 3 : 4;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glLightfv(light, pname, f);
}
static void CDECL wrap_glMaterialx(GLenum face, GLenum pname, GLfixed param) {
    glMaterialf(face, pname, FIX2FLT(param));
}
static void CDECL wrap_glMaterialxv(GLenum face, GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_SHININESS) ? 1 : 4;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glMaterialfv(face, pname, f);
}
static void CDECL wrap_glLightModelx(GLenum pname, GLfixed param) {
    glLightModelf(pname, FIX2FLT(param));
}
static void CDECL wrap_glLightModelxv(GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_LIGHT_MODEL_TWO_SIDE) ? 1 : 4;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glLightModelfv(pname, f);
}
static void CDECL wrap_glTexEnvx(GLenum target, GLenum pname, GLfixed param) {
    glTexEnvf(target, pname, (pname == GL_TEXTURE_ENV_MODE) ? (float)param : FIX2FLT(param));
}
static void CDECL wrap_glTexEnvxv(GLenum target, GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_TEXTURE_ENV_COLOR) ? 4 : 1;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glTexEnvfv(target, pname, f);
}
static void CDECL wrap_glFogx(GLenum pname, GLfixed param) {
    glFogf(pname, FIX2FLT(param));
}
static void CDECL wrap_glFogxv(GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_FOG_COLOR) ? 4 : 1;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glFogfv(pname, f);
}
static void CDECL wrap_glPointParameterx(GLenum pname, GLfixed param) {
    wrap_glPointParameterf(pname, FIX2FLT(param));
}
static void CDECL wrap_glPointParameterxv(GLenum pname, const GLfixed *params) {
    float f[3] = { FIX2FLT(params[0]), FIX2FLT(params[1]), FIX2FLT(params[2]) };
    wrap_glPointParameterfv(pname, f);
}
static void CDECL wrap_glPointSizex(GLfixed size) {
    glPointSize(FIX2FLT(size));
}
static void CDECL wrap_glPolygonOffsetx(GLfixed factor, GLfixed units) {
    glPolygonOffset(FIX2FLT(factor), FIX2FLT(units));
}
static void CDECL wrap_glLineWidthx(GLfixed width) {
    glLineWidth(FIX2FLT(width));
}
static void CDECL wrap_glSampleCoveragex(GLclampx value, GLboolean invert) {
    wrap_glSampleCoverage(FIX2FLT(value), invert);
}
static void CDECL wrap_glMultiTexCoord4x(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q) {
    wrap_glMultiTexCoord4f(target, FIX2FLT(s), FIX2FLT(t), FIX2FLT(r), FIX2FLT(q));
}
static void CDECL wrap_glTexParameterx(GLenum target, GLenum pname, GLfixed param) {
    wrap_glTexParameteri(target, pname, param);
}
static void CDECL wrap_glTexParameterxv(GLenum target, GLenum pname, const GLfixed *params) {
    wrap_glTexParameteriv(target, pname, (const GLint*)params);
}
static void CDECL wrap_glGetFixedv(GLenum pname, GLfixed *params) {
    float f[16];
    glGetFloatv(pname, f);
    for (int i = 0; i < 16; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
static void CDECL wrap_glGetLightxv(GLenum light, GLenum pname, GLfixed *params) {
    float f[4];
    glGetLightfv(light, pname, f);
    for (int i = 0; i < 4; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
static void CDECL wrap_glGetMaterialxv(GLenum face, GLenum pname, GLfixed *params) {
    float f[4];
    glGetMaterialfv(face, pname, f);
    for (int i = 0; i < 4; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
static void CDECL wrap_glGetTexEnvxv(GLenum target, GLenum pname, GLfixed *params) {
    float f[4];
    glGetTexEnvfv(target, pname, f);
    for (int i = 0; i < 4; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
static void CDECL wrap_glGetTexParameterxv(GLenum target, GLenum pname, GLfixed *params) {
    float f[4];
    glGetTexParameterfv(target, pname, f);
    for (int i = 0; i < 4; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
static void CDECL wrap_glGetPointerv(GLenum pname, void **params) {
    glGetPointerv(pname, params);
}
static void CDECL wrap_glClipPlanex(GLenum plane, const GLfixed *equation) {
    GLdouble eq[4] = { FIX2FLT(equation[0]), FIX2FLT(equation[1]), FIX2FLT(equation[2]), FIX2FLT(equation[3]) };
    glClipPlane(plane, eq);
}
static void CDECL wrap_glGetClipPlanex(GLenum plane, GLfixed *equation) {
    GLdouble eq[4];
    glGetClipPlane(plane, eq);
    for (int i = 0; i < 4; i++) equation[i] = (GLfixed)(eq[i] * 65536.0f);
}

static void CDECL wrap_stub(void) {}
static int CDECL wrap_ret0(void) { return 0; }
static int CDECL wrap_ret1(void) { return 1; }

void gl_wrap_init(void) {
    gl_buffers_init();
    
    pfn_glActiveTexture = (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
    pfn_glClientActiveTexture = (PFNGLCLIENTACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glClientActiveTexture");
    pfn_glCompressedTexImage2D = (PFNGLCOMPRESSEDTEXIMAGE2DPROC)SDL_GL_GetProcAddress("glCompressedTexImage2D");
    pfn_glCompressedTexSubImage2D = (PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC)SDL_GL_GetProcAddress("glCompressedTexSubImage2D");
    pfn_glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
    pfn_glBufferSubData = (PFNGLBUFFERSUBDATAPROC)SDL_GL_GetProcAddress("glBufferSubData");
    pfn_glGetBufferParameteriv = (PFNGLGETBUFFERPARAMETERIVPROC)SDL_GL_GetProcAddress("glGetBufferParameteriv");
    pfn_glMultiTexCoord4f = (PFNGLMULTITEXCOORD4FPROC)SDL_GL_GetProcAddress("glMultiTexCoord4f");
    pfn_glPointParameterf = (PFNGLPOINTPARAMETERFPROC)SDL_GL_GetProcAddress("glPointParameterf");
    pfn_glPointParameterfv = (PFNGLPOINTPARAMETERFVPROC)SDL_GL_GetProcAddress("glPointParameterfv");
    pfn_glSampleCoverage = (PFNGLSAMPLECOVERAGEPROC)SDL_GL_GetProcAddress("glSampleCoverage");
    
    /* FBO */
    pfn_glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    if (!pfn_glBindFramebuffer) pfn_glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebufferEXT");
    
    pfn_glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    if (!pfn_glGenFramebuffers) pfn_glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffersEXT");
    
    pfn_glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteFramebuffers");
    if (!pfn_glDeleteFramebuffers) pfn_glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteFramebuffersEXT");
    
    pfn_glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    if (!pfn_glCheckFramebufferStatus) pfn_glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT");
    
    pfn_glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    if (!pfn_glFramebufferTexture2D) pfn_glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2DEXT");
    
    pfn_glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
    if (!pfn_glFramebufferRenderbuffer) pfn_glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)SDL_GL_GetProcAddress("glFramebufferRenderbufferEXT");
    
    pfn_glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)SDL_GL_GetProcAddress("glGenRenderbuffers");
    if (!pfn_glGenRenderbuffers) pfn_glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)SDL_GL_GetProcAddress("glGenRenderbuffersEXT");
    
    pfn_glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteRenderbuffers");
    if (!pfn_glDeleteRenderbuffers) pfn_glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteRenderbuffersEXT");
    
    pfn_glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)SDL_GL_GetProcAddress("glBindRenderbuffer");
    if (!pfn_glBindRenderbuffer) pfn_glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)SDL_GL_GetProcAddress("glBindRenderbufferEXT");
    
    pfn_glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)SDL_GL_GetProcAddress("glRenderbufferStorage");
    if (!pfn_glRenderbufferStorage) pfn_glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)SDL_GL_GetProcAddress("glRenderbufferStorageEXT");
    
    pfn_glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)SDL_GL_GetProcAddress("glGenerateMipmap");
    if (!pfn_glGenerateMipmap) pfn_glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)SDL_GL_GetProcAddress("glGenerateMipmapEXT");
}

void gl_wrap_set_texture_quality(int mipmaps, int anisotropy) {
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    int major = 0, minor = 0;
    if (version) sscanf(version, "%d.%d", &major, &minor);

    /* GL_GENERATE_MIPMAP is core since OpenGL 1.4 */
    g_tex_mipmaps = mipmaps && (major > 1 || (major == 1 && minor >= 4));
    if (mipmaps && !g_tex_mipmaps) {
        printf("[GL] OpenGL %s lacks automatic mipmap generation; texture enhancements disabled\n", version ? version : "?");
    }

    g_tex_aniso = 0.0f;
    if (g_tex_mipmaps && anisotropy > 1 && extensions && strstr(extensions, "GL_EXT_texture_filter_anisotropic")) {
        GLfloat max_aniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
        g_tex_aniso = (float)anisotropy < max_aniso ? (float)anisotropy : max_aniso;
    }

    printf("[GL] Texture filtering: %s, anisotropy %.0fx\n",
           g_tex_mipmaps ? "trilinear (auto mipmaps)" : "original",
           g_tex_aniso > 1.0f ? g_tex_aniso : 1.0f);
}

/* Dispatch lookup table */
typedef struct {
    const char *name;
    void *fn;
} gl_dispatch_t;

static const gl_dispatch_t g_gl_dispatch[] = {
    { "glClearColor",            (void*)wrap_glClearColor },
    { "glClearDepthf",           (void*)wrap_glClearDepthf },
    { "glDepthRangef",           (void*)wrap_glDepthRangef },
    { "glGetFloatv",             (void*)wrap_glGetFloatv },
    { "glGetTexParameterfv",     (void*)wrap_glGetTexParameterfv },
    { "glLineWidth",             (void*)wrap_glLineWidth },
    { "glPolygonOffset",         (void*)wrap_glPolygonOffset },
    { "glTexParameterf",         (void*)wrap_glTexParameterf },
    { "glTexParameterfv",        (void*)wrap_glTexParameterfv },
    { "glBindTexture",           (void*)wrap_glBindTexture },
    { "glBlendFunc",             (void*)wrap_glBlendFunc },
    { "glClear",                 (void*)wrap_glClear },
    { "glClearStencil",          (void*)wrap_glClearStencil },
    { "glColorMask",             (void*)wrap_glColorMask },
    { "glCopyTexImage2D",        (void*)wrap_glCopyTexImage2D },
    { "glCopyTexSubImage2D",     (void*)wrap_glCopyTexSubImage2D },
    { "glCullFace",              (void*)wrap_glCullFace },
    { "glDeleteTextures",        (void*)wrap_glDeleteTextures },
    { "glDepthFunc",             (void*)wrap_glDepthFunc },
    { "glDepthMask",             (void*)wrap_glDepthMask },
    { "glDisable",               (void*)wrap_glDisable },
    { "glDrawArrays",            (void*)wrap_glDrawArrays },
    { "glDrawElements",          (void*)wrap_glDrawElements },
    { "glEnable",                (void*)wrap_glEnable },
    { "glFinish",                (void*)wrap_glFinish },
    { "glFlush",                 (void*)wrap_glFlush },
    { "glFrontFace",             (void*)wrap_glFrontFace },
    { "glGetBooleanv",           (void*)wrap_glGetBooleanv },
    { "glGenTextures",           (void*)wrap_glGenTextures },
    { "glGetError",              (void*)wrap_glGetError },
    { "glGetIntegerv",           (void*)wrap_glGetIntegerv },
    { "glGetString",             (void*)wrap_glGetString },
    { "glGetTexParameteriv",     (void*)wrap_glGetTexParameteriv },
    { "glHint",                  (void*)wrap_glHint },
    { "glIsEnabled",             (void*)wrap_glIsEnabled },
    { "glIsTexture",             (void*)wrap_glIsTexture },
    { "glPixelStorei",           (void*)wrap_glPixelStorei },
    { "glReadPixels",            (void*)wrap_glReadPixels },
    { "glScissor",               (void*)wrap_glScissor },
    { "glStencilFunc",           (void*)wrap_glStencilFunc },
    { "glStencilMask",           (void*)wrap_glStencilMask },
    { "glStencilOp",             (void*)wrap_glStencilOp },
    { "glTexImage2D",            (void*)wrap_glTexImage2D },
    { "glTexParameteri",         (void*)wrap_glTexParameteri },
    { "glTexParameteriv",        (void*)wrap_glTexParameteriv },
    { "glTexSubImage2D",         (void*)wrap_glTexSubImage2D },
    { "glViewport",              (void*)wrap_glViewport },
    { "glAlphaFunc",             (void*)wrap_glAlphaFunc },
    { "glColor4f",               (void*)wrap_glColor4f },
    { "glColor4ub",              (void*)wrap_glColor4ub },
    { "glColorPointer",          (void*)wrap_glColorPointer },
    { "glDisableClientState",    (void*)wrap_glDisableClientState },
    { "glEnableClientState",     (void*)wrap_glEnableClientState },
    { "glFogf",                  (void*)wrap_glFogf },
    { "glFogfv",                 (void*)wrap_glFogfv },
    { "glFrustumf",              (void*)wrap_glFrustumf },
    { "glGetClipPlanef",         (void*)wrap_glGetClipPlanef },
    { "glClipPlanef",            (void*)wrap_glClipPlanef },
    { "glGetLightfv",            (void*)wrap_glGetLightfv },
    { "glGetMaterialfv",         (void*)wrap_glGetMaterialfv },
    { "glGetTexEnvfv",           (void*)wrap_glGetTexEnvfv },
    { "glGetTexEnviv",           (void*)wrap_glGetTexEnviv },
    { "glLightModelf",           (void*)wrap_glLightModelf },
    { "glLightModelfv",          (void*)wrap_glLightModelfv },
    { "glLightf",                (void*)wrap_glLightf },
    { "glLightfv",               (void*)wrap_glLightfv },
    { "glLoadIdentity",          (void*)wrap_glLoadIdentity },
    { "glLoadMatrixf",           (void*)wrap_glLoadMatrixf },
    { "glLogicOp",               (void*)wrap_glLogicOp },
    { "glMaterialf",             (void*)wrap_glMaterialf },
    { "glMaterialfv",            (void*)wrap_glMaterialfv },
    { "glMatrixMode",            (void*)wrap_glMatrixMode },
    { "glMultMatrixf",           (void*)wrap_glMultMatrixf },
    { "glNormal3f",              (void*)wrap_glNormal3f },
    { "glNormalPointer",         (void*)wrap_glNormalPointer },
    { "glOrthof",                (void*)wrap_glOrthof },
    { "glPointSize",             (void*)wrap_glPointSize },
    { "glPopMatrix",             (void*)wrap_glPopMatrix },
    { "glPushMatrix",            (void*)wrap_glPushMatrix },
    { "glRotatef",               (void*)wrap_glRotatef },
    { "glScalef",                (void*)wrap_glScalef },
    { "glShadeModel",            (void*)wrap_glShadeModel },
    { "glTexCoordPointer",       (void*)wrap_glTexCoordPointer },
    { "glTexEnvi",               (void*)wrap_glTexEnvi },
    { "glTexEnvf",               (void*)wrap_glTexEnvf },
    { "glTexEnviv",              (void*)wrap_glTexEnviv },
    { "glTexEnvfv",              (void*)wrap_glTexEnvfv },
    { "glTranslatef",            (void*)wrap_glTranslatef },
    { "glVertexPointer",         (void*)wrap_glVertexPointer },

    /* Buffer objects */
    { "glGenBuffers",            (void*)glGenBuffers_soloader },
    { "glGenBuffersOES",         (void*)glGenBuffers_soloader },
    { "glBindBuffer",            (void*)glBindBuffer_soloader },
    { "glBindBufferOES",         (void*)glBindBuffer_soloader },
    { "glDeleteBuffers",         (void*)glDeleteBuffers_soloader },
    { "glDeleteBuffersOES",      (void*)glDeleteBuffers_soloader },
    { "glIsBuffer",              (void*)glIsBuffer_soloader },
    { "glIsBufferOES",           (void*)glIsBuffer_soloader },
    { "glBufferData",            (void*)wrap_glBufferData },
    { "glBufferDataOES",         (void*)wrap_glBufferData },
    { "glBufferSubData",         (void*)wrap_glBufferSubData },
    { "glBufferSubDataOES",      (void*)wrap_glBufferSubData },
    { "glGetBufferParameteriv",  (void*)wrap_glGetBufferParameteriv },
    { "glGetBufferParameterivOES",(void*)wrap_glGetBufferParameteriv },

    /* Extension wrappers */
    { "glActiveTexture",         (void*)wrap_glActiveTexture },
    { "glClientActiveTexture",   (void*)wrap_glClientActiveTexture },
    { "glCompressedTexImage2D",  (void*)wrap_glCompressedTexImage2D },
    { "glCompressedTexSubImage2D",(void*)wrap_glCompressedTexSubImage2D },
    { "glMultiTexCoord4f",       (void*)wrap_glMultiTexCoord4f },
    { "glPointParameterf",       (void*)wrap_glPointParameterf },
    { "glPointParameterfv",      (void*)wrap_glPointParameterfv },
    { "glSampleCoverage",        (void*)wrap_glSampleCoverage },

    /* Fixed-point conversions */
    { "glOrthox",                (void*)wrap_glOrthox },
    { "glFrustumx",              (void*)wrap_glFrustumx },
    { "glTranslatex",            (void*)wrap_glTranslatex },
    { "glRotatex",               (void*)wrap_glRotatex },
    { "glScalex",                (void*)wrap_glScalex },
    { "glClearColorx",           (void*)wrap_glClearColorx },
    { "glClearDepthx",           (void*)wrap_glClearDepthx },
    { "glDepthRangex",           (void*)wrap_glDepthRangex },
    { "glColor4x",               (void*)wrap_glColor4x },
    { "glNormal3x",              (void*)wrap_glNormal3x },
    { "glAlphaFuncx",            (void*)wrap_glAlphaFuncx },
    { "glLoadMatrixx",           (void*)wrap_glLoadMatrixx },
    { "glMultMatrixx",           (void*)wrap_glMultMatrixx },
    { "glLightx",                (void*)wrap_glLightx },
    { "glLightxv",               (void*)wrap_glLightxv },
    { "glMaterialx",             (void*)wrap_glMaterialx },
    { "glMaterialxv",            (void*)wrap_glMaterialxv },
    { "glLightModelx",           (void*)wrap_glLightModelx },
    { "glLightModelxv",          (void*)wrap_glLightModelxv },
    { "glTexEnvx",               (void*)wrap_glTexEnvx },
    { "glTexEnvxv",              (void*)wrap_glTexEnvxv },
    { "glFogx",                  (void*)wrap_glFogx },
    { "glFogxv",                 (void*)wrap_glFogxv },
    { "glPointParameterx",       (void*)wrap_glPointParameterx },
    { "glPointParameterxv",      (void*)wrap_glPointParameterxv },
    { "glPointSizex",            (void*)wrap_glPointSizex },
    { "glPolygonOffsetx",        (void*)wrap_glPolygonOffsetx },
    { "glLineWidthx",            (void*)wrap_glLineWidthx },
    { "glSampleCoveragex",       (void*)wrap_glSampleCoveragex },
    { "glMultiTexCoord4x",       (void*)wrap_glMultiTexCoord4x },
    { "glTexParameterx",         (void*)wrap_glTexParameterx },
    { "glTexParameterxv",        (void*)wrap_glTexParameterxv },
    { "glGetFixedv",             (void*)wrap_glGetFixedv },
    { "glGetLightxv",            (void*)wrap_glGetLightxv },
    { "glGetMaterialxv",         (void*)wrap_glGetMaterialxv },
    { "glGetTexEnvxv",           (void*)wrap_glGetTexEnvxv },
    { "glGetTexParameterxv",     (void*)wrap_glGetTexParameterxv },
    { "glGetPointerv",           (void*)wrap_glGetPointerv },
    { "glClipPlanex",            (void*)wrap_glClipPlanex },
    { "glGetClipPlanex",         (void*)wrap_glGetClipPlanex },

    /* FBO Extensions */
    { "glBindFramebufferOES",    (void*)wrap_glBindFramebuffer },
    { "glBindFramebuffer",       (void*)wrap_glBindFramebuffer },
    { "glGenFramebuffersOES",    (void*)wrap_glGenFramebuffers },
    { "glGenFramebuffers",       (void*)wrap_glGenFramebuffers },
    { "glDeleteFramebuffersOES", (void*)wrap_glDeleteFramebuffers },
    { "glDeleteFramebuffers",    (void*)wrap_glDeleteFramebuffers },
    { "glCheckFramebufferStatusOES",(void*)wrap_glCheckFramebufferStatus },
    { "glCheckFramebufferStatus",(void*)wrap_glCheckFramebufferStatus },
    { "glFramebufferTexture2DOES",(void*)wrap_glFramebufferTexture2D },
    { "glFramebufferTexture2D",  (void*)wrap_glFramebufferTexture2D },
    { "glFramebufferRenderbufferOES",(void*)wrap_glFramebufferRenderbuffer },
    { "glFramebufferRenderbuffer",(void*)wrap_glFramebufferRenderbuffer },
    { "glGenRenderbuffersOES",   (void*)wrap_glGenRenderbuffers },
    { "glGenRenderbuffers",      (void*)wrap_glGenRenderbuffers },
    { "glDeleteRenderbuffersOES",(void*)wrap_glDeleteRenderbuffers },
    { "glDeleteRenderbuffers",   (void*)wrap_glDeleteRenderbuffers },
    { "glBindRenderbufferOES",   (void*)wrap_glBindRenderbuffer },
    { "glBindRenderbuffer",      (void*)wrap_glBindRenderbuffer },
    { "glRenderbufferStorageOES",(void*)wrap_glRenderbufferStorage },
    { "glRenderbufferStorage",   (void*)wrap_glRenderbufferStorage },
    { "glGenerateMipmapOES",     (void*)wrap_glGenerateMipmap },
    { "glGenerateMipmap",        (void*)wrap_glGenerateMipmap },

    /* EGL Stubs */
    { "eglGetProcAddress",       (void*)gl_get_proc },
    { "eglGetError",             (void*)wrap_ret0 }
};

void *gl_get_proc(const char *name) {
    if (!name) return NULL;
    
    for (size_t i = 0; i < sizeof(g_gl_dispatch)/sizeof(g_gl_dispatch[0]); i++) {
        if (strcmp(g_gl_dispatch[i].name, name) == 0) {
            return g_gl_dispatch[i].fn;
        }
    }
    
    if (strncmp(name, "egl", 3) == 0) {
        return (void*)wrap_ret1;
    }
    
    printf("[GL] Unmapped function requested: %s\n", name);
    return (void*)wrap_stub;
}
