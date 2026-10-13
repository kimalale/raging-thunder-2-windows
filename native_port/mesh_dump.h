#ifndef MESH_DUMP_H
#define MESH_DUMP_H

#include <windows.h>
#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set by mesh_dump_init(); checked by the draw wrappers so the dumper
 * costs nothing when disabled. */
extern int g_mesh_dump_enabled;

/* Must be called with a current GL context. Creates out_dir if needed. */
void mesh_dump_init(const char *out_dir);
void mesh_dump_shutdown(void);

/* Called just before the corresponding GL draw, with GL state intact. */
void mesh_dump_draw_arrays(GLenum mode, GLint first, GLsizei count);
void mesh_dump_draw_elements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);

#ifdef __cplusplus
}
#endif

#endif /* MESH_DUMP_H */
