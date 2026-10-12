#ifndef GL_BUFFERS_H
#define GL_BUFFERS_H

#include <windows.h>
#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_buffers_init(void);
void glGenBuffers_soloader(GLsizei n, GLuint *buffers);
void glBindBuffer_soloader(GLenum target, GLuint buffer);
void glDeleteBuffers_soloader(GLsizei n, const GLuint *buffers);
GLboolean glIsBuffer_soloader(GLuint buffer);

#ifdef __cplusplus
}
#endif

#endif /* GL_BUFFERS_H */
