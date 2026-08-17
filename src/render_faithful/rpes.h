#ifndef RESTUNTS_RPES_H
#define RESTUNTS_RPES_H

#include <stdint.h>

/*
 * rpes.h - the .PES container.
 *
 * Every other 2D archive in the game is a .PVS: DSI-compressed, and once
 * unpacked the shapes are already 8 bits per pixel.  SDCRED.PES - the only
 * .PES the game ships - is compressed the same way but its shapes are still
 * in the planar form the artwork was authored in: up to four 1-bit planes,
 * each plane contributing one nibble value where its bit is set.
 *
 * The original handles that in file_load_shape2d (restunts shape2d.c:583):
 *
 *     file_decomp                    - the same unpacker as a .PVS
 *     file_unflip_shape2d_pes        - un-transpose the planes that say so
 *     file_load_shape2d_esh          - expand 1bpp x N planes to 8bpp,
 *                                      then remap through palmap[]
 *
 * See rpes.c for the format and the two deviations.
 */

/* Load a .PES by base name ("sdcred").  The returned handle is the archive
 * exactly as file_load_resource(2, ...) returns it; unload it with
 * unload_resource().  Shapes must be fetched with pes_locate_shape() or
 * pes_locate_many() below, NOT with locate_shape_*, because the archive's
 * own bytes are still planar. */
void far* file_load_resource_pes(const char* basename);

/* locate_shape_alt + expand.  The expanded copy is cached per (archive,
 * name), so calling this twice returns the same pointer and expands once.
 * Returns NULL when the name is not in the archive. */
void far* pes_locate_shape(void far* res, const char* name);

/* seg003 locate_many_resources over a run of four-character names, each
 * looked up with pes_locate_shape(). */
void pes_locate_many(void far* res, const char* names, void far** result);

/* Drop every expanded copy belonging to `res`.  unload_resource() does not
 * know about them, so the caller does this first. */
void pes_release(void far* res);

#endif /* RESTUNTS_RPES_H */
