/*
 * rfileio.c - Native adapter between the vendored restunts renderer's DOS
 * file/memory API and this project's verified asset loader
 * (src/asset/stunts_asset_loader.c).
 *
 * The original fileio.c/memmgr.c rely on 16-bit segment arithmetic and an
 * in-place decompression trick; none of that is needed in flat memory.
 * The project's loader already decompresses DSI archives bit-exactly
 * (verified in Phase 2), so resource loading routes through it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "../asset/stunts_asset_loader.h"

/* ------------------------------------------------------------------ */
/* Data directory                                                      */
/* ------------------------------------------------------------------ */
static char s_data_dir[512] = ".";

void rfileio_set_data_dir(const char* dir)
{
	snprintf(s_data_dir, sizeof(s_data_dir), "%s", dir);
}

/* Phase 3: the high-score and replay files are written next to the data,
 * as the original writes them next to its own. */
const char* rfileio_get_data_dir(void)
{
	return s_data_dir;
}

/* ------------------------------------------------------------------ */
/* Allocation registry: maps returned pointers to their archive or     */
/* malloc'd chunk so mmgr_free/mmgr_get_chunk_size_bytes work.         */
/* ------------------------------------------------------------------ */
typedef struct chunk_entry {
	void* ptr;                    /* pointer handed to the renderer */
	stunts_res_archive_t* arch;   /* non-NULL if an archive */
	stunts_res_archive_t* view;   /* lazy archive view over a plain chunk */
	uint32_t size;                /* size for plain chunks */
	struct chunk_entry* next;
} chunk_entry;

static chunk_entry* s_chunks = NULL;

static void register_chunk(void* ptr, stunts_res_archive_t* arch, uint32_t size)
{
	chunk_entry* e = (chunk_entry*)malloc(sizeof(chunk_entry));
	e->ptr = ptr;
	e->arch = arch;
	e->view = NULL;
	e->size = size;
	e->next = s_chunks;
	s_chunks = e;
}

static chunk_entry* find_chunk(void* ptr)
{
	for (chunk_entry* e = s_chunks; e; e = e->next)
		if (e->ptr == ptr) return e;
	return NULL;
}

/* ------------------------------------------------------------------ */
/* fatal_error - the renderer calls this on unrecoverable errors       */
/* ------------------------------------------------------------------ */
void fatal_error(const char* fmterr, ...)
{
	va_list ap;
	va_start(ap, fmterr);
	fprintf(stderr, "[renderer fatal] ");
	vfprintf(stderr, fmterr, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	abort();
}

/* ------------------------------------------------------------------ */
/* Resource loading                                                    */
/* ------------------------------------------------------------------ */
static void* load_archive_by_name(const char* filename)
{
	/* filename arrives lowercase with extension, e.g. "game1.p3s".
	 * Files on disk are uppercase. */
	char upper[64];
	char path[640];
	size_t i;
	for (i = 0; filename[i] && i < sizeof(upper) - 1; i++)
		upper[i] = (char)toupper((unsigned char)filename[i]);
	upper[i] = 0;
	snprintf(path, sizeof(path), "%s/%s", s_data_dir, upper);

	stunts_res_archive_t* arch = stunts_asset_load_archive(path);
	if (!arch) return NULL;
	register_chunk(arch->raw_unpacked_data, arch, arch->total_size);
	/* The renderer treats the return value as an opaque resource handle
	 * that locate_shape_* searches; hand back the raw data pointer and
	 * resolve it through the registry. */
	return arch->raw_unpacked_data;
}

void* file_load_resource(int16_t restype, const char* filename)
{
	(void)restype;
	return load_archive_by_name(filename);
}

void* file_load_3dres(const char* filename)
{
	char name[80];
	void* result;

	snprintf(name, sizeof(name), "%s.p3s", filename);
	result = load_archive_by_name(name);
	if (result) return result;

	snprintf(name, sizeof(name), "%s.3sh", filename);
	result = load_archive_by_name(name);
	if (result) return result;

	fatal_error("file_load_3dres: cannot load %s", filename);
	return NULL;
}

void* file_load_resfile(const char* filename)
{
	char name[80];
	void* result;

	snprintf(name, sizeof(name), "%s.res", filename);
	result = load_archive_by_name(name);
	if (result) return result;

	snprintf(name, sizeof(name), "%s.pre", filename);
	result = load_archive_by_name(name);
	if (result) return result;

	fatal_error("file_load_resfile: cannot load %s", filename);
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Shape lookup inside a loaded archive                                */
/* ------------------------------------------------------------------ */
/* The renderer treats a resource handle as opaque, but the game does not: it
 * duplicates one with a plain byte copy and then searches the copy, which
 * shape3d_load_car_shapes does when the opponent drives the player's car.
 * Such a chunk arrives here through mmgr_alloc_resbytes with no archive
 * attached, so parse it on first use. The bytes are a whole archive image, so
 * the ordinary header parse applies; the view does not own them. */
static stunts_res_archive_t* chunk_archive(chunk_entry* e)
{
	if (e->arch) return e->arch;
	if (!e->view && e->ptr && e->size >= 6)
		e->view = stunts_asset_adopt_archive((uint8_t*)e->ptr, e->size, false);
	return e->view;
}

void* locate_shape_nofatal(void* resptr, const char* shapename)
{
	chunk_entry* e = find_chunk(resptr);
	stunts_res_archive_t* arch = e ? chunk_archive(e) : NULL;
	if (!arch) return NULL;
	{
		const stunts_sub_resource_t* sub =
			stunts_asset_find_resource(arch, shapename);
		return sub ? (void*)sub->data : NULL;
	}
}

void* locate_shape_fatal(void* resptr, const char* shapename)
{
	void* p = locate_shape_nofatal(resptr, shapename);
	if (!p) {
		/* Say which of the three ways this failed. A bare "not found" cost
		 * a long hunt once: the handle can be unknown to the registry, a
		 * known chunk whose bytes do not parse, or a good archive that
		 * genuinely lacks the name. */
		chunk_entry* e = find_chunk(resptr);
		stunts_res_archive_t* a = e ? chunk_archive(e) : NULL;
		fatal_error("locate_shape: '%s' saknas (handtag=%p, %s, storlek=%u, "
		            "resurser=%u)", shapename, resptr,
		            !e ? "OKAND - inte i registret"
		               : e->arch ? "laddat arkiv" : "kopia i minnet",
		            e ? e->size : 0u, a ? a->num_resources : 0u);
	}
	return p;
}

void* locate_shape_alt(void* resptr, const char* shapename)
{
	return locate_shape_nofatal(resptr, shapename);
}

/* ------------------------------------------------------------------ */
/* Memory manager shims                                                */
/* ------------------------------------------------------------------ */
uint32_t mmgr_get_res_ofs_diff_scaled(void)
{
	return 0x00FFFFFF; /* "plenty of free memory" */
}

void* mmgr_alloc_resbytes(const char* name, uint32_t numbytes)
{
	(void)name;
	void* p = calloc(1, numbytes);
	register_chunk(p, NULL, numbytes);
	return p;
}

uint32_t mmgr_get_chunk_size_bytes(void* ptr)
{
	chunk_entry* e = find_chunk(ptr);
	return e ? e->size : 0;
}

static void free_entry(void* ptr)
{
	chunk_entry** pp = &s_chunks;
	while (*pp) {
		if ((*pp)->ptr == ptr) {
			chunk_entry* e = *pp;
			*pp = e->next;
			/* a view never owns the bytes; the chunk is freed below */
			if (e->view) stunts_asset_free_archive(e->view);
			if (e->arch) stunts_asset_free_archive(e->arch);
			else free(e->ptr);
			free(e);
			return;
		}
		pp = &(*pp)->next;
	}
}

void mmgr_free(void* ptr)
{
	if (ptr) free_entry(ptr);
}

void mmgr_release(void* ptr)
{
	if (ptr) free_entry(ptr);
}

/* fileio.c:1026 in the original. load_opponent_data() releases the OPP<n>
 * archive as soon as it has copied "sped" out and walked the track, so the
 * port needs the same entry point on this side of the file split. */
void unload_resource(void far* resptr)
{
	if (resptr) free_entry(resptr);
}
