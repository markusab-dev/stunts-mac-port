#ifndef RESTUNTS_MEMMGR_H
#define RESTUNTS_MEMMGR_H

#ifdef RESTUNTS_SDL
#define far
#endif

#pragma pack (push, 1)
struct MEMCHUNK {
	char resname[12];
	uint16_t ressize;
	uint16_t resofs;
	uint16_t resunk;
};
#pragma pack (pop)

const char* mmgr_path_to_name(const char* filename);
void far* mmgr_alloc_pages(const char* arg_0, uint16_t arg_2);
void mmgr_alloc_resmem(uint16_t arg_0);
void mmgr_alloc_a000(void);
uint16_t mmgr_get_ofs_diff();
void far* mmgr_free(char far* ptr);
void mmgr_copy_paras(uint16_t srcseg, uint16_t destseg, int16_t paras);
void copy_paras_reverse(uint16_t srcseg, uint16_t destseg, int16_t paras);
void mmgr_find_free();
void far* mmgr_get_chunk_by_name(const char* arg_0);
void mmgr_release(char far* ptr);
uint16_t mmgr_get_chunk_size(char far* ptr);
uint16_t mmgr_resize_memory(uint16_t arg_0, uint16_t arg_2, uint16_t arg_4);
void far* mmgr_op_unk(char far* ptr);
void far* mmgr_alloc_resbytes(const char* name, int32_t size);
uint32_t mmgr_get_res_ofs_diff_scaled(void);
uint32_t mmgr_get_chunk_size_bytes(char far* ptr);

char far* locate_resource(char far* data, char* name, uint16_t fatal);
char far* locate_shape_nofatal(char far* data, char* name);
char far* locate_shape_fatal(char far* data, char* name);
char far* locate_shape_alt(char far* data, char* name);
char far* locate_sound_fatal(char far* data, char* name);
void locate_many_resources(char far* data, char* names, char far** result);
char far* locate_text_res(char far* data, char* name);

#endif
