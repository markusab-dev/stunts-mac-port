#ifndef STUNTS_CANONICAL_STATE_H
#define STUNTS_CANONICAL_STATE_H

#include "../common/stunts_types.h"
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Writes a canonical simulation state frame as a single line of JSON.
 */
void stunts_canonical_state_write_jsonl(FILE* f, const stunts_canonical_state_t* state);

/**
 * Writes a canonical simulation state frame in compact binary format (128 bytes).
 */
void stunts_canonical_state_write_binary(FILE* f, const stunts_canonical_state_t* state);

/**
 * Reads a canonical state from a line of JSON. Returns true on success.
 */
bool stunts_canonical_state_read_jsonl(const char* line, stunts_canonical_state_t* out_state);

/**
 * Reads a compact binary canonical state frame (128 bytes). Returns true on success.
 */
bool stunts_canonical_state_read_binary(FILE* f, stunts_canonical_state_t* out_state);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_CANONICAL_STATE_H */
