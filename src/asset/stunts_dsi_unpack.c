#include "stunts_dsi_unpack.h"
#include <string.h>
#include <stdlib.h>

#define RS_RLE_ESCLOOKUP_LEN 0x100
#define RS_RLE_ESCSEQ_POS    0x01
#define RS_VLE_ESC_LEN       0x10
#define RS_VLE_ALPH_LEN      0x100
#define RS_VLE_ESC_WIDTH     0x40
#define RS_VLE_NUM_SYMB      0x80

uint32_t stunts_dsi_get_decompressed_size(const uint8_t* src, size_t src_len) {
    if (!src || src_len < 4) return 0;
    uint32_t sizel = (uint32_t)src[1] | ((uint32_t)src[2] << 8);
    uint32_t sizeh = (uint32_t)src[3];
    return sizel | (sizeh << 16);
}

static uint32_t dsi_decomp_rle_seq(const uint8_t* src, size_t srclen, uint8_t* dst, uint8_t esc) {
    const uint8_t* srcend = src + srclen;
    uint8_t* dststart = dst;

    while (src < srcend) {
        uint8_t cur = *src++;
        if (cur == esc) {
            const uint8_t* seqstart = src;
            while (src < srcend && (cur = *src++) != esc) {
                *dst++ = cur;
            }
            if (src >= srcend) break;
            uint8_t rep = (*src++) - 1;
            const uint8_t* seqend = src;
            while (rep--) {
                const uint8_t* s = seqstart;
                while (s < seqend - 2) {
                    *dst++ = *s++;
                }
            }
            src = seqend;
        } else {
            *dst++ = cur;
        }
    }
    return (uint32_t)(dst - dststart);
}

static uint32_t dsi_decomp_rle_single(const uint8_t* src, size_t srclen, uint8_t* dst, uint32_t len, const uint8_t* esclookup) {
    uint8_t* dststart = dst;
    uint8_t* dstend = dst + len;
    const uint8_t* srcend = src + srclen;

    while (dst < dstend && src < srcend) {
        uint8_t cur = *src++;
        uint8_t esc_val = esclookup[cur];
        if (esc_val) {
            if (esc_val == 1) {
                if (src >= srcend) break;
                uint8_t rep = *src++;
                if (src >= srcend) break;
                cur = *src++;
                while (rep-- && dst < dstend) {
                    *dst++ = cur;
                }
            } else if (esc_val == 3) {
                if (src + 2 > srcend) break;
                uint16_t repw = (uint16_t)*src++;
                repw |= (uint16_t)(*src++) << 8;
                if (src >= srcend) break;
                cur = *src++;
                while (repw-- && dst < dstend) {
                    *dst++ = cur;
                }
            } else {
                uint8_t rep = esc_val - 1;
                if (src >= srcend) break;
                cur = *src++;
                while (rep-- && dst < dstend) {
                    *dst++ = cur;
                }
            }
        } else {
            *dst++ = cur;
        }
    }
    return (uint32_t)(dst - dststart);
}

static uint32_t dsi_decomp_rle(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_capacity) {
    if (src_len < 10) return 0;
    uint32_t len = stunts_dsi_get_decompressed_size(src, src_len);
    if (len > dst_capacity) return 0;

    const uint8_t* origsrc = src + 4;
    uint32_t srclen = (uint32_t)origsrc[0] | ((uint32_t)origsrc[1] << 8) | ((uint32_t)origsrc[2] << 16);
    (void)srclen;
    uint8_t esclen = origsrc[4];
    bool skipseq = (esclen & 0x80) == 0x80;
    esclen &= ~0x80;

    const uint8_t* payload = origsrc + 5 + esclen;
    size_t payload_len = src_len - (payload - src);

    uint8_t esclookup[RS_RLE_ESCLOOKUP_LEN];
    memset(esclookup, 0, sizeof(esclookup));
    for (uint8_t i = 0; i < esclen && i < RS_VLE_ESC_LEN; ++i) {
        esclookup[origsrc[5 + i]] = i + 1;
    }

    if (!skipseq) {
        uint8_t* temp_buf = (uint8_t*)malloc(len + 4096);
        if (!temp_buf) return 0;
        uint32_t passlen = dsi_decomp_rle_seq(payload, payload_len, temp_buf, origsrc[5 + RS_RLE_ESCSEQ_POS]);
        uint32_t res = dsi_decomp_rle_single(temp_buf, passlen, dst, len, esclookup);
        free(temp_buf);
        return res;
    }

    return dsi_decomp_rle_single(payload, payload_len, dst, len, esclookup);
}

/* The DOS original reads the compressed stream without bounds checks: on the
 * final symbol the bit-buffer refill runs once more than necessary and reads
 * one byte past the payload. In DOS that byte was simply whatever followed in
 * the resource heap and was never consumed; here it is a heap overread, and
 * the garbage it returns made decompression — and therefore the whole
 * simulation — non-deterministic between runs.
 *
 * VLE_RD() keeps the read sequence identical but yields 0 once the payload is
 * exhausted, so the result is defined and reproducible. Guard only; it does
 * not change any byte the decoder actually consumes. */
#define VLE_RD() ((p < src_end) ? *p++ : (p++, (uint8_t)0))

static uint32_t dsi_decomp_vle(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_capacity) {
    if (src_len < 10) return 0;
    uint32_t len = stunts_dsi_get_decompressed_size(src, src_len);
    if (len > dst_capacity) return 0;

    const uint8_t* const src_end = src + src_len;
    const uint8_t* p = src + 4;
    uint8_t esclen = VLE_RD();
    bool additive = (esclen & 0x80) == 0x80;
    esclen &= ~0x80;

    const uint8_t* wdtpos = p;
    uint16_t esc1[RS_VLE_ESC_LEN], esc2[RS_VLE_ESC_LEN];
    uint8_t alph[RS_VLE_ALPH_LEN], symb[RS_VLE_ALPH_LEN], wdth[RS_VLE_ALPH_LEN];
    uint32_t alphlen = 0;
    uint32_t j = 0;

    for (uint32_t i = 0; i < esclen && i < RS_VLE_ESC_LEN; ++i, j *= 2) {
        esc1[i] = (uint16_t)(alphlen - j);
        uint8_t tmp = VLE_RD();
        j += tmp;
        alphlen += tmp;
        esc2[i] = (uint16_t)j;
    }

    for (uint32_t i = 0; i < alphlen && i < RS_VLE_ALPH_LEN; ++i) {
        alph[i] = VLE_RD();
    }

    const uint8_t* codpos = p;
    p = wdtpos;

    uint32_t width = 1;
    uint32_t widthdistr = (esclen >= 8 ? 8 : esclen);
    uint32_t numsymb = RS_VLE_NUM_SYMB;
    uint32_t si = 0;
    j = 0;

    for (; width <= widthdistr; ++width, numsymb >>= 1) {
        uint8_t symbwdth = VLE_RD();
        for (; symbwdth > 0; --symbwdth, ++j) {
            for (uint32_t n = numsymb; n > 0 && si < RS_VLE_ALPH_LEN; --n, ++si) {
                symb[si] = alph[j];
                wdth[si] = (uint8_t)width;
            }
        }
    }

    for (; si < RS_VLE_ALPH_LEN; ++si) {
        wdth[si] = RS_VLE_ESC_WIDTH;
    }

    p = codpos;
    uint16_t curword = (uint16_t)(((uint16_t)(p < src_end ? p[0] : 0) << 8)
                                  | (p + 1 < src_end ? p[1] : 0));
    p += 2;
    uint8_t curwdt = 8;
    uint8_t cursymb = 0;
    uint32_t lenleft = len;

    while (lenleft > 0) {
        uint8_t code = (uint8_t)(curword >> 8);
        uint8_t nextwdt = wdth[code];

        if (nextwdt > 8) {
            code = (uint8_t)curword;
            curword >>= 8;
            uint32_t k = 7;
            while (1) {
                if (!curwdt) {
                    code = VLE_RD();
                    curwdt = 8;
                }
                curword = (curword << 1) + ((code & 0x80) == 0x80);
                code <<= 1;
                --curwdt;
                ++k;

                if (k < RS_VLE_ESC_LEN && curword < esc2[k]) {
                    curword += esc1[k];
                    if (additive) {
                        cursymb += alph[curword];
                    } else {
                        cursymb = alph[curword];
                    }
                    *dst++ = cursymb;
                    --lenleft;
                    break;
                }
            }
            curword = (uint16_t)((code << curwdt) | VLE_RD());
            nextwdt = 8 - curwdt;
            curwdt = 8;
        } else {
            if (additive) {
                cursymb += symb[code];
            } else {
                cursymb = symb[code];
            }
            *dst++ = cursymb;
            --lenleft;

            if (curwdt < nextwdt) {
                curword <<= curwdt;
                nextwdt -= curwdt;
                curwdt = 8;
                curword |= VLE_RD();
            }
        }
        curword <<= nextwdt;
        curwdt -= nextwdt;
    }

    return len;
}

uint32_t stunts_dsi_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_capacity) {
    if (!src || src_len < 4 || !dst) return 0;
    uint8_t first_byte = src[0];

    if (first_byte & 0x80) {
        /* Multi-pass compressed */
        uint8_t passes = first_byte & ~0x80;
        uint32_t final_len = stunts_dsi_get_decompressed_size(src, src_len);
        if (final_len > dst_capacity) return 0;

        uint8_t* buf_in = (uint8_t*)malloc(src_len);
        uint8_t* buf_out = (uint8_t*)malloc(final_len + 65536);
        if (!buf_in || !buf_out) {
            free(buf_in); free(buf_out);
            return 0;
        }

        memcpy(buf_in, src + 4, src_len - 4);
        size_t cur_len = src_len - 4;

        for (uint8_t p = 0; p < passes; p++) {
            uint8_t pass_type = buf_in[0];
            uint32_t out_len = 0;
            if (pass_type == 1) {
                out_len = dsi_decomp_rle(buf_in, cur_len, buf_out, final_len + 65536);
            } else if (pass_type == 2) {
                out_len = dsi_decomp_vle(buf_in, cur_len, buf_out, final_len + 65536);
            }
            if (out_len == 0) {
                free(buf_in); free(buf_out);
                return 0;
            }
            if (p + 1 < passes) {
                buf_in = (uint8_t*)realloc(buf_in, out_len);
                memcpy(buf_in, buf_out, out_len);
                cur_len = out_len;
            } else {
                memcpy(dst, buf_out, out_len);
                free(buf_in); free(buf_out);
                return out_len;
            }
        }
        free(buf_in); free(buf_out);
        return 0;
    } else {
        /* Single pass */
        uint8_t type = first_byte;
        if (type == 1) {
            return dsi_decomp_rle(src, src_len, dst, dst_capacity);
        } else if (type == 2) {
            return dsi_decomp_vle(src, src_len, dst, dst_capacity);
        }
    }
    return 0;
}
