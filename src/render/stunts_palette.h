#ifndef STUNTS_PALETTE_H
#define STUNTS_PALETTE_H

#include "stunts_render_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Loads the original 256-color VGA palette from SDMAIN.PVS (tag '!pal').
 */
bool stunts_palette_load(const char* sdmain_path, stunts_palette_t* out_palette);

/**
 * Fallback default Stunts VGA palette.
 */
void stunts_palette_init_default(stunts_palette_t* out_palette);

/**
 * Resolves a Stunts material ID to a palette color index.
 */
uint8_t stunts_material_to_color(uint8_t material_id, uint8_t paint_job);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_PALETTE_H */
