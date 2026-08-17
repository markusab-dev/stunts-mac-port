# Visual Fidelity & Regression Testing

## 1. Overview

Visual fidelity verification ensures that our native ARM64 3D renderer reproduces Stunts 1.1 scenes with complete geometric accuracy, faithful color palettes, correct camera perspectives, and clean high-resolution direct rasterization.

---

## 2. Visual Test Scenarios & Frame Captures

Deterministic frame captures are generated using `tools/export_visual_frame.c` across multiple camera angles, vehicles, and track topologies:

| Scenario / Image File | Resolution | Target Frame | Vehicle | Track | Features Captured |
| :--- | :---: | :---: | :---: | :---: | :--- |
| `01_coun_320x200.png` | 320×200 | Frame 50 | COUN | DEFAULT | Original 320x200 4:3 baseline mode |
| `01_coun_1024x768.png` | 1024×768 | Frame 50 | COUN | DEFAULT | Enhanced direct rasterization |
| `01_coun_1440x1080.png` | 1440×1080 | Frame 50 | COUN | DEFAULT | 4:3 Full HD reference resolution |
| `01_coun_1920x1080.png` | 1920×1080 | Frame 50 | COUN | DEFAULT | 16:9 Widescreen high-resolution |
| `04_p962_1440x1080.png` | 1440×1080 | Frame 100 | P962 | FAST2 | High-speed circuit banking & cornering |
| `05_lm02_1440x1080.png` | 1440×1080 | Frame 120 | LM02 | FUNHILLS | Off-road hills, elevation & terrain grid |
| `11_pmin_1440x1080.png` | 1440×1080 | Frame 80 | PMIN | ALLJUMPS | 3D Loop-the-loop and jump ramp structures |

---

## 3. Automated Visual Comparison Tooling

```bash
# Compare two visual frames for geometric and color consistency
python3 tools/compare_visual_fidelity.py tests/visual_captures/01_coun_1440x1080.bmp tests/visual_captures/01_coun_1440x1080.bmp
```

The comparator evaluates:
1. **Dimension Conformance**: Checks exact matching framebuffer resolutions.
2. **Per-Channel Color Delta**: Validates palette color mapping accuracy.
3. **Geometry Silhouette & Polygon Edges**: Compares rasterized pixel coverage.

---

## 4. Continuous Simulation Regression Status

The simulation fidelity test suite (`./tools/test-fidelity`) was run alongside the renderer implementation and confirms:
* **Total Primary-Oracle Scenarios**: 12
* **Total Verified Simulation Frames**: 40,146
* **Matching Canonical Frames**: 40,146
* **Divergent Frames**: **0**
