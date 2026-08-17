/*
 * dump_editor_tables - print the four tables the track editor's layout comes
 * out of, so nothing about the editor's screen has to be invented.
 * House rule 2: read the data before writing layout code.
 *
 *   pbox  - 11 pages x 6 rows x 6 columns of element codes (0xFF/0xFE are
 *           "this cell is the continuation of the one left/above")
 *   snam  - 186 four-character shape names, the icon FILLINGS
 *   mnam  - 186 four-character shape names, the icon MASKS
 *   tnam  - three-character text keys, one per element, for the name line
 *
 *   cc -o bin/dump_editor_tables tools/dump_editor_tables.c \
 *      src/asset/stunts_asset_loader.c src/asset/stunts_dsi_unpack.c
 *   ./bin/dump_editor_tables extracted/stunts/stunts/TEDIT.PRE
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../src/asset/stunts_asset_loader.h"

static const stunts_sub_resource_t* get(stunts_res_archive_t* a, const char* t)
{
	unsigned i;
	for (i = 0; i < a->num_resources; i++)
		if (!strncmp(a->resources[i].tag, t, 4)) return &a->resources[i];
	return NULL;
}

int main(int argc, char** argv)
{
	stunts_res_archive_t* a;
	const stunts_sub_resource_t* r;
	int page, row, col, i;

	if (argc < 2) { fprintf(stderr, "usage: dump_editor_tables <TEDIT.PRE>\n"); return 2; }
	a = stunts_asset_load_archive(argv[1]);
	if (!a) { fprintf(stderr, "kan inte lasa %s\n", argv[1]); return 1; }

	r = get(a, "pbox");
	if (r) {
		printf("pbox: %u byte = %u sidor a 36\n", r->size, r->size / 36);
		for (page = 0; page * 36 + 35 < (int)r->size; page++) {
			printf("  sida %d:\n", page);
			for (row = 0; row < 6; row++) {
				printf("   ");
				for (col = 0; col < 6; col++)
					printf(" %3u", r->data[page * 36 + row * 6 + col]);
				printf("\n");
			}
		}
	}
	r = get(a, "snam");
	if (r) {
		printf("snam: %u byte\n", r->size);
		for (i = 0; i < 186; i++)
			printf("  %3d %.4s %.4s\n", i, r->data + i * 4,
			       get(a, "mnam") ? get(a, "mnam")->data + i * 4
			                      : (const unsigned char*)"????");
	}
	r = get(a, "tnam");
	if (r) {
		const stunts_sub_resource_t* pb = get(a, "pbox");
		const stunts_sub_resource_t* sn = get(a, "snam");
		printf("tnam: %u byte\n", r->size);
		/* Every palette cell with the shape name it draws and the name
		 * the bottom line shows - the check that the three tables agree. */
		for (page = 0; pb && page < 11; page++) {
			printf("  sida %d:\n", page);
			for (row = 0; row < 6; row++)
				for (col = 0; col < 6; col++) {
					int c = pb->data[page * 36 + row * 6 + col];
					const stunts_sub_resource_t* t;
					char key[5];
					if (c >= 0xFD) continue;
					key[0] = 'e';
					key[1] = (char)r->data[c * 3 + 0];
					key[2] = (char)r->data[c * 3 + 1];
					key[3] = (char)r->data[c * 3 + 2];
					key[4] = 0;
					t = get(a, key);
					printf("   (%d,%d) kod %3d  bild %.4s  text %-5s \"%s\"\n",
					       row, col, c, sn ? sn->data + c * 4
					                       : (const unsigned char*)"????",
					       key, t ? (const char*)t->data : "-");
				}
		}
	}
	stunts_asset_free_archive(a);
	return 0;
}
