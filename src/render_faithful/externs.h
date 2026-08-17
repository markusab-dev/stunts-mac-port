#ifndef RESTUNTS_EXTERNS_H
#define RESTUNTS_EXTERNS_H

#include "math.h"

#ifdef RESTUNTS_SDL
#define far
#define huge
#endif

#pragma pack (push, 1)

struct GAMEINFO {
	char game_playercarid[4];
	char game_playermaterial;
	char game_playertransmission;
	char game_opponenttype;
	char game_opponentcarid[4];
	char game_opponentmaterial;
	char game_opponenttransmission;
	char game_trackname[9];
	uint16_t game_framespersec;
	uint16_t game_recordedframes;
};

struct CARSTATE {
	struct VECTORLONG car_posWorld1;
	struct VECTORLONG car_posWorld2;
	struct VECTOR car_rotate; // applying the (x, y, z) vector notation to rotation
                              // angles is a source of confusion.
	int16_t car_pseudoGravity;
	int16_t car_steeringAngle;
	int16_t car_currpm;
	int16_t car_lastrpm;
	int16_t car_idlerpm2;
	int16_t car_speeddiff; // former gripdiff
	uint16_t car_speed;     // former trackgrip
                         // value is 2^8*(mph value) and uint16_t
	uint16_t car_speed2;    // former trackgrip2
                         // speed is the rev-coupled speed, while speed2 is
                         // the actual car speed. They are different, for
                         // instance, during jumps (where accelerating increases
                         // revs without making the car go faster).
	uint16_t car_lastspeed; // former lasttrackgrip
	uint16_t car_gearratio;
	uint16_t car_gearratioshr8;
	int16_t car_knob_x;
	int16_t car_36MwhlAngle;
	int16_t car_knob_y;
	int16_t car_knob_x2;
	int16_t car_knob_y2;
	int16_t car_angle_z;
	int16_t car_40MfrontWhlAngle;
	int16_t field_42;
	int16_t car_demandedGrip;
	int16_t car_surfacegrip_sum;
	int16_t field_48;
	int16_t car_trackdata3_index;
	int16_t car_rc1[4]; // four words, one for each wheel.
	int16_t car_rc2[4];
	int16_t car_rc3[4];
	int16_t car_rc4[4];
	int16_t car_rc5[4];
	struct VECTOR car_whlWorldCrds1[4];
	struct VECTOR car_whlWorldCrds2[4];
	struct VECTOR car_vec_unk3;
	struct VECTOR car_vec_unk4;
	struct VECTOR car_vec_unk5;
	int16_t field_B6;
	int16_t field_B8;
	int16_t field_BA;
	char car_is_braking;
	char car_is_accelerating;
	char car_current_gear;
	char car_sumSurfFrontWheels;
	char car_sumSurfRearWheels;
	char car_sumSurfAllWheels; // used as jump flag.
	char car_surfaceWhl[4];      // surface types for each of the wheels, it seems.
	char car_engineLimiterTimer;
	char car_slidingFlag;
	char field_C8;
	char car_crashBmpFlag;
	char car_changing_gear;
	char car_fpsmul2;
	char car_transmission;
	char field_CD;
	char field_CE; // is added?
	char field_CF; // is initialized?
};

struct GAMESTATE {
	int32_t game_longs1[24]; // x
	int32_t game_longs2[24]; // y
	int32_t game_longs3[24]; // z
	struct VECTOR game_vec1[2]; // 0 = player, 1 = opponent
	struct VECTOR game_vec3;
	struct VECTOR game_vec4;
	int16_t game_frame_in_sec;
	int16_t game_frames_per_sec;
	int32_t  game_travDist;
	int16_t game_frame;
	int16_t game_total_finish; // finish time + penalty when crossed finish line
	int16_t field_144;
	int16_t game_pEndFrame;
	int16_t game_oEndFrame;   // former game_frame2
	int16_t game_penalty; // probably penalty counter
	uint16_t game_impactSpeed;
	uint16_t game_topSpeed;
	int16_t game_jumpCount;
	struct CARSTATE playerstate;
	struct CARSTATE opponentstate;
	int16_t field_2F2;
	int16_t field_2F4;
	int16_t game_startcol;
	int16_t game_startcol2;
	int16_t game_startrow;
	int16_t game_startrow2;
	int16_t field_2FE[24];
	int16_t field_32E[24];
	int16_t field_35E[24];
	int16_t field_38E[24];
	char field_3BE[48];
	char kevinseed[6];
	char field_3F4;
	char game_inputmode; // 0 = waiting for input, 1 = input active, 2 = no input (during the intro)
	char game_3F6autoLoadEvalFlag;
	char field_3F7[2]; // 0 = player, 1 = opponent
	char field_3F9;
	char field_3FA[48];
	char field_42A;
	char field_42B[24];
	char field_443[24];
	char field_45B;
	char field_45C;
	char field_45D;
	char field_45E;
	char field_45F;
};

struct SIMD {
	char num_gears;
	char simd_unk;
	int16_t car_mass;
	int16_t braking_eff;
	int16_t idle_rpm;
	int16_t downshift_rpm;
	int16_t upshift_rpm;
	int16_t max_rpm;
	uint16_t gear_ratios[7];
	struct POINT2D knob_points[7];
	int16_t aero_resistance;
	char idle_torque;
	char torque_curve[104];
	char field_A3;
	int16_t grip;
	int16_t field_A6[7];
	int16_t sliding;
	int16_t surface_grip[4];
	char simd_unk3[10];
	struct POINT2D collide_points[2];
	int16_t car_height;
	struct VECTOR wheel_coords[4];
	char steeringdots[62];
	struct POINT2D spdcenter;
	int16_t spdnumpoints;
	char spdpoints[208];
	struct POINT2D revcenter;
	int16_t revnumpoints;
	char revpoints[256];
	int16_t far* aerorestable;
};

struct TRKOBJINFO {
	char  si_noOfBlocks;      // How many shapeInfo pieces compose the element. Arbitrary for the first piece, 0 for the following ones.
	char  si_entryPoint;      // Connectivity of the track element regarding tiles.
	char  si_exitPoint;
	char  si_entryType;        // Connectivity of the track element regarding element types.
	char  si_exitType;
	char  si_arrowType;        // Type of the element for determining penalty-arrow behaviour.
	int16_t si_arrowOrient;      // Orientation angle for penalty-arrow purposes
	int16_t* si_cameraDataOffset; // offset (0003B770)
	char  si_opp1;             //Appears to affect how the opponent AI approaches an element.
	char  si_opp2;
	char  si_opp3;
	char  si_oppSpedCode;
};

/* src/sim_faithful/sfshapeinfo.c, generated by tools/extract_shapeinfos.py */
extern struct TRKOBJINFO shapeinfos[120];
/* src/sim_faithful/sftrack_setup.c */
extern int16_t track_setup(void);
extern void sfdata_init_trackdata(void);
extern int16_t track_pieces_counter;
extern uint8_t byte_45635, byte_45D90, byte_45E16, byte_4616E;
extern const uint8_t* shapeinfo_opp_ptr[120];
extern struct TRKOBJINFO shapeinfo_null[16];
extern unsigned long shapeinfo_null_hits;

struct TRACKOBJECT {
	struct TRKOBJINFO* ss_trkObjInfoPtr; // offset (0003B770)
	int16_t ss_rotY;           // Horizontal orientation of the element.
	struct SHAPE3D* ss_shapePtr;       // offset (0003B770)
	struct SHAPE3D* ss_loShapePtr;     // offset (0003B770)
	uint8_t  ss_ssOvelay;       // Renders additional sceneShapes over the current one.
	char  ss_surfaceType;    // Paintjob. FF will induce alternating paintjobs.
	char  ss_ignoreZBias;    // Appears to be Z-bias override flag, mostly used for roads and corners.
	char  ss_multiTileFlag;  // 0 = one-tile, 1 = two-tile vertical, 2 = two-tile horizontal, 3 = four-tile.
	char  ss_physicalModel;  // sets the physical model in build_track_object
	char  scene_unk5;        // always zero.
};

#pragma pack (pop)

extern struct GAMEINFO gameconfig;
extern struct GAMEINFO gameconfigcopy;

extern struct GAMESTATE state;
extern struct SIMD simd_player;
extern struct SIMD simd_opponent;

extern int16_t video_flag1_is1;
extern int16_t video_flag2_is1;
extern int16_t video_flag3_isFFFF;
extern int16_t video_flag4_is1;
extern int16_t video_flag5_is0;
extern int16_t video_flag6_is1;

extern uint8_t byte_44A8A;
extern uint8_t byte_4552F;
extern uint16_t elapsed_time1;
extern uint16_t elapsed_time2;
extern uint8_t byte_449DA;
extern uint8_t byte_4393C;
extern uint8_t game_replay_mode; // 0 = playing, 1 = paused, 2 = replay
extern int16_t word_44DCA;

extern int16_t word_45A24; // current frame?
extern int16_t word_45A00; // fps * 30
extern int16_t word_4499C; // 100 / fps
extern int16_t track_angle;
extern void* steerWhlRespTable_ptr;
extern void* steerWhlRespTable_10fps;
extern void* steerWhlRespTable_20fps;
extern char startcol2, startrow2;
extern char hillFlag;
extern int16_t hillHeightConsts[];

extern struct RECTANGLE rect_windshield;
extern int16_t word_449EA;
extern int16_t run_game_random;
extern char replaybar_toggle;
extern char is_in_replay;
extern char cameramode;
extern char byte_449E6;
extern char game_replay_mode_copy;
extern char byte_44346;
extern char byte_46467;
extern char dashb_toggle;
extern char byte_4432A;
extern char show_penalty_counter;
extern int16_t word_45D94;
extern int16_t word_45D3E;
extern char byte_3B8F2;
extern char byte_3FE00;
extern void far* gameresptr;
extern void far* dasmshapeptr;
extern int16_t word_3F88E;
extern char dashb_toggle_copy;
extern char replaybar_toggle_copy;
extern char is_in_replay_copy;
extern char followOpponentFlag;
extern char followOpponentFlag_copy;
extern int16_t roofbmpheight_copy;
extern char byte_449E2;
extern char replaybar_enabled;
extern int16_t dashbmp_y_copy;
extern int16_t height_above_replaybar;
extern char byte_454A4;
extern char byte_449D8[];
extern int16_t dastseg;
extern int16_t dastbmp_y;
extern int16_t dastbmp_y2;
extern int16_t dashbmp_y;
extern int16_t roofbmpheight;
extern struct RECTANGLE* rectptr_unk;

extern void player_op(char);
extern void opponent_op(void);
extern void audio_carstate(void);
/* src/sim_faithful/sfasm_port.c - the per-frame driver and its helpers.
 * update_gamestate is what calls player_op/opponent_op in the original;
 * nothing else should call them directly. */
extern void update_gamestate(void);        /* seg001.asm 4395..4603 */
extern void move_helicopters(void);        /* seg005.asm 1552..1923 (sub_2298C) */
extern void sub_19BA0(void);               /* seg001.asm 9386..9507 */
extern void init_game_state_vars(void);    /* seg001.asm 3885..4021 (fragment) */
extern int16_t get_kevinrandom(void);      /* seg002.asm  232..267 */
extern void get_kevinrandom_seed(char*);   /* seg002.asm  206..231 */
/* Installed by audio_native.c; stands in for update_gamestate's two
 * `call audio_carstate` sites in builds that do not link SDL. */
extern void (*sim_hook_audio_frame)(void);
extern void setup_car_shapes(int16_t);
extern void update_frame(char, struct RECTANGLE* rc);
extern void loop_game(int16_t, int16_t, int16_t);
extern void set_frame_callback(void);
extern void mouse_minmax_position(int16_t);
extern int16_t kb_get_char(void);
extern void handle_ingame_kb_shortcuts(int16_t);

extern int16_t mouse_butstate;
extern int16_t mouse_xpos;
extern int16_t mouse_ypos;
extern int16_t performGraphColor;
extern char resID_byte1[32];
extern int16_t waitflag;

extern void far* fontnptr;
extern void far* fontdefptr;
extern void far* mainresptr;
extern struct GAMESTATE far* cvxptr;
extern int16_t trackrows[];
extern int16_t terrainrows[];
extern int16_t trackpos[];
extern int16_t trackcenterpos[];
extern int16_t terrainpos[];
extern int16_t terraincenterpos[];
extern int16_t trackpos2[];
extern int16_t trackcenterpos2[];
extern int16_t far* td01_track_file_cpy; //trackdata1;
extern int16_t far* td02_penalty_related; //trackdata2;
extern char far* trackdata3;
extern int16_t far* td04_aerotable_pl; //trackdata4;
extern int16_t far* td05_aerotable_op; //trackdata5;
extern char far* trackdata6;
extern char far* trackdata7;
extern int16_t far* td08_direction_related; //trackdata8;
extern int16_t far* trackdata9;
extern int16_t far* td10_track_check_rel;// trackdata10;
extern char far* td11_highscores; //trackdata11;
extern char far* trackdata12;
extern char far* td13_rpl_header; //trackdata13;
extern uint8_t far* td14_elem_map_main; //trackdata14;
extern uint8_t far* td15_terr_map_main; //trackdata15;
extern char far* td16_rpl_buffer; //trackdata16;
extern char far* td17_trk_elem_ordered; //trackdata17;
extern char far* trackdata18;
extern uint8_t far* trackdata19;
extern char far* td20_trk_file_appnd; //trackdata20;
extern char far* td21_col_from_path; //trackdata21;
extern char far* td22_row_from_path; //trackdata22;
extern uint8_t far* trackdata23; // indexes into trkObjectList
extern char kbormouse;
extern char passed_security;
extern char g_is_busy;
extern char g_path_buf[];
extern char byte_3B80C[];
extern char idle_expired;
extern uint16_t dialogarg2;
extern char byte_3B85E[];
extern char byte_43966;
extern char aMain[];
extern char aMisc_1[];
extern char aFontdef_fnt[];
extern char aFontn_fnt[];
extern char aTrakdata[];
extern char aDefault_0[];
extern char aCvx[];
extern char aTedit__0[];
extern char aSlct[];
extern char aSkidms_0[];
extern char aSkidslct[];
extern char aDos[];

extern uint16_t framespersec;
extern uint16_t framespersec2;
extern uint16_t slow_video_mgmt;
extern uint16_t slow_video_mgmt_copy;
extern uint8_t detail_level;

extern uint16_t pspofs;
extern uint16_t pspseg;
extern uint16_t word_3FF82;
extern uint16_t word_3FF84;

extern struct MEMCHUNK* resptr1;
extern struct MEMCHUNK* resptr2;
extern struct MEMCHUNK* resendptr1;
extern struct MEMCHUNK* resendptr2;
extern uint16_t resmaxsize;

extern uint32_t timer_callback_counter;
extern uint32_t last_timer_callback_counter;
extern uint32_t timer_copy_unk;

extern uint8_t g_kevinrandom_seed[];
extern const char aReservememoryO[];
extern const char aReservememoryOutOfMemory[];
extern const char aMemoryManagerB[];
extern const char aResizememoryNo[];
extern const char aResizememoryCa[];
extern const char aSFileError[];
extern const char aSFileError_0[];
extern const char aSFileError_1[];
extern const char aSInvalidPackTy[];
extern const char aLocateshape4_4sShapeNotF[];
extern const char aLocatesound4_4sSoundNotF[];
extern char audiodriverstring[];

extern uint16_t gState_frame;
extern int16_t is_audioloaded;
extern void far* songfileptr;
extern void far* voicefileptr;
extern char textresprefix; // = 'e'
extern char* shapeexts[];
extern uint8_t palmap[];

extern int16_t* material_clrlist_ptr;
extern int16_t* material_clrlist_ptr_cpy;
extern int16_t* material_clrlist2_ptr;
extern int16_t* material_clrlist2_ptr_cpy;
extern int16_t* material_patlist_ptr;
extern int16_t* material_patlist_ptr_cpy;
extern int16_t* material_patlist2_ptr;
extern int16_t* material_patlist2_ptr_cpy;
extern uint16_t someZeroVideoConst;

extern int16_t sub_18D60(int16_t car_trackdata3_index, struct VECTOR* car_vec_unk3, int16_t field_CE, int16_t* unk);
extern void font_set_fontdef(void);
extern void init_polyinfo(void);
extern uint16_t run_intro_looped(void);
extern uint16_t show_dialog(int16_t unk1, int16_t unk2, void far* textresptr, uint16_t unk3, uint16_t unk4, int16_t arg, void* unk5, int16_t unk6);
extern char run_menu(void);
extern char setup_track(void);
extern void run_tracks_menu(int16_t unk);
extern void run_opponent_menu(void);
extern void show_waiting(void);
extern void run_car_menu(struct GAMEINFO* unk, char* unk2, char* unk3, uint16_t unk4);
extern void run_game(void);
extern uint16_t end_hiscore(void);
extern uint16_t run_option_menu(void);
extern void security_check(void);

extern void ensure_file_exists(int16_t unk);

extern void far* load_song_file(const char* filename);
extern void far* load_voice_file(const char* filename);
extern void far* load_sfx_file(const char* filename);
extern void far* file_load_shape2d_nofatal_thunk(const char* filename);
extern void far* file_load_shape2d_res_nofatal_thunk(const char* filename);
extern void far* file_load_shape2d_nofatal(char* shapename);
extern void far* file_load_shape2d_nofatal2(char* shapename);
extern void far* init_audio_resources(void far* songptr, void far* voiceptr, const char* name);
extern void load_audio_finalize(void far* audiores);
extern int16_t audio_load_driver(char* driver, int16_t a2, int16_t a3);
extern void audio_unload(void);
extern int16_t audio_toggle_flag2(void);
extern int16_t audio_toggle_flag6(void);
extern void audio_stop_unk(void);
extern void audiodrv_atexit(void);

extern void check_input(void);
extern int16_t input_do_checking(int16_t unk);
extern void kb_exit_handler(void);
extern void kb_shift_checking1(void);
extern void kb_shift_checking2(void);
extern void kb_reg_callback(int16_t code, void (far* callback)(void));
extern void show_graphic_levels_menu(void);
extern void do_joy_restext(void);
extern void do_key_restext(void);
extern void do_mof_restext(void);
extern void do_pau_restext(void);
extern void do_dos_restext(void);
extern void do_sonsof_restext(void);
extern int16_t get_kb_or_joy_flags(void);

extern int16_t mouse_init(int16_t a1, int16_t a2);
extern void mouse_draw_opaque_check(void);

extern void video_set_mode4(void);
extern void video_set_mode7(void);
extern void video_set_mode_13h(void);

extern void shape3d_load_car_shapes(char* carid, char* oppcarid);

extern void load_palandcursor(void);
extern void sprite_set_1_size(uint16_t left, uint16_t right, uint16_t top, uint16_t height);
extern void sprite_clear_1_color(uint8_t);
extern void sprite_blit_to_video(struct SPRITE far* sprite);

extern int16_t intr0_handler(void);
extern int16_t (far* old_intr0_handler)(void);
extern void timer_setup_interrupt(void);
extern uint32_t timer_get_delta_alt(void);

extern int16_t set_criterr_handler(int16_t (far* callback)(void));
extern void libsub_quit_to_dos_alt(int16_t a1);
extern void fatal_error(const char*, ...);
extern int16_t do_dea_textres(void);

extern void* _memcpy(void*, const void*, uint16_t);
extern char* _strcpy(char* dest, const char* src);
extern char* _strcat(char* dest, const char* src);
extern int16_t _strcmp(const char* dest, const char* src);
extern int16_t _stricmp(const char* dest, const char* src);
extern uint16_t _strlen(const char* str);
extern void far* __fmemcpy(void far*, const void far*, uint16_t);
extern uint16_t _abs(uint16_t);
extern int16_t _rand(void);
extern void _srand(uint16_t);

#ifdef RESTUNTS_DOS
#define memcpy _memcpy
#define strcpy _strcpy
#define strcat _strcat
#define strlen _strlen
#define fmemcpy __fmemcpy
#define strcmp _strcmp
#define stricmp _stricmp
#define abs _abs
#define printf _printf
#define rand _rand
#define srand _srand
#else
#define MK_FP(x, y) ((x << 4) + y)
#define FP_SEG(x) 0
#define FP_OFF(x) (size_t)x
#endif

#endif
