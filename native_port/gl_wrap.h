#ifndef GL_WRAP_H
#define GL_WRAP_H

#include <stdint.h>
#include <windows.h>
#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t GLfixed;
typedef int32_t GLclampx;

void gl_wrap_init(void);
void *gl_get_proc(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* GL_WRAP_H */
