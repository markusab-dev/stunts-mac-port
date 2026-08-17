/*
 * map2d_shot - draw the editor's 2D track map for one track and write a BMP.
 *
 * Phase 11 test harness.  It exists because tools/build_native.sh may not be
 * edited from this worktree, and the map has to be looked at, not merely
 * compiled.  It does the smallest init draw_2DtrackMap needs:
 *
 *   - the resource loader's data directory
 *   - rblit_init() for the framebuffer and sprite1
 *   - init_row_tables' two row tables (restunts.c:236)
 *   - td14_elem_map_main / td15_terr_map_main pointed at the .TRK's two
 *     900-byte halves
 *
 * and then calls trackmap2d_shot(), which is exactly what the host will call
 * behind STUNTS_MAP2D_SHOT.
 *
 *   ./bin/map2d_shot <data_dir> <TRACK> <out.bmp> [col] [row]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/render_faithful/externs.h"
#include "../src/asset/stunts_asset_loader.h"

extern void rfileio_set_data_dir(const char* dir);
extern void rblit_init(void);
extern int  trackmap2d_shot(const char* path);
extern int16_t video_flag1_is1;

static uint8_t s_elem[900], s_terr[900];

int main(int argc, char** argv)
{
	char p[600];
	int i;

	if (argc < 4) {
		fprintf(stderr, "usage: %s <data_dir> <TRACK> <out.bmp> [col] [row]\n",
		        argv[0]);
		return 2;
	}
	if (argc > 4) setenv("STUNTS_MAP2D_COL", argv[4], 1);
	if (argc > 5) setenv("STUNTS_MAP2D_ROW", argv[5], 1);

	rfileio_set_data_dir(argv[1]);
	rblit_init();
	video_flag1_is1 = 1;

	for (i = 0; i < 30; i++) {                 /* init_row_tables */
		trackrows[i]   = (int16_t)(30 * (29 - i));
		terrainrows[i] = (int16_t)(30 * i);
	}

	snprintf(p, sizeof p, "%s/%s.TRK", argv[1], argv[2]);
	if (!stunts_load_track(p, s_elem, s_terr)) {
		fprintf(stderr, "kan inte ladda banan %s\n", p);
		return 1;
	}
	td14_elem_map_main = s_elem;
	td15_terr_map_main = s_terr;

	i = trackmap2d_shot(argv[3]);
	if (i < 0) { fprintf(stderr, "kartan kunde inte ritas\n"); return 1; }
	return 0;
}
