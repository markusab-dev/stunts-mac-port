/*
 * icons_shot - drive src/render_faithful/reditoricons.c on its own.
 *
 * Phase 11's verification harness.  main_native.c is off limits until the
 * integration step, so the icon palette gets its own entry point: this loads
 * the palette and the data directory, calls editor_icons_shot(), and also
 * exercises sub_2C81C()/sub_2C9B4() over every shipped .TRK so the two map
 * helpers are run against real data rather than only compiled.
 *
 *   bash tools/build_icons_shot.sh
 *   STUNTS_ICONS_SHOT=build/icons ./bin/icons_shot <datadir>
 *
 * Exit status 0 only if every page of the palette had pixels in it and every
 * track survived the repair pass unchanged (a shipped track is by definition
 * already legal, so any repair is a bug in the port).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "externs.h"
#include "../src/render_faithful/reditoricons.h"
#include "../src/render/stunts_palette.h"
#include "../src/asset/stunts_asset_loader.h"

extern void rfileio_set_data_dir(const char* dir);
extern void rblit_init(void);
extern struct TRACKOBJECT trkObjectList[215];
extern uint8_t far* td14_elem_map_main;
extern uint8_t far* td15_terr_map_main;
extern int16_t trackrows[];
extern int16_t terrainrows[];
extern int16_t video_flag1_is1;

/* The renderer's element/terrain maps, laid out as the original's track block
 * is: element grid, one spare byte, terrain grid (sfdata.c:211). */
static uint8_t s_track[901 + 900];

int main(int argc, char** argv)
{
	const char* data = argc > 1 ? argv[1]
	                            : (getenv("STUNTS_DATA")
	                               ? getenv("STUNTS_DATA")
	                               : "extracted/stunts/stunts");
	stunts_palette_t pal;
	char path[1024];
	int i, rc, ntrack = 0, repaired = 0;
	DIR* d;
	struct dirent* de;

	rfileio_set_data_dir(data);
	rblit_init();
	video_flag1_is1 = 1;

	snprintf(path, sizeof path, "%s/SDMAIN.PVS", data);
	if (!stunts_palette_load(path, &pal)) {
		fprintf(stderr, "kan inte lasa palett fran %s, anvander standard\n", path);
		stunts_palette_init_default(&pal);
	}

	for (i = 0; i < 30; i++) {
		trackrows[i] = (int16_t)(30 * (29 - i));
		terrainrows[i] = (int16_t)(30 * i);
	}
	td14_elem_map_main = s_track;
	td15_terr_map_main = s_track + 901;

	/* ---- the icon palette ---------------------------------------- */
	rc = editor_icons_shot(&pal);
	if (rc == 0) {
		fprintf(stderr, "STUNTS_ICONS_SHOT ar inte satt\n");
		return 2;
	}
	if (rc < 0) {
		fprintf(stderr, "ikonpaletten misslyckades\n");
		return 1;
	}

	/* ---- sub_2C81C over every shipped track ---------------------- */
	d = opendir(data);
	if (!d) { perror(data); return 1; }
	while ((de = readdir(d)) != NULL) {
		size_t n = strlen(de->d_name);
		uint8_t elem[900], terr[900];
		int16_t code;
		if (n < 5 || strcmp(de->d_name + n - 4, ".TRK") != 0) continue;
		snprintf(path, sizeof path, "%s/%s", data, de->d_name);
		if (!stunts_load_track(path, elem, terr)) continue;
		memset(s_track, 0, sizeof s_track);
		memcpy(s_track, elem, 900);
		memcpy(s_track + 901, terr, 900);
		code = sub_2C81C();
		ntrack++;
		if (code != 0 || memcmp(s_track, elem, 900) != 0) {
			int changed = 0, k;
			for (k = 0; k < 900; k++) if (s_track[k] != elem[k]) changed++;
			printf("  %-14s kod %d, %d rutor andrade\n",
			       de->d_name, code, changed);
			repaired++;
		}
	}
	closedir(d);
	printf("sub_2C81C: %d banor, %d andrade\n", ntrack, repaired);

	/* ---- and the same routine on deliberately broken maps ---------
	 * "39 shipped tracks come through unchanged" only shows the pass does
	 * no harm.  These nine cases show it does the work: each one names the
	 * branch of seg009:4007 it exercises and the outcome that branch must
	 * produce. */
	{
		static const struct {
			const char* what;
			int row, col;
			uint8_t terr, elem;
			int16_t want_code;     /* var_A on return */
			uint8_t want_elem;     /* the cell afterwards */
		} cases[] = {
	{ "terrang 0x0B, omojlig      -> 0x0E", 5,  5, 0x0B, 0x01, 0x0E, 0 },
	{ "terrang 1, element utanfor -> 0x0C", 7,  7, 0x01, 0x01, 0x0C, 0 },
	{ "terrang 1, element 0x22    -> ok  ", 9,  9, 0x01, 0x22, 0x00, 0x22 },
	{ "terrang 7, subst finns     -> ok  ", 11, 11, 0x07, 0x04, 0x00, 0x04 },
	{ "terrang 7, ingen subst     -> 0x0D", 13, 13, 0x07, 0x01, 0x0D, 0 },
	{ "terrang 6 slapper igenom   -> ok  ", 21, 21, 0x06, 0x01, 0x00, 0x01 },
	{ "terrang 0 slapper igenom   -> ok  ", 23, 23, 0x00, 0x01, 0x00, 0x01 },
		};
		int k, fails = 0;
		for (k = 0; k < (int)(sizeof cases / sizeof cases[0]); k++) {
			int ei = 30 * (29 - cases[k].row) + cases[k].col;
			int ti = 30 * cases[k].row + cases[k].col;
			int16_t code;
			memset(s_track, 0, sizeof s_track);
			s_track[ei] = cases[k].elem;
			s_track[901 + ti] = cases[k].terr;
			code = sub_2C81C();
			if (code != cases[k].want_code || s_track[ei] != cases[k].want_elem) {
				printf("  FEL  %s: kod %d (vantat %d), ruta %u (vantat %u)\n",
				       cases[k].what, code, cases[k].want_code,
				       s_track[ei], cases[k].want_elem);
				fails++;
			} else {
				printf("  ok   %s\n", cases[k].what);
			}
		}

		/* An orphaned continuation marker: sub_2C9B4 deletes it silently,
		 * so sub_2C81C still returns 0. */
		{
			int ei = 30 * (29 - 15) + 15;
			int16_t code;
			memset(s_track, 0, sizeof s_track);
			s_track[ei] = 0xFF;
			code = sub_2C81C();
			if (code != 0 || s_track[ei] != 0) {
				printf("  FEL  foraldralos 0xFF: kod %d, ruta %u\n",
				       code, s_track[ei]);
				fails++;
			} else {
				printf("  ok   foraldralos 0xFF tas bort\n");
			}
		}

		/* A complete 2x2 element survives; the same one with its 0xFD
		 * corner missing loses all four cells. */
		{
			int a  = 30 * (29 - 17) + 17;   /* the anchor          */
			int rt = a + 1;                 /* one to the right    */
			int bl = 30 * (29 - 18) + 17;   /* one row "below"     */
			int br = bl + 1;
			int16_t code;
			if ((int8_t)trkObjectList[10].ss_multiTileFlag != 3) {
				printf("  FEL  element 10 har inte multiTileFlag 3\n");
				fails++;
			}
			memset(s_track, 0, sizeof s_track);
			s_track[a] = 10; s_track[rt] = 0xFF;
			s_track[bl] = 0xFE; s_track[br] = 0xFD;
			code = sub_2C81C();
			if (code != 0 || s_track[a] != 10 || s_track[rt] != 0xFF ||
			    s_track[bl] != 0xFE || s_track[br] != 0xFD) {
				printf("  FEL  hel 2x2: kod %d, %u/%u/%u/%u\n", code,
				       s_track[a], s_track[rt], s_track[bl], s_track[br]);
				fails++;
			} else {
				printf("  ok   hel 2x2 overlever\n");
			}

			memset(s_track, 0, sizeof s_track);
			s_track[a] = 10; s_track[rt] = 0xFF;
			s_track[bl] = 0xFE;            /* the 0xFD corner is missing */
			code = sub_2C81C();
			if (s_track[a] || s_track[rt] || s_track[bl]) {
				printf("  FEL  trasig 2x2: kod %d, %u/%u/%u\n", code,
				       s_track[a], s_track[rt], s_track[bl]);
				fails++;
			} else {
				printf("  ok   trasig 2x2 rensas helt\n");
			}
		}

		if (fails) { repaired += fails; }
	}

	editor_free_icon_shapes();
	if (ntrack == 0) { fprintf(stderr, "inga banor hittades\n"); return 1; }
	return repaired ? 1 : 0;
}
