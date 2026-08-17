/*
 * dump_textres - print every text sub-resource of an archive as printable
 * characters, so a dialog's own layout (its ']' line breaks, '}' half-line
 * breaks and '{' button markers) can be read before any layout code is
 * written.  House rule 2.
 *
 *   cc -o bin/dump_textres tools/dump_textres.c src/asset/stunts_asset_loader.c \
 *      src/asset/stunts_dsi_unpack.c
 *   ./bin/dump_textres extracted/stunts/stunts/TEDIT.PRE e
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../src/asset/stunts_asset_loader.h"

int main(int argc, char** argv)
{
	stunts_res_archive_t* a;
	unsigned i;
	if (argc < 2) {
		fprintf(stderr, "usage: dump_textres <archive> [tagprefix]\n");
		return 2;
	}
	a = stunts_asset_load_archive(argv[1]);
	if (!a) { fprintf(stderr, "kan inte lasa %s\n", argv[1]); return 1; }
	for (i = 0; i < a->num_resources; i++) {
		const stunts_sub_resource_t* r = &a->resources[i];
		const unsigned char* d = r->data;
		int k, n = 0;
		if (argc > 2 && strncmp(r->tag, argv[2], strlen(argv[2]))) continue;
		if (!d) continue;
		for (k = 0; k < 2000; k++) if (!d[k]) { n = k; break; }
		printf("=== %-5s (%d tecken) ===\n", r->tag, n);
		for (k = 0; k < n; k++)
			putchar(isprint(d[k]) ? d[k] : '.');
		putchar('\n');
	}
	stunts_asset_free_archive(a);
	return 0;
}
