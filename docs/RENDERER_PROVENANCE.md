# Renderer Provenance & Architecture

## 1. Executive Summary

This document specifies the provenance, technical decomposition, mathematical foundations, and implementation details of the native 3D rendering pipeline for the **Stunts 1.1** Apple Silicon macOS port.

In strict adherence to the project charter:
* **The verified simulation is frozen and immutable**.
* The renderer is a pure consumer of immutable simulation state snapshots.
* Rendering never feeds values back into the simulation or alters replay determinism.

---

## 2. Source Provenance & Decomposition

The Stunts rendering pipeline originates from Distinctive Software Inc.'s 1990 real-mode MS-DOS codebase (`seg008.asm`, `seg012.asm`, `seg031.asm`), reverse-engineered in `reference/restunts/` (`shape3d.c`, `shape2d.c`, `data3d.cpp`).

The pipeline decomposes into four distinct architectural layers:

```
+-----------------------------------------------------------------------------+
| 1. Asset & Model Representation (Original Format)                          |
|    - 3D meshes stored in GAME1.P3S (track pieces) & GAME2.P3S (scenery)     |
|    - Vehicle meshes stored in ST<CAR>.P3S (LODs: car0, car1, car2, wheels)  |
|    - VGA 256-color DAC palette stored in SDMAIN.PVS (!pal resource)         |
+-----------------------------------------------------------------------------+
                                       |
                                       v
+-----------------------------------------------------------------------------+
| 2. Geometry Transformation & Projection (Native C99)                        |
|    - 3D World Placement: P_world = P_obj + M_obj * V_local                  |
|    - View Space Transform: P_cam = M_cam * (P_world - P_cam)                |
|    - 3D Perspective Projection: X_screen, Y_screen with 4:3 correction      |
+-----------------------------------------------------------------------------+
                                       |
                                       v
+-----------------------------------------------------------------------------+
| 3. Visibility, Culling & Sorting                                            |
|    - Near-plane depth clipping (Z_cam > 16)                                 |
|    - Polygon Backface Culling: (x1-x0)(y2-y0) - (y1-y0)(x2-x0) > 0          |
|    - Painter's Algorithm Depth Sorting (Z_avg back-to-front queue)          |
+-----------------------------------------------------------------------------+
                                       |
                                       v
+-----------------------------------------------------------------------------+
| 4. Rasterization & Presentation                                             |
|    - Mode A (Original 320x200): Faithful scanline rasterizer, 4:3 pillarbox |
|    - Mode B (Enhanced HD): Direct polygon rasterization at 1080p / Retina   |
|    - Visual Interpolation: Smooth 60/120Hz presentation (alpha in [0, 1])   |
+-----------------------------------------------------------------------------+
```

---

## 3. Mathematical Specifications

### 3.1 3D Perspective Projection
For camera space vertex \(\vec{P}_{\text{cam}} = (X_c, Y_c, Z_c)\) and viewport dimensions \((W, H)\):

\[
X_{\text{screen}} = X_{\text{center}} + \frac{X_c \cdot F}{Z_c}
\]
\[
Y_{\text{screen}} = Y_{\text{center}} - \frac{Y_c \cdot F \cdot 1.2}{Z_c}
\]

where \(F = \frac{W \cdot 256}{320}\) is the dynamic focal length scaled to the viewport width, and \(1.2\) accounts for the non-square pixel aspect ratio of original \(320 \times 200\) VGA mode 13h on 4:3 CRT monitors.

### 3.2 Backface Culling
For projected screen vertices \((x_0, y_0), (x_1, y_1), (x_2, y_2)\):

\[
\text{Cross} = (x_1 - x_0)(y_2 - y_0) - (y_1 - y_0)(x_2 - x_0)
\]

If \(\text{Cross} \le 0\), the polygon is facing away from the camera and is culled prior to rasterization.

### 3.3 Visual Snapshot Interpolation
Between discrete 20 Hz simulation ticks \(N-1\) and \(N\), with fractional progress \(\alpha \in [0.0, 1.0]\):

\[
\vec{P}_{\text{visual}} = \vec{P}_{N-1} + \alpha \cdot (\vec{P}_N - \vec{P}_{N-1})
\]
\[
\theta_{\text{visual}} = \theta_{N-1} + \alpha \cdot \text{ShortestAngularDiff}(\theta_{N-1}, \theta_N)
\]
