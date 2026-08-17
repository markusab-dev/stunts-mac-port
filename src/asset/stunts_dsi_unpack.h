#ifndef STUNTS_DSI_UNPACK_H
#define STUNTS_DSI_UNPACK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * In-Memory DSI (Distinctive Software Inc.) Resource Decompressor
 * Handles Multi-Pass Run-Length Encoding (RLE) and Variable-Length (VLE)
 * ------------------------------------------------------------------------- */

/**
 * Returns expected uncompressed size in bytes from the compressed header.
 */
uint32_t stunts_dsi_get_decompressed_size(const uint8_t* src, size_t src_len);

/**
 * Decompresses in-memory DSI compressed buffer into dst buffer.
 * @param src Pointer to compressed input buffer
 * @param src_len Length of compressed input buffer in bytes
 * @param dst Pointer to destination buffer (must be at least get_decompressed_size bytes)
 * @param dst_capacity Capacity of destination buffer in bytes
 * @return Number of decompressed bytes written, or 0 on error.
 */
uint32_t stunts_dsi_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_capacity);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_DSI_UNPACK_H */
