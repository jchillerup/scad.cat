#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Evaluate SCAD source and produce a binary STL mesh.
 *
 * On success:
 *   returns 0, *stl_out points to malloc'd binary STL bytes of length *stl_len.
 *   Caller must free *stl_out with openscad_free().
 *
 * On failure:
 *   returns -1, *error_out points to a malloc'd NUL-terminated error string (if
 *   error_out is non-NULL).  Caller must free *error_out with openscad_free().
 *
 * Thread safety: NOT reentrant.  Call sequentially; concurrent calls are
 * undefined behaviour due to global state inside OpenSCAD's evaluator.
 */
int openscad_evaluate(const char *scad_source,
                      uint8_t **stl_out, size_t *stl_len,
                      char **error_out);

void openscad_free(void *ptr);

#ifdef __cplusplus
}
#endif
