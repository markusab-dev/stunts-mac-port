#ifndef STUNTS_INTERP_H
#define STUNTS_INTERP_H

#include "stunts_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Linearly interpolates between two consecutive 20 Hz simulation snapshots.
 * alpha in [0.0, 1.0] representing fractional progress between ticks.
 * Affects ONLY visual presentation; never modifies simulation state.
 */
void stunts_interp_snapshot(const stunts_render_snapshot_t* prev,
                            const stunts_render_snapshot_t* curr,
                            float alpha,
                            stunts_render_snapshot_t* out_interp);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_INTERP_H */
