/* rfbsize.h - one place that knows the render resolution.
 *
 * RFB_SCALE 1 is the original 320x200 and the ONLY configuration verified
 * against the DOS oracle. Higher values re-project the same 3D geometry onto a
 * larger grid, so edges are genuinely recomputed rather than magnified; they
 * are a display option, not a fidelity claim.
 *
 * RFB_SPANROWS is the per-scanline edge buffer in shape3d.c. The original
 * sizes it 480 for a 200-row screen - a 2.4x margin, because a polygon is
 * walked before it is clipped and can span far more rows than the screen has.
 * It therefore has to scale with RFB_SCALE, keeping that margin. Sizing it to
 * the screen height alone leaves no headroom and smashes the stack. */
#ifndef RESTUNTS_RFBSIZE_H
#define RESTUNTS_RFBSIZE_H
#ifndef RFB_SCALE
#define RFB_SCALE 1
#endif
#define RFB_VIEW_W (320 * RFB_SCALE)
#define RFB_VIEW_H (200 * RFB_SCALE)
#define RFB_SPANROWS (480 * RFB_SCALE)
#endif
