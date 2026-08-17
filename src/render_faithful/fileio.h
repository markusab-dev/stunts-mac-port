#ifndef RESTUNTS_FILEIO_H
#define RESTUNTS_FILEIO_H

#ifdef RESTUNTS_SDL
#define far
#endif

#ifdef __cplusplus
extern "C" {
#endif

const char* file_find(const char* query);
const char* file_find_next(void);
const char* file_find_next_alt(void);

void file_build_path(const char* dir, const char* name, const char* ext, char* dst);
const char* file_combine_and_find(const char* dir, const char* name, const char* ext);

uint16_t file_paras(const char* filename, int16_t fatal);
uint16_t file_paras_fatal(const char* filename);
uint16_t file_paras_nofatal(const char* filename);

uint16_t file_decomp_paras(const char* filename, int16_t fatal);
uint16_t file_decomp_paras_fatal(const char* filename);
uint16_t file_decomp_paras_nofatal(const char* filename);

void far* file_read(const char* filename, void far* dst, int16_t fatal);
void far* file_read_fatal(const char* filename, void far* dst);
void far* file_read_nofatal(const char* filename, void far* dst);

int16_t file_write(const char* filename, void far* src, uint32_t length, int16_t fatal);
int16_t file_write_fatal(const char* filename, void far* src, uint32_t length);
int16_t file_write_nofatal(const char* filename, void far* src, uint32_t length);

void far* file_decomp(const char* filename, int16_t fatal);
void far* file_decomp_fatal(const char* filename);
void far* file_decomp_nofatal(const char* filename);

void far* file_load_binary(const char* filename, int16_t fatal);
void far* file_load_binary_nofatal(const char* filename);
void far* file_load_binary_fatal(const char* filename);

void far* file_load_resfile(const char* filename);
void far* file_load_resource(int16_t type, const char* filename);
void unload_resource(void far* resptr);
void file_load_audiores(const char* songfile, const char* voicefile, const char* name);
void far* file_load_3dres(const char* filename);

int16_t file_load_replay(const char* dir, const char* name);
int16_t file_write_replay(const char* filename);

#ifdef __cplusplus
}
#endif

#endif
