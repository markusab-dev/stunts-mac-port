#!/usr/bin/env python3
"""
tools/compare_frame_320.py - Frame-by-Frame Pixel & Geometry Comparator
Compares reference vs native 320x200 Stunts frames and produces visual diffs.
"""

import sys
import os
from PIL import Image, ImageChops

def compare_frames(ref_path, native_path, diff_path):
    os.makedirs(os.path.dirname(diff_path), exist_ok=True)

    im_ref = Image.open(ref_path).convert("RGB")
    im_nat = Image.open(native_path).convert("RGB")

    if im_ref.size != im_nat.size:
        print(f"[ERROR] Size mismatch: {im_ref.size} vs {im_nat.size}")
        return

    w, h = im_ref.size
    total_pixels = w * h
    diff = ImageChops.difference(im_ref, im_nat)
    bbox = diff.getbbox()

    identical_pixels = 0
    diff_pixels = 0
    max_color_delta = 0

    ref_data = list(im_ref.getdata())
    nat_data = list(im_nat.getdata())

    diff_img = Image.new("RGB", (w, h), (0, 0, 0))
    diff_pixels_list = []

    for i in range(total_pixels):
        r1, g1, b1 = ref_data[i]
        r2, g2, b2 = nat_data[i]
        delta = max(abs(r1 - r2), abs(g1 - g2), abs(b1 - b2))
        if delta == 0:
            identical_pixels += 1
            diff_pixels_list.append((30, 30, 30)) # Dark gray for identical
        else:
            diff_pixels += 1
            if delta > max_color_delta:
                max_color_delta = delta
            # Highlight difference in bright magenta/red
            diff_pixels_list.append((255, 0, 128))

    diff_img.putdata(diff_pixels_list)
    diff_img.save(diff_path)

    match_pct = (identical_pixels / total_pixels) * 100.0

    print("================================================================================")
    print("           STUNTS 320x200 FAITHFUL RENDER COMPARISON REPORT                     ")
    print("================================================================================")
    print(f"Reference Image:       {ref_path}")
    print(f"Native Image:          {native_path}")
    print(f"Diff Image:            {diff_path}")
    print(f"Resolution:            {w}x{h} ({total_pixels} total pixels)")
    print(f"Identical Pixels:      {identical_pixels} / {total_pixels} ({match_pct:.2f}%)")
    print(f"Differing Pixels:      {diff_pixels} ({100.0 - match_pct:.2f}%)")
    print(f"Bounding Box of Diff:  {bbox}")
    print(f"Max Channel Delta:     {max_color_delta}")
    print("================================================================================")

if __name__ == "__main__":
    ref = "tests/render_reference/frame_00000_original.png"
    nat = "tests/render_native/frame_00000_native.png"
    diff = "tests/render_diff/frame_00000_diff.png"
    compare_frames(ref, nat, diff)
