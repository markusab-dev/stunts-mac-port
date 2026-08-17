/*
 * dump_shape2d - print the SHAPE2D header of every sub-resource in an archive.
 *
 * The layout of this game lives in its data, not in its code, so being able to
 * read a shape's own width/height/anchor/position without running the game is
 * worth a small tool. Written while chasing the gear knob sitting outside its
 * gate: the answer was in gbox's and gnob's headers.
 *
 *   cc -o bin/dump_shape2d tools/dump_shape2d.c src/asset/stunts_asset_loader.c \
 *      src/asset/stunts_dsi_unpack.c
 *   ./bin/dump_shape2d extracted/stunts/stunts/STDBCOUN.PVS
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/asset/stunts_asset_loader.h"

int main(int argc, char **argv)
{
	int a;
	if (argc < 2) {
		fprintf(stderr, "usage: dump_shape2d <archive.pvs|.res|.p3s> ...\n");
		return 2;
	}
	for (a = 1; a < argc; a++) {
		stunts_res_archive_t *arc = stunts_asset_load_archive(argv[a]);
		if (!arc) {
			fprintf(stderr, "kan inte lasa %s\n", argv[a]);
			continue;
		}
		printf("%s  (%u resurser, %u byte)\n",
		       argv[a], arc->num_resources, arc->total_size);
		printf("  tag    bredd  hojd  ankare      position    "
		       "unk3 unk4 unk5 unk6  flip  byte\n");
		for (uint16_t i = 0; i < arc->num_resources; i++) {
			const stunts_sub_resource_t *r = &arc->resources[i];
			const uint8_t *d = r->data;
			uint16_t f[6];
			int flip;
			if (!d || r->size < 16) {
				printf("  %-5s  (for liten: %u byte)\n", r->tag, r->size);
				continue;
			}
			for (int k = 0; k < 6; k++)
				f[k] = (uint16_t)d[k * 2] | ((uint16_t)d[k * 2 + 1] << 8);
			/* the transpose flag, same test unflip_shape uses */
			flip = ((d[15] & 0xF0) == 0) ? (d[14] >> 4) : 0;
			printf("  %-5s  %5u %5u  (%4u,%4u)  (%4u,%4u)  "
			       "%4u %4u %4u %4u  %4d  %6u\n",
			       r->tag, f[0], f[1], f[2], f[3], f[4], f[5],
			       d[12], d[13], d[14], d[15], flip, r->size);
		}
		stunts_asset_free_archive(arc);
	}
	return 0;
}
