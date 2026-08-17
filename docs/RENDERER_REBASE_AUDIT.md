# Source-Level Architecture Rebase & Audit: Stunts 3D Renderer

## 1. Executive Summary

This audit compares the native Phase 3 graphical implementation against the reverse-engineered C reference implementation in `reference/restunts` (`src/restunts/c/frame.c`, `shape3d.c`, `math.c`, `shape3d.h`, and `src/restunts/sdl/`).

The audit reveals that approximately **70% of our custom renderer code duplicated or approximated systems that were already reverse-engineered and translated into C by Restunts**. Rehousing the Restunts C implementations directly into a clean, modern 320×200 software framebuffer pipeline will eliminate discrepancies, restore exact 23-tile cone traversal, enable authentic camera/ground collision, and guarantee faithful geometry.

---

## 2. Detailed Subsystem Comparison (16 Systems)

| Subsystem | Restunts Original-Translated (`reference/restunts/`) | Our Native Implementation (`src/render/`) | Classification | Action Required |
| :--- | :--- | :--- | :---: | :--- |
| **1. Camera State Construction** | `frame.c` `update_frame()`: Cockpit (driver eye level `car_height - 6`), Overview (`game_vec1`), Chase (`custom_camera_distance/elevation/azimuth`), Ground collision clamp (`cam_pos.y < terrainHeight`), and Analytical collision plane avoidance (`plane_origin_op`). | `stunts_camera.c`: Heuristic chase camera with fixed distance and downward pitch. No terrain or plane collision avoidance. | **APPROXIMATION** | **PORT** `frame.c` camera math directly. |
| **2. Projection** | `shape3d.c` `set_projection()` and `math.c` `vector_to_point()`: Integer focal length calculation (`projectiondata9/10`) with $160 \times 100$ screen center. | `stunts_camera.c` `stunts_camera_project_point()`: Scaled to arbitrary resolutions with $1.2$ aspect correction. | **SEMANTIC PORT** | **RETAIN** 320×200 integer core; keep resolution scaling for enhanced mode. |
| **3. Clipping** | `shape3d.c` `select_cliprect_rotate()`: Sets 2D screen clip rectangle `select_rect_rc` and view orientation matrix. Near plane test in `vector_to_point`. | `stunts_camera.c`: Near-plane test ($Z > 16$) and rasterizer 2D bounding box clipping. | **SEMANTIC PORT** | **PORT** Restunts cliprect rotate setup. |
| **4. Visible Tile Selection** | `frame.c`: 8 precomputed lookahead schemas (`lookahead_tiles_tables`) selecting exactly 23 tiles ahead of camera in a heading-oriented cone; rendered farthest to nearest with detail threshold filtering. | `stunts_rasterizer.c`: Iterated over entire 30×30 grid without cone culling. | **NEW DESIGN** | **REPLACE** with Restunts 23-tile lookahead cone tables. |
| **5. Terrain Rendering** | `frame.c`: Heightmap lookup (`td15_terr_map_main`), hill road substitution (`subst_hillroad_track` for tiles 7..10), vertex height offsets. | `stunts_rasterizer.c`: Read raw `track_heights` array without hill road substitution. | **APPROXIMATION** | **PORT** `subst_hillroad_track` and hill slope vertex offsets. |
| **6. Track Element Selection** | `frame.c`: Element map lookup (`td14_elem_map_main`), indexed shape pointer lookup in `game3dshapes` (130 shape table loaded from `GAME1.P3S` and `GAME2.P3S`). | `stunts_rasterizer.c`: Hardcoded switch statement mapping element IDs to shape string tags. | **SEMANTIC PORT** | **REPLACE** with indexed `game3dshapes` table. |
| **7. Multi-Tile Element Handling** | `frame.c`: Handled multi-tile filler codes `0xFD` (SE), `0xFE` (S), `0xFF` (E) and marked `should_skip_tile[si] = 1` to prevent duplicate rendering. | `stunts_rasterizer.c`: Multi-tile filler codes were unhandled. | **MISSING** | **PORT** multi-tile coordinate redirection from `frame.c`. |
| **8. Scenery Placement** | `frame.c`: `sceneshapes2` and `sceneshapes3` lists from track header; 8 horizon background shapes when `detail_level == 0`. | `stunts_rasterizer.c`: Scenery scanned from tile grid. | **APPROXIMATION** | **PORT** track scenery lists from `frame.c`. |
| **9. Car Placement** | `frame.c`: Player & opponent body models from `ST<CAR>.P3S`, paint job material index, brake light color overrides (`backlights_paint_override`). | `stunts_rasterizer.c`: Loaded `car0` mesh, rendered at `pos_world` and `rotate`. | **SEMANTIC PORT** | **PORT** material selection and brake light override. |
| **10. Wheel Placement** | `frame.c`: 4 wheel shapes (`whfl`, `whrl`, `whfr`, `whrr`) positioned using `simd_player.wheel_coords[w]` and front steering angle. | `stunts_rasterizer.c`: Transformed 4 wheels with steer angle. | **IDENTICAL** | **RETAIN**. |
| **11. Shape3D Parsing** | `shape3d.c` `shape3d_init_shape()`: Header (`numverts`, `numprims`, `numpaints`), `shape3d_verts = +4`, `cull1 = +numverts*6+4`, `cull2 = +numprims*4+...`, `primitives = +numprims*8+...`. | `stunts_shape3d.c`: Parsed 3D shape blocks from archive chunks. | **SEMANTIC PORT** | **RETAIN** (Verified offset: `numprims * 8`). |
| **12. Primitive Parsing** | `shape3d.c`: Decodes primitive type ($0..15$), flags, paint color table, vertex index list. | `stunts_shape3d.c`: Decodes primitive type, paint table, and vertex indices. | **IDENTICAL** | **RETAIN**. |
| **13. `transformed_shape_op`** | `shape3d.c` / `game2.asm`: Matrix rotation of vertices, world-to-camera translation, bounding box frustum culling, insertion into depth-sorted poly linked list. | `stunts_rasterizer.c` `render_shape_transformed()`: Transforms vertices and appends to flat array `s_poly_queue`. | **APPROXIMATION** | **REPLACE** with Restunts transformed shape pipeline. |
| **14. Backface Tests** | `shape3d.c` `is_facing_camera()`: `(dx1 * dy0) - (dx0 * dy1) > 0`. | `stunts_rasterizer.c`: Implemented exact `(dx1 * dy0) - (dx0 * dy1) > 0` formula. | **IDENTICAL** | **RETAIN**. |
| **15. Polygon Ordering** | `shape3d.c` / `frame.c`: Tile cone draw order (Painter's coarse order) + depth linked list `poly_linked_list_40ED6` + `heapsort_by_order()`. | `stunts_rasterizer.c`: Global `qsort()` on average polygon Z. | **APPROXIMATION** | **PORT** Restunts polygon linked list and sorting. |
| **16. Palette / Material Selection** | `shape3d.c` / `frame.c`: 256-color VGA DAC palette from `SDMAIN.PVS` (`!pal`), car paint job indexing `color = prim_paint_table[material]`. | `stunts_palette.c`: Loaded 256-color palette from `SDMAIN.PVS`, default material index 0. | **SEMANTIC PORT** | **PORT** multi-paint material indexing. |

---

## 3. Analysis of the Old Restunts SDL Prototype (`src/restunts/sdl/`)

Inspection of `reference/restunts/src/restunts/sdl/main.cpp` and `data3d.cpp`:
* **What Worked**: Proved that `frame.c`, `shape3d.c`, and `math.c` can compile in a modern 32-bit/64-bit environment when DOS memory manager (`mmgr`) calls are stubbed with standard `malloc()`/heap buffers.
* **Limitations**: The prototype relied on partial DOS segment thunks, global variables (`g_pVertices`, `g_pwTrnsfrmTab`), and incomplete 2D blitters.
* **Port Strategy**: We can port `frame.c`, `shape3d.c`, and `math.c` cleanly into pure C99 with modern 320×200 software framebuffer output, discarding all DOS segment/16-bit far pointer artifacts while retaining 100% of the mathematical algorithms.
