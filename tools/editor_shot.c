/*
 * editor_shot.c - a standalone driver for the Phase 11 track editor
 * (src/render_faithful/reditor.c and rdialog.c), so the screen can be
 * built, run and screenshotted without src/main_native.c being involved.
 *
 * It reproduces only the part of main_native.c's start-up the editor
 * depends on: the data directory, the palette, the font, the trakdata
 * block and one .TRK read straight into td14_elem_map_main.
 *
 *   STUNTS_EDITOR_SHOT=/tmp/ed.bmp bin/editor_shot --data <dir> [--track DEFAULT]
 *   STUNTS_DIALOG_SHOT=/tmp/dlg.bmp bin/editor_shot --data <dir> --dialog mss
 *   bin/editor_shot --data <dir> --selftest
 *
 * --selftest exercises the editor's model without drawing: place an
 * element, place terrain, write the track out, read it back and compare.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_faithful/externs.h"
#include "render_faithful/rdialog.h"
#include "render_faithful/reditor.h"
#include "render_faithful/rfbsize.h"
#include "render/stunts_palette.h"

extern void rfileio_set_data_dir(const char* dir);
extern void rblit_init(void);
extern void sfdata_init_trackdata(void);
extern void sfdata_init_track_tables(void);
extern void font_set_fontdef2(void far* data);
extern void far* fontdefptr;
extern void far* fontnptr;
extern uint8_t rfb_pixels[];
extern uint32_t* rs_rgba;
extern const uint32_t* rs_pal;
extern uint8_t far* td14_elem_map_main;
extern uint8_t far* td15_terr_map_main;
extern int16_t trackrows[];
extern int16_t terrainrows[];
extern int16_t trackpos[], trackpos2[], trackcenterpos[], trackcenterpos2[];
extern int16_t terrainpos[], terraincenterpos[];

static void* load_raw(const char* dir, const char* name)
{
	char p[700];
	FILE* f;
	long n;
	uint8_t* b;
	snprintf(p, sizeof p, "%s/%s", dir, name);
	f = fopen(p, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	b = (uint8_t*)malloc((size_t)n);
	if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
	fclose(f);
	return b;
}

static uint32_t pal_rgba[256];
static stunts_palette_t pal;

static void write_bmp(const char* path)
{
	uint32_t rowb = (uint32_t)RFB_VIEW_W * 3;
	uint32_t pad = (4 - (rowb % 4)) % 4;
	uint32_t stride = rowb + pad;
	uint32_t img = stride * (uint32_t)RFB_VIEW_H;
	uint8_t hdr[54];
	int32_t w = RFB_VIEW_W, h = RFB_VIEW_H;
	uint32_t fsz = 54 + img;
	uint8_t* buf = (uint8_t*)calloc(1, img);
	FILE* f;
	int y, x;
	if (!buf) return;
	memset(hdr, 0, sizeof hdr);
	for (y = 0; y < RFB_VIEW_H; y++) {
		uint8_t* row = buf + (uint32_t)(RFB_VIEW_H - 1 - y) * stride;
		for (x = 0; x < RFB_VIEW_W; x++) {
			stunts_color_rgba_t c =
				pal.colors[rfb_pixels[(int32_t)y * RFB_VIEW_W + x]];
			row[x * 3 + 0] = c.b;
			row[x * 3 + 1] = c.g;
			row[x * 3 + 2] = c.r;
		}
	}
	hdr[0] = 'B'; hdr[1] = 'M';
	memcpy(hdr + 2, &fsz, 4); hdr[10] = 54; hdr[14] = 40;
	memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
	hdr[26] = 1; hdr[28] = 24; memcpy(hdr + 34, &img, 4);
	f = fopen(path, "wb");
	if (f) { fwrite(hdr, 1, 54, f); fwrite(buf, 1, img, f); fclose(f); }
	free(buf);
	printf("skrev %s\n", path);
}

int main(int argc, char** argv)
{
	const char* data_dir = "extracted/stunts/stunts";
	const char* track = "DEFAULT";
	const char* dialog = NULL;
	int selftest = 0, i;
	int cur_col = -1, cur_row = -1, page = -1, focus = -1, check = 0, blink = 1;
	int elem = -1, hook = 0;
	static struct REDITOR ed;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--data") && i + 1 < argc) data_dir = argv[++i];
		else if (!strcmp(argv[i], "--track") && i + 1 < argc) track = argv[++i];
		else if (!strcmp(argv[i], "--dialog") && i + 1 < argc) dialog = argv[++i];
		else if (!strcmp(argv[i], "--selftest")) selftest = 1;
		else if (!strcmp(argv[i], "--check")) check = 1;
		else if (!strcmp(argv[i], "--blink") && i + 1 < argc) blink = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--elem") && i + 1 < argc) elem = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--hook")) hook = 1;
		else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--focus") && i + 1 < argc) focus = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--cursor") && i + 2 < argc) {
			cur_col = atoi(argv[++i]);
			cur_row = atoi(argv[++i]);
		}
	}

	rfileio_set_data_dir(data_dir);
	rblit_init();

	{
		char p[700];
		snprintf(p, sizeof p, "%s/SDMAIN.PVS", data_dir);
		if (!stunts_palette_load(p, &pal)) stunts_palette_init_default(&pal);
	}
	for (i = 0; i < 256; i++)
		pal_rgba[i] = 0xFF000000u | ((uint32_t)pal.colors[i].r << 16)
		            | ((uint32_t)pal.colors[i].g << 8) | pal.colors[i].b;
	rs_pal = pal_rgba;
	rs_rgba = NULL;

	fontdefptr = load_raw(data_dir, "FONTDEF.FNT");
	fontnptr   = load_raw(data_dir, "FONTN.FNT");
	if (!fontdefptr) fontdefptr = fontnptr;
	font_set_fontdef2(fontdefptr);

	for (i = 0; i < 30; i++) {                     /* init_row_tables */
		trackrows[i] = (int16_t)(30 * (29 - i));
		terrainrows[i] = (int16_t)(30 * i);
		trackpos[i] = (int16_t)((29 - i) << 10);
		trackpos2[i] = (int16_t)(i << 10);
		trackcenterpos[i] = (int16_t)(((29 - i) << 10) + 0x200);
		trackcenterpos2[i] = (int16_t)((i << 10) + 0x200);
		terrainpos[i] = (int16_t)(i << 10);
		terraincenterpos[i] = (int16_t)((i << 10) + 0x200);
	}
	sfdata_init_track_tables();
	sfdata_init_trackdata();

	{
		char p[700];
		FILE* f;
		snprintf(p, sizeof p, "%s/%s.TRK", data_dir, track);
		f = fopen(p, "rb");
		if (!f) { fprintf(stderr, "kan inte oppna %s\n", p); return 1; }
		if (fread(td14_elem_map_main, 1, 1802, f) != 1802)
			fprintf(stderr, "%s ar kortare an 1802 byte\n", p);
		fclose(f);
	}

	if (hook) {
		/* the entry point the host calls - reditor.c reads
		 * STUNTS_EDITOR_SHOT itself and opens/closes its own editor. */
		int r = reditor_shot(pal.colors);
		printf("reditor_shot -> %d\n", r);
		return r ? 0 : 1;
	}

	if (!reditor_open(&ed)) {
		fprintf(stderr, "reditor_open misslyckades\n");
		return 1;
	}
	if (page >= 0) ed.var_C6 = (uint8_t)page;
	if (elem >= 0) ed.var_190 = (uint8_t)elem;
	if (focus >= 0) ed.var_34 = (uint8_t)focus;
	if (cur_col >= 0) {
		if (ed.var_34) { ed.var_18D = (uint8_t)cur_col;
		                 ed.var_17F = (uint8_t)cur_row; }
		else           { ed.var_18E = (uint8_t)cur_col;
		                 ed.var_180 = (uint8_t)cur_row; }
	}
	if (check) {
		int16_t v = reditor_check(&ed);
		printf("track_setup -> %d (%s) \"%s\"\n", v, reditor_verdict_key(v),
		       reditor_text(&ed, reditor_verdict_key(v)));
	}

	if (selftest) {
		/* --- what the model has to get right, checked with numbers --- */
		int fails = 0;
		uint8_t before[1802], after[1802];
		const char* tmp = "/tmp/stunts_editor_selftest.trk";

		memcpy(before, td14_elem_map_main, 1802);
		printf("landskapsbyte td14[0x384] = %u\n", td14_elem_map_main[0x384]);

		/* the 1802-byte round trip */
		if (!reditor_save_track(&ed, tmp)) { printf("FEL: spara\n"); fails++; }
		memset(td14_elem_map_main, 0xAA, 1802);
		if (!reditor_load_track(&ed, tmp)) { printf("FEL: ladda\n"); fails++; }
		memcpy(after, td14_elem_map_main, 1802);
		if (memcmp(before, after, 1802) != 0) {
			int n = 0, k;
			for (k = 0; k < 1802; k++) if (before[k] != after[k]) n++;
			printf("FEL: rundturen skiljer i %d byte\n", n);
			fails++;
		} else {
			printf("ok  1802-byte rundtur identisk\n");
		}

		/* place a two-by-two element and check the three continuations */
		ed.var_C6 = 6;
		ed.var_190 = 91;                 /* pbox page 6 row 0 column 2 */
		ed.var_34 = 0;
		ed.var_18E = 5;
		ed.var_180 = 5;
		reditor_update(&ed);
		reditor_activate(&ed);
		{
			extern struct TRACKOBJECT trkObjectList[215];
			int flag = trkObjectList[91].ss_multiTileFlag;
			uint8_t a = td14_elem_map_main[trackrows[5] + 5];
			uint8_t b = td14_elem_map_main[trackrows[5] + 6];
			uint8_t c = td14_elem_map_main[trackrows[6] + 5];
			uint8_t d = td14_elem_map_main[trackrows[6] + 6];
			printf("element 91 flagga=%d -> [%u %u / %u %u]\n",
			       flag, a, b, c, d);
			if (a != 91) { printf("FEL: elementet hamnade inte\n"); fails++; }
			if (flag == 3 && (b != 0xFF || c != 0xFE || d != 0xFD)) {
				printf("FEL: fortsattningsbyte fel\n"); fails++;
			}
		}

		/* terrain, page 0 */
		ed.var_C6 = 0;
		ed.var_190 = 6;                  /* "high" */
		ed.var_18E = 10;
		ed.var_180 = 10;
		ed.var_DA = 0xFF;
		reditor_update(&ed);
		reditor_activate(&ed);
		if (td15_terr_map_main[terrainrows[10] + 10] != 6) {
			printf("FEL: terrangen hamnade inte\n"); fails++;
		} else {
			printf("ok  terrang 6 pa (10,10)\n");
		}

		/* the landscape byte */
		reditor_set_horizon(&ed, 3);
		if (td14_elem_map_main[0x384] != 3) {
			printf("FEL: landskapsbyten\n"); fails++;
		} else {
			printf("ok  landskapsbyte satt till 3\n");
		}

		/* a new track from preset 2 */
		reditor_new_track(&ed, 2);
		{
			int n = 0, k;
			for (k = 0; k < 900; k++) if (td14_elem_map_main[k]) n++;
			printf("ny bana: %d elementbyte kvar (ska vara 0)\n", n);
			if (n) fails++;
			n = 0;
			for (k = 0; k < 900; k++) if (td15_terr_map_main[k]) n++;
			printf("ny bana: %d terrangbyte satta\n", n);
		}

		/* the verdict table */
		for (i = 0; i < 15; i++)
			printf("  utfall %2d %-3s \"%s\"\n", i, reditor_verdict_key(i),
			       reditor_text(&ed, reditor_verdict_key(i)));

		printf(selftest && fails == 0 ? "SJALVTEST OK\n"
		                              : "SJALVTEST MISSLYCKADES\n");
		reditor_close(&ed);
		return fails == 0 ? 0 : 1;
	}

	if (dialog) {
		struct RDIALOG d;
		const char* text = reditor_text(&ed, dialog);
		const char* shot = getenv("STUNTS_DIALOG_SHOT");
		printf("dialogtext \"%s\" = \"%s\"\n", dialog, text);
		ed.blink = 1;
		reditor_update(&ed);
		reditor_draw(&ed);
		rdialog_open(&d, 2, 1, text, -1, -1, 4, NULL, 0);
		printf("  ruta (%d,%d)-(%d,%d), %d knappar\n",
		       d.var_30, d.var_2C, d.var_2E, d.var_2A, d.var_140);
		for (i = 0; i < d.var_140; i++)
			printf("  knapp %d: x %d..%d y %d..%d \"%.*s\"\n", i,
			       d.var_28[i], d.var_C6[i], d.var_1BE[i], d.var_EE[i],
			       d.var_98[i], d.var_13E[i]);
		rdialog_draw(&d);
		if (shot) write_bmp(shot);
		reditor_close(&ed);
		return 0;
	}

	{
		const char* shot = getenv("STUNTS_EDITOR_SHOT");
		ed.blink = (int16_t)blink;
		reditor_update(&ed);
		reditor_draw(&ed);
		if (shot) write_bmp(shot);
		else printf("satt STUNTS_EDITOR_SHOT=<fil> for en bild\n");
	}
	reditor_close(&ed);
	return 0;
}
