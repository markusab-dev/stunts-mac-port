#ifndef STUNTS_SHAPE3D_H
#define STUNTS_SHAPE3D_H

#include "stunts_render_types.h"
#include "../asset/stunts_asset_loader.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    stunts_shape3d_t shapes[160];
    uint16_t count;
} stunts_shape_db_t;

/**
 * Loads all 3D shape models from GAME1.P3S (track pieces), GAME2.P3S (scenery),
 * and ST<CAR>.P3S (car LODs & wheels).
 */
bool stunts_shape_db_load(const char* data_dir, const char* car_id, stunts_shape_db_t* out_db);

/**
 * Finds a shape by its 4-character tag (e.g. "road", "fini", "loop", "car0").
 */
const stunts_shape3d_t* stunts_shape_db_find(const stunts_shape_db_t* db, const char* name);

/**
 * Parses an in-memory 3D shape buffer.
 */
bool stunts_shape_parse(const char* name, const uint8_t* data, uint32_t size, stunts_shape3d_t* out_shape);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_SHAPE3D_H */
