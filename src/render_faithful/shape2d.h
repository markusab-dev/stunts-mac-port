#ifndef RESTUNTS_SHAPE2D_H
#define RESTUNTS_SHAPE2D_H

#pragma pack (push, 1)
struct SHAPE2D {
	uint16_t s2d_width;
	uint16_t s2d_height;
	uint16_t s2d_unk1;
	uint16_t s2d_unk2;
	uint16_t s2d_pos_x;
	uint16_t s2d_pos_y;
	uint8_t s2d_unk3;
	uint8_t s2d_unk4;
	uint8_t s2d_unk5;
	uint8_t s2d_unk6;
};

struct SPRITE {
	struct SHAPE2D far* sprite_bitmapptr;
	uint16_t sprite_unk1;
	uint16_t sprite_unk2;
	uint16_t sprite_unk3;
	uint16_t* sprite_lineofs;
	uint16_t sprite_left;
	uint16_t sprite_right;
	uint16_t sprite_top;
	uint16_t sprite_height;
	uint16_t sprite_pitch;
	uint16_t sprite_unk4;
	uint16_t sprite_width2;
	uint16_t sprite_left2;
	uint16_t sprite_widthsum;
};
#pragma pack (pop)

struct SPRITE far* sprite_make_wnd(uint16_t width, uint16_t height, uint16_t);
void sprite_free_wnd(struct SPRITE far* wndsprite);

void sprite_set_1_from_argptr(struct SPRITE far* argsprite);

void sprite_copy_2_to_1(void);
void sprite_copy_2_to_1_2(void);
void sprite_copy_2_to_1_clear(void);
void sprite_copy_wnd_to_1(void);
void sprite_copy_wnd_to_1_clear(void);

void sprite_copy_both_to_arg(struct SPRITE* argsprite);
void sprite_copy_arg_to_both(struct SPRITE* argsprite);

void sprite_clear_1_color(uint8_t color);

void sprite_putimage(struct SHAPE2D far* shape);
void sprite_putimage_and(struct SHAPE2D far* shape, uint16_t a, uint16_t b);
void sprite_putimage_or(struct SHAPE2D far* shape, uint16_t a, uint16_t b);

void setup_mcgawnd1(void);
void setup_mcgawnd2(void);

struct SHAPE2D far* file_get_shape2d(uint8_t far* memchunk, int16_t index);

uint16_t file_get_res_shape_count(void far* memchunk);

void file_unflip_shape2d(uint8_t far* memchunk, char far* mempages);

void file_unflip_shape2d_pes(uint8_t far* memchunk, char far* mempages);

void file_load_shape2d_expand(uint8_t far* memchunk, char far* mempages);

uint16_t file_get_unflip_size(char far* memchunk);

uint16_t file_load_shape2d_expandedsize(void far* memchunk);

void file_load_shape2d_palmap_init(uint8_t far* pal);
void file_load_shape2d_palmap_apply(uint8_t far* memchunk, uint8_t palmap[]);

void far* file_load_shape2d_esh(void far* memchunk, const char* str);
void far* file_load_shape2d(char* shapename, int16_t fatal);

void far* file_load_shape2d_fatal(char* shapename);
void far* file_load_shape2d_nofatal(char* shapename);
void far* file_load_shape2d_nofatal2(char* shapename);

void far* file_load_shape2d_res(char* resname, int16_t fatal);
void far* file_load_shape2d_res_fatal(char* resname);
void far* file_load_shape2d_res_nofatal(char* resname);

#endif
