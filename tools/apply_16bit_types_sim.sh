#!/usr/bin/env bash
# Mechanical 16-bit Watcom -> fixed-width C99 type mapping for the vendored
# restunts renderer sources in src/render_faithful/.
#
# Watcom 16-bit real mode: int/unsigned = 16 bits, long = 32 bits.
# Order matters: multi-word types first, then bare keywords.
# char is left as-is; the build uses -funsigned-char to match Watcom's
# default unsigned char semantics.
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../src/sim_faithful" && pwd)"

for f in "$DIR"/*.c "$DIR"/*.h; do
  perl -pi -e '
    s/\bunsigned\s+long\s+int\b/uint32_t/g;
    s/\bunsigned\s+long\b/uint32_t/g;
    s/\bsigned\s+long\b/int32_t/g;
    s/\blong\s+int\b/int32_t/g;
    s/\blong\b/int32_t/g;
    s/\bunsigned\s+short\s+int\b/uint16_t/g;
    s/\bunsigned\s+short\b/uint16_t/g;
    s/\bsigned\s+short\b/int16_t/g;
    s/\bshort\s+int\b/int16_t/g;
    s/\bshort\b/int16_t/g;
    s/\bunsigned\s+char\b/uint8_t/g;
    s/\bsigned\s+char\b/int8_t/g;
    s/\bunsigned\s+int\b/uint16_t/g;
    s/\bsigned\s+int\b/int16_t/g;
    s/\bunsigned\b/uint16_t/g;
    s/\bint\b/int16_t/g;
  ' "$f"
done

echo "Applied 16-bit type mapping to $DIR"
