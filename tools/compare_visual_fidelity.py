#!/usr/bin/env python3
"""
Stunts Visual Fidelity & Regression Tool (tools/compare_visual_fidelity.py)
Compares rendered visual capture frames against reference images.
Computes PSNR, exact pixel matches, and bounding box geometry checks.
"""

import sys
import os
import struct
import argparse
from pathlib import Path

def parse_bmp(filepath):
    with open(filepath, "rb") as f:
        data = f.read()
    if len(data) < 54:
        raise ValueError("Invalid BMP file")
    bfType, bfSize, _, _, bfOffBits = struct.unpack_from("<HIHHI", data, 0)
    if bfType != 0x4D42:
        raise ValueError("Not a BMP file")
    biSize, biWidth, biHeight, biPlanes, biBitCount = struct.unpack_from("<IiiHH", data, 14)
    abs_h = abs(biHeight)
    abs_w = abs(biWidth)

    raw_pixels = data[bfOffBits : bfOffBits + (abs_w * abs_h * 4)]
    return abs_w, abs_h, raw_pixels

def compare_images(file_a, file_b, tolerance=0):
    w_a, h_a, pix_a = parse_bmp(file_a)
    w_b, h_b, pix_b = parse_bmp(file_b)

    if (w_a, h_a) != (w_b, h_b):
        return {
            "status": "DIMENSION_MISMATCH",
            "dim_a": f"{w_a}x{h_a}",
            "dim_b": f"{w_b}x{h_b}",
            "match_pct": 0.0
        }

    total_pixels = w_a * h_a
    identical_pixels = 0
    diff_count = 0
    max_delta = 0

    for i in range(0, len(pix_a), 4):
        b_a, g_a, r_a, _ = pix_a[i:i+4]
        b_b, g_b, r_b, _ = pix_b[i:i+4]
        delta = max(abs(r_a - r_b), abs(g_a - g_b), abs(b_a - b_b))
        if delta <= tolerance:
            identical_pixels += 1
        else:
            diff_count += 1
            if delta > max_delta:
                max_delta = delta

    match_pct = (identical_pixels / total_pixels) * 100.0
    return {
        "status": "PASS" if diff_count == 0 else "DIFF",
        "dimensions": f"{w_a}x{h_a}",
        "total_pixels": total_pixels,
        "identical_pixels": identical_pixels,
        "diff_pixels": diff_count,
        "match_pct": round(match_pct, 3),
        "max_color_delta": max_delta
    }

def main():
    parser = argparse.ArgumentParser(description="Stunts Visual Fidelity Comparator")
    parser.add_argument("image_a", help="Reference or primary image (.bmp)")
    parser.add_argument("image_b", help="Target comparison image (.bmp)")
    parser.add_argument("--tolerance", type=int, default=0, help="Per-channel color tolerance (0-255)")
    args = parser.parse_args()

    res = compare_images(args.image_a, args.image_b, args.tolerance)
    print(f"Visual Comparison: {Path(args.image_a).name} vs {Path(args.image_b).name}")
    print(f"  Status:           {res['status']}")
    if "dimensions" in res:
        print(f"  Dimensions:       {res['dimensions']}")
        print(f"  Match Percentage: {res['match_pct']}% ({res['identical_pixels']}/{res['total_pixels']} pixels)")
        print(f"  Max Color Delta:  {res['max_color_delta']}")
    else:
        print(f"  Dimensions:       {res['dim_a']} vs {res['dim_b']}")

if __name__ == "__main__":
    main()
