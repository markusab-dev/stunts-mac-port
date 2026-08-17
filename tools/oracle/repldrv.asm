;=============================================================================
; repldrv.asm -- behavioural oracle driver for the original 16-bit Stunts code
;-----------------------------------------------------------------------------
; This module is a hand transcription of stuntsmain() (plus the RESTUNTS_ORIGINAL
; helpers fopen/fclose/fwrite/init_row_tables/init_trackdata) from
;   reference/restunts/src/restunts/repldump/repldump.c
; into TASM assembly.  It exists because bcc.exe in the bundled toolchain cannot
; run (32RTM.EXE is missing and is not legally obtainable), so repldump.c cannot
; be compiled.  Everything else the oracle needs is already present in the
; disassembled original segments seg000..seg041 + dseg.
;
; It replays a .RPL through the ORIGINAL game code and dumps the full 1120-byte
; GAMESTATE for every simulated frame to <replayname>.BIN:
;
;     offset 0 : uint16  frame count  (= gameconfig.game_recordedframes)
;     offset 2 : N * 1120 bytes, one GAMESTATE per frame, in frame order
;
; All printf() progress messages in repldump.c are deliberately omitted; they
; have no effect on the dumped state.  Three unbuffered INT 21h/40h progress
; markers are written to stdout after the last fwrite instead -- see the note
; on fatal_error at the bottom of stuntsmain for why they matter.
;
;-----------------------------------------------------------------------------
; CONVENTIONS (each verified against the original disassembly, see notes)
;
; * .model medium -> far code / near data, DS = dseg.  Header pattern copied
;   from seg041.asm.  custom.inc is deliberately NOT included: it contains
;   "extrn stuntsmain:proc", which would clash with our "public stuntsmain".
;
; * Borland C: args pushed right-to-left, CALLER cleans the stack, result in AX
;   (32-bit / far pointer in DX:AX), callee preserves BP/SI/DI/DS/SS.
;   Verified in seg000.asm (ported_stuntsmain_) and seg005.asm (run_game).
;
; * stuntsmain is entered by a FAR call from seg010.asm:1cc..:
;       xor bp, bp / push word_3EE0C / push argv / push argc / call stuntsmain
;   -> after "push bp / mov bp,sp":  [bp+6]=argc, [bp+8]=argv, [bp+10]=envp.
;
; * SS == DS == dseg for the whole program: the hacked CRT in seg010.asm does
;   "mov di, seg dseg / mov ss, di / add sp, 0AD1Eh".  Therefore the offset of a
;   stack local is a valid *near* (DS-relative) pointer and may be passed to the
;   game's near-pointer functions and used as DS:DX for INT 21h.
;
; * sizeof(GAMESTATE) = 0460h = 1120.  Verified three ways: structs.inc
;   (last member field_45F db at 45Fh), dseg.asm (the "state" reservation is
;   exactly 1120 db lines), and seg001.asm update_gamestate ("mov cx, 230h /
;   repne movsw" = 230h words = 460h bytes).
; * sizeof(GAMEINFO)  = 001Ah = 26.  Verified in seg000.asm ("mov cx, 0Dh /
;   repne movsw" for the gameconfig -> gameconfigcopy copy).
;=============================================================================

.model medium
nosmart
    include structs.inc
    include seg000.inc
    include seg001.inc
    include seg002.inc
    include seg003.inc
    include seg004.inc
    include seg005.inc
    include seg006.inc
    include seg007.inc
    include seg008.inc
    include seg009.inc
    include seg010.inc
    include seg011.inc
    include seg012.inc
    include seg013.inc
    include seg014.inc
    include seg015.inc
    include seg016.inc
    include seg017.inc
    include seg018.inc
    include seg019.inc
    include seg020.inc
    include seg021.inc
    include seg022.inc
    include seg023.inc
    include seg024.inc
    include seg025.inc
    include seg026.inc
    include seg027.inc
    include seg028.inc
    include seg029.inc
    include seg030.inc
    include seg031.inc
    include seg032.inc
    include seg033.inc
    include seg034.inc
    include seg035.inc
    include seg036.inc
    include seg037.inc
    include seg038.inc
    include seg039.inc
    include dseg.inc
    include seg041.inc

GAMESTATE_SIZE  equ 0460h        ; sizeof(struct GAMESTATE) = 1120
GAMEINFO_WORDS  equ 000Dh        ; sizeof(struct GAMEINFO) / 2 = 26 / 2

; stack frame of stuntsmain (see prologue)
V_OUTNAME       equ -20          ; char outname[20]  (repldump.c: char[13])
V_TRAKF         equ -22          ; FILE* for TRAKDATA.BIN (added for the table dump)
V_STRBUF        equ -40          ; char scratch[20]  for literals not in dseg
V_FOUT          equ -42          ; FILE* fout
V_GLOBF         equ -44          ; FILE* for GLOBALS.BIN (build_track_object probe)
V_GLOB          equ -52          ; 8-byte record: terrainHeight, planindex,
                                 ; wallindex, current_surf_type
V_FRAMESIZE     equ 54  ; +2 V_TRAKF, +2 V_GLOBF, +8 V_GLOB

repldrv segment byte public 'CODE' use16
    assume cs:repldrv
    assume es:nothing, ss:nothing, ds:dseg

    public stuntsmain

;-----------------------------------------------------------------------------
; Module-private data.  Lives in the code segment (real mode: writable), read
; via the automatic CS: override that ASSUME CS:repldrv produces.
; These three literals are the only strings stuntsmain needs that do not
; already exist in dseg; they are copied into a stack buffer before use so that
; every pointer handed to game code / INT 21h is a normal DS-relative pointer.
;-----------------------------------------------------------------------------
s_bin       db '.BIN', 0
s_wmode     db 'w', 0
s_done      db 0Ah, 'Done.', 0Ah, 0
g_errno     dw 0                 ; repldump.c: int g_errno (set, never read)

; Progress markers.  repldump.c prints progress with printf(); all of those are
; omitted here except these three, which are written straight to DOS handle 1
; with INT 21h/40h.  Unlike printf they are unbuffered, so they land in the
; redirected log the instant they happen -- that is what tells an operator that
; the dump really is complete and closed, without having to trust a wall clock.
; All three are emitted AFTER the last fwrite, so they cannot affect the dump.
s_m_frames  db 'frames OK', 0Dh, 0Ah, 0
s_m_closed  db 'file closed OK', 0Dh, 0Ah, 0
s_m_input   db 'input OK', 0Dh, 0Ah, 0

; --- trakdata dump (not part of repldump.c) ---------------------------------
; After track_setup() has built the trakdata chunk, write the whole 0x6BF3-byte
; allocation plus the start scalars to TRAKDATA.BIN.  The native port cannot
; run track_setup (seg004.asm, unported), so this lets it load the real tables
; and be measured against the oracle without them being the difference.
; Written before the frame loop starts and never touched again.
s_trakname  db 'TRAKDATA.BIN', 0
s_m_trak    db 'trakdata OK', 0Dh, 0Ah, 0
; Per-frame snapshot of what build_track_object decided.  Our port's version of
; that routine is a machine translation of ~2700 lines of asm and was never
; ported by restunts itself, so its outputs are the thing most worth checking.
s_globname  db 'GLOBALS.BIN', 0
s_m_glob    db 'globals OK', 0Dh, 0Ah, 0

;-----------------------------------------------------------------------------
; dos_stdout -- write the ASCIIZ string at CS:SI to DOS handle 1 (stdout).
; Trashes AX, BX, CX, DX, DI.  DS is preserved.
;-----------------------------------------------------------------------------
dos_stdout proc near
    push    ds
    push    cs
    pop     ds                  ; DS = repldrv, so DS:SI addresses the string
    mov     dx, si
    mov     di, si
    sub     cx, cx
dso_len:
    cmp     byte ptr [di], 0
    je      short dso_write
    inc     di
    inc     cx
    jmp     short dso_len
dso_write:
    mov     bx, 1               ; stdout
    mov     ah, 40h             ; DOS - write to file/device
    int     21h
    pop     ds
    ret
dos_stdout endp

;-----------------------------------------------------------------------------
; cs_strcpy -- copy the ASCIIZ string at CS:SI to DS:DI, terminator included.
; On return DI points one past the terminator.  Trashes AL, SI, DI.
;-----------------------------------------------------------------------------
cs_strcpy proc near
cs_strcpy_1:
    mov     al, cs:[si]
    mov     [di], al
    inc     si
    inc     di
    or      al, al
    jnz     short cs_strcpy_1
    ret
cs_strcpy endp

;-----------------------------------------------------------------------------
; FILE* dos_fopen(const char* path, const char* mode)   [repldump.c fopen()]
;   [bp+4] = path (near), [bp+6] = mode (near)
; mode[0]=='w' -> INT 21h/3Ch create, otherwise INT 21h/3Dh open read-only.
; Returns the DOS handle in AX, or 0 on failure.
;-----------------------------------------------------------------------------
dos_fopen proc near
    push    bp
    mov     bp, sp
    mov     g_errno, 0
    mov     bx, [bp+6]              ; mode
    cmp     byte ptr [bx], 'w'
    jne     short dfo_read
    mov     ah, 3Ch                 ; DOS - create file
    mov     cx, 0                   ; no attributes
    mov     dx, [bp+4]              ; DS:DX = path
    int     21h
    jnc     short dfo_done
    mov     ax, 0
    mov     g_errno, 1
    jmp     short dfo_done
dfo_read:
    mov     ah, 3Dh                 ; DOS - open file
    mov     al, 0                   ; read only
    mov     dx, [bp+4]              ; DS:DX = path
    int     21h
    jnc     short dfo_done
    mov     ax, 0
    mov     g_errno, 1
dfo_done:
    pop     bp
    ret
dos_fopen endp

;-----------------------------------------------------------------------------
; int dos_fclose(FILE* file)                            [repldump.c fclose()]
;   [bp+4] = handle
;-----------------------------------------------------------------------------
dos_fclose proc near
    push    bp
    mov     bp, sp
    mov     ah, 3Eh                 ; DOS - close file
    mov     bx, [bp+4]
    int     21h
    jnc     short dfc_done
    mov     ax, 0
    mov     g_errno, 1
dfc_done:
    pop     bp
    ret
dos_fclose endp

;-----------------------------------------------------------------------------
; size_t dos_fwrite(const void far* src, size_t size, size_t nmemb, FILE* file)
;   [bp+4]=src offset  [bp+6]=src segment  [bp+8]=size  [bp+10]=nmemb
;   [bp+12]=handle
; Writes size*nmemb bytes (16-bit product, as in repldump.c).  Returns the
; number of bytes actually written in AX, 0 on failure.
;-----------------------------------------------------------------------------
dos_fwrite proc near
    push    bp
    mov     bp, sp
    mov     ax, [bp+8]              ; size
    mul     word ptr [bp+10]        ; size *= nmemb
    mov     cx, ax
    mov     bx, [bp+12]             ; handle
    mov     dx, [bp+4]              ; src offset
    push    ds
    mov     ds, word ptr [bp+6]     ; src segment
    mov     ah, 40h                 ; DOS - write to file
    int     21h
    jnc     short dfw_done
    mov     ax, 0
    mov     g_errno, 1
dfw_done:
    pop     ds
    pop     bp
    ret
dos_fwrite endp

;-----------------------------------------------------------------------------
; void init_row_tables(void)                    [repldump.c init_row_tables()]
;   for (i = 0; i < 30; i++) {
;       trackrows[i]        = 30 * (29 - i);
;       terrainrows[i]      = 30 * i;
;       trackpos[i]         = (29 - i) << 10;
;       trackcenterpos[i]   = ((29 - i) << 10) + 0x200;
;       terrainpos[i]       = i << 10;
;       terraincenterpos[i] = (i << 10) + 0x200;
;   }
;   for (i = 0; i < 30; i++) {
;       trackpos2[i]        = i << 10;
;       trackcenterpos2[i]  = (i << 10) + 0x200;
;   }
; (the "+ 0x200" is done as "add ah, 2", exactly as the original does it)
;-----------------------------------------------------------------------------
init_row_tables proc near
    push    si
    push    di
    sub     si, si                  ; i
irt_loop1:
    mov     bx, si
    shl     bx, 1                   ; word index
    mov     di, 1Dh
    sub     di, si                  ; 29 - i
    mov     ax, 1Eh
    imul    di
    mov     trackrows[bx], ax
    mov     ax, 1Eh
    imul    si
    mov     terrainrows[bx], ax
    mov     cl, 0Ah
    mov     ax, di
    shl     ax, cl                  ; (29 - i) << 10
    mov     trackpos[bx], ax
    add     ah, 2                   ; + 0x200
    mov     trackcenterpos[bx], ax
    mov     ax, si
    shl     ax, cl                  ; i << 10
    mov     terrainpos[bx], ax
    add     ah, 2                   ; + 0x200
    mov     terraincenterpos[bx], ax
    inc     si
    cmp     si, 1Eh
    jl      short irt_loop1

    sub     si, si
irt_loop2:
    mov     bx, si
    shl     bx, 1
    mov     cl, 0Ah
    mov     ax, si
    shl     ax, cl                  ; i << 10
    mov     trackpos2[bx], ax
    add     ah, 2                   ; + 0x200
    mov     trackcenterpos2[bx], ax
    inc     si
    cmp     si, 1Eh
    jl      short irt_loop2

    pop     di
    pop     si
    ret
init_row_tables endp

;-----------------------------------------------------------------------------
; void init_trackdata(void)                      [repldump.c init_trackdata()]
; Allocates one 0x6BF3-byte "trakdata" block and carves the 23 track-data
; pointers out of it.  Borland far-pointer arithmetic does NOT normalise, so
; only the offset is advanced and the segment (DX) stays constant throughout --
; this reproduces that exactly, as the original in seg000.asm does.
;-----------------------------------------------------------------------------
init_trackdata proc near
    push    si
    mov     si, 6BF3h               ; bytes to allocate
    mov     ax, si
    cwd
    push    dx
    push    ax                      ; long size
    mov     ax, offset aTrakdata    ; "trakdata"
    push    ax
    call    mmgr_alloc_resbytes
    add     sp, 6
    mov     si, ax                  ; trkptr offset ; DX = trkptr segment

    mov     word ptr td01_track_file_cpy, si
    mov     word ptr td01_track_file_cpy+2, dx
    add     si, 70Ah
    mov     word ptr td02_penalty_related, si
    mov     word ptr td02_penalty_related+2, dx
    add     si, 70Ah
    mov     word ptr trackdata3, si
    mov     word ptr trackdata3+2, dx
    add     si, 70Ah
    mov     word ptr td04_aerotable_pl, si
    mov     word ptr td04_aerotable_pl+2, dx
    add     si, 80h
    mov     word ptr td05_aerotable_op, si
    mov     word ptr td05_aerotable_op+2, dx
    add     si, 80h
    mov     word ptr trackdata6, si
    mov     word ptr trackdata6+2, dx
    add     si, 80h
    mov     word ptr trackdata7, si
    mov     word ptr trackdata7+2, dx
    add     si, 80h
    mov     word ptr td08_direction_related, si
    mov     word ptr td08_direction_related+2, dx
    add     si, 60h
    mov     word ptr trackdata9, si
    mov     word ptr trackdata9+2, dx
    add     si, 180h
    mov     word ptr td10_track_check_rel, si
    mov     word ptr td10_track_check_rel+2, dx
    add     si, 120h
    mov     word ptr td11_highscores, si
    mov     word ptr td11_highscores+2, dx
    add     si, 16Ch
    mov     word ptr trackdata12, si
    mov     word ptr trackdata12+2, dx
    add     si, 0F0h
    mov     word ptr td13_rpl_header, si
    mov     word ptr td13_rpl_header+2, dx
    add     si, 1Ah
    mov     word ptr td14_elem_map_main, si
    mov     word ptr td14_elem_map_main+2, dx
    add     si, 385h
    mov     word ptr td15_terr_map_main, si
    mov     word ptr td15_terr_map_main+2, dx
    add     si, 385h
    mov     word ptr td16_rpl_buffer, si
    mov     word ptr td16_rpl_buffer+2, dx
    add     si, 2EE0h
    mov     word ptr td17_trk_elem_ordered, si
    mov     word ptr td17_trk_elem_ordered+2, dx
    add     si, 385h
    mov     word ptr trackdata18, si
    mov     word ptr trackdata18+2, dx
    add     si, 385h
    mov     word ptr trackdata19, si
    mov     word ptr trackdata19+2, dx
    add     si, 385h
    mov     word ptr td20_trk_file_appnd, si
    mov     word ptr td20_trk_file_appnd+2, dx
    add     si, 7ACh
    mov     word ptr td21_col_from_path, si
    mov     word ptr td21_col_from_path+2, dx
    add     si, 385h
    mov     word ptr td22_row_from_path, si
    mov     word ptr td22_row_from_path+2, dx
    add     si, 385h
    mov     word ptr trackdata23, si
    mov     word ptr trackdata23+2, dx
    pop     si
    ret
init_trackdata endp

;=============================================================================
; int far stuntsmain(int argc, char* argv[])           [repldump.c stuntsmain()]
;=============================================================================
stuntsmain proc far
    push    bp
    mov     bp, sp
    sub     sp, V_FRAMESIZE
    push    di
    push    si

    ; if (argc != 2) return 1;
    cmp     word ptr [bp+6], 2
    je      short sm_args_ok
    mov     ax, 1
    jmp     sm_return
sm_args_ok:

    ; init_main(argc, argv);
    push    word ptr [bp+8]
    push    word ptr [bp+6]
    call    init_main
    add     sp, 4

    ; init_div0();
    call    init_div0

    ; init_row_tables();
    call    init_row_tables

    ; mainresptr = file_load_resfile("main");
    mov     ax, offset aMain
    push    ax
    call    file_load_resfile
    add     sp, 2
    mov     word ptr mainresptr, ax
    mov     word ptr mainresptr+2, dx

    ; fontdefptr = file_load_resource(0, "fontdef.fnt");
    mov     ax, offset aFontdef_fnt
    push    ax
    sub     ax, ax
    push    ax
    call    file_load_resource
    add     sp, 4
    mov     word ptr fontdefptr, ax
    mov     word ptr fontdefptr+2, dx

    ; fontnptr = file_load_resource(0, "fontn.fnt");
    mov     ax, offset aFontn_fnt
    push    ax
    sub     ax, ax
    push    ax
    call    file_load_resource
    add     sp, 4
    mov     word ptr fontnptr, ax
    mov     word ptr fontnptr+2, dx

    ; font_set_fontdef();  init_polyinfo();  init_trackdata();  init_unknown();
    call    font_set_fontdef
    call    init_polyinfo
    call    init_trackdata
    call    init_unknown

    ; init_kevinrandom("kevin");
    mov     ax, offset aKevin
    push    ax
    call    init_kevinrandom
    add     sp, 2

    ; file_load_replay("", argv[1]);
    ;   The "" is built in a stack local.  (file_build_path treats a NULL dir
    ;   and an empty dir identically -- see seg008.asm file_build_path -- so
    ;   this matches the original run_game() which passes NULL.)
    mov     byte ptr [bp+V_STRBUF], 0
    mov     bx, word ptr [bp+8]         ; argv
    push    word ptr [bx+2]             ; argv[1]
    lea     ax, [bp+V_STRBUF]
    push    ax                          ; ""
    call    file_load_replay
    add     sp, 4

    ; _memcpy(&gameconfigcopy, &gameconfig, sizeof(struct GAMEINFO));
    cld
    mov     di, offset gameconfigcopy
    mov     si, offset gameconfig
    push    ds
    pop     es
    mov     cx, GAMEINFO_WORDS
    rep movsw

    ; for (i = 0; i < 0x70A; i++)
    ;     td20_trk_file_appnd[i] = td14_elem_map_main[i];
    sub     si, si
sm_trkcopy1:
    les     bx, td14_elem_map_main
    mov     al, es:[bx+si]
    les     bx, td20_trk_file_appnd
    mov     es:[bx+si], al
    inc     si
    cmp     si, 70Ah
    jl      short sm_trkcopy1

    ; for (i = 0; i < 0x51; i++) {
    ;     td20_trk_file_appnd[i + 0x70A] = byte_3B80C[i];
    ;     td20_trk_file_appnd[i + 0x75B] = byte_3B85E[i];
    ; }
    sub     si, si
sm_trkcopy2:
    les     bx, td20_trk_file_appnd
    mov     al, byte_3B80C[si]
    mov     es:[bx+si+70Ah], al
    les     bx, td20_trk_file_appnd
    mov     al, byte_3B85E[si]
    mov     es:[bx+si+75Bh], al
    inc     si
    cmp     si, 51h
    jl      short sm_trkcopy2

    ; track_setup();
    call    track_setup

    ; --- dump the trakdata chunk that track_setup just filled ---------------
    ; File layout: the 8 dseg bytes at startcol2 (startcol2, hillFlag,
    ; word_4499C, startrow2, ...), then track_angle as a word, then the raw
    ; followed by the raw 0x6BF3 bytes starting at td01_track_file_cpy, which
    ; init_trackdata() set to the base of the "trakdata" allocation.
    ; The string literals live in the CODE segment, but dos_fopen reads its
    ; arguments through DS.  Copy both to stack scratch first, exactly as the
    ; outname/fopen sequence further down does.  V_OUTNAME and V_STRBUF are
    ; not populated until after this point, so borrowing them here is safe.
    mov     si, offset s_trakname
    lea     di, [bp+V_OUTNAME]
    call    cs_strcpy
    mov     si, offset s_wmode
    lea     di, [bp+V_STRBUF]
    call    cs_strcpy
    lea     ax, [bp+V_STRBUF]
    push    ax
    lea     ax, [bp+V_OUTNAME]
    push    ax
    call    dos_fopen
    add     sp, 4
    mov     [bp+V_TRAKF], ax
    or      ax, ax
    jz      short sm_trakskip

    push    word ptr [bp+V_TRAKF]
    mov     ax, 1
    push    ax
    mov     ax, 8
    push    ax
    push    ds
    mov     ax, offset startcol2
    push    ax
    call    dos_fwrite
    add     sp, 10

    ; track_angle lives far from the others in dseg, so write it separately.
    push    word ptr [bp+V_TRAKF]
    mov     ax, 1
    push    ax
    mov     ax, 2
    push    ax
    push    ds
    mov     ax, offset track_angle
    push    ax
    call    dos_fwrite
    add     sp, 10

    push    word ptr [bp+V_TRAKF]
    mov     ax, 1
    push    ax
    mov     ax, 6BF3h
    push    ax
    push    word ptr td01_track_file_cpy+2
    push    word ptr td01_track_file_cpy
    call    dos_fwrite
    add     sp, 10

    push    word ptr [bp+V_TRAKF]
    call    dos_fclose
    add     sp, 2
    mov     si, offset s_m_trak
    call    dos_stdout
sm_trakskip:

    ; cvxptr = mmgr_alloc_resbytes("cvx", 0x5780);
    mov     ax, 5780h
    cwd
    push    dx
    push    ax
    mov     ax, offset aCvx
    push    ax
    call    mmgr_alloc_resbytes
    add     sp, 6
    mov     word ptr cvxptr, ax
    mov     word ptr cvxptr+2, dx

    ; init_game_state(0xFFFF);
    mov     ax, 0FFFFh
    push    ax
    call    init_game_state
    add     sp, 2

    ; --- inits lifted from run_game() -------------------------------------
    mov     word_449EA, 0FFFFh
    call    get_kevinrandom
    mov     cl, 3
    shl     ax, cl
    mov     run_game_random, ax         ; run_game_random = get_kevinrandom()<<3
    mov     replaybar_toggle, 1
    mov     is_in_replay, 0
    mov     idle_expired, 0
    mov     cameramode, 0
    mov     game_replay_mode, 2
    mov     is_in_replay, 1

    ; setup_player_cars();
    call    setup_player_cars

    mov     kbormouse, 0
    mov     byte_449E6, 0
    mov     byte_449DA, 1

    ; set_frame_callback();
    call    set_frame_callback

    mov     game_replay_mode_copy, 0FFh
    mov     byte_44346, 0
    mov     byte_4432A, 0
    mov     byte_46467, 0
    mov     dashb_toggle, 0

    mov     cameramode, 0
    mov     game_replay_mode, 2
    mov     word_44DCA, 1F4h
    mov     framespersec, 14h           ; 20

    ; restore_gamestate(0);
    sub     ax, ax
    push    ax
    call    restore_gamestate
    add     sp, 2
    ; restore_gamestate(gameconfig.game_recordedframes);
    push    gameconfig.game_recordedframes
    call    restore_gamestate
    add     sp, 2

    ; strcpy(outname, argv[1]); strcat(outname, ".BIN"); outname[12] = 0;
    mov     bx, word ptr [bp+8]
    mov     si, word ptr [bx+2]         ; argv[1]
    lea     di, [bp+V_OUTNAME]
sm_namecpy:
    mov     al, [si]
    mov     [di], al
    inc     si
    inc     di
    or      al, al
    jnz     short sm_namecpy
    dec     di                          ; back onto the terminator
    mov     si, offset s_bin
    call    cs_strcpy
    mov     byte ptr [bp+V_OUTNAME+12], 0

    ; fout = fopen(outname, "w");
    mov     si, offset s_wmode
    lea     di, [bp+V_STRBUF]
    call    cs_strcpy
    lea     ax, [bp+V_STRBUF]
    push    ax                          ; mode
    lea     ax, [bp+V_OUTNAME]
    push    ax                          ; path
    call    dos_fopen
    add     sp, 4
    mov     word ptr [bp+V_FOUT], ax
    or      ax, ax
    jnz     short sm_file_ok
    mov     ax, 1                       ; if (!fout) return 1;
    jmp     sm_return
sm_file_ok:
    ; --- open GLOBALS.BIN ---------------------------------------------------
    ; Placed AFTER the label: the fout check jumps straight here on success,
    ; so anything before it is skipped.  V_OUTNAME is free to reuse now that
    ; the main dump file is already open.
    mov     si, offset s_globname
    lea     di, [bp+V_OUTNAME]
    call    cs_strcpy
    mov     si, offset s_wmode
    lea     di, [bp+V_STRBUF]
    call    cs_strcpy
    lea     ax, [bp+V_STRBUF]
    push    ax
    lea     ax, [bp+V_OUTNAME]
    push    ax
    call    dos_fopen
    add     sp, 4
    mov     [bp+V_GLOBF], ax

    ; fwrite(&gameconfig.game_recordedframes, sizeof(unsigned short), 1, fout);
    push    word ptr [bp+V_FOUT]
    mov     ax, 1
    push    ax
    mov     ax, 2
    push    ax
    push    ds
    mov     ax, offset gameconfig.game_recordedframes
    push    ax
    call    dos_fwrite
    add     sp, 10

    ; while (gameconfig.game_recordedframes > state.game_frame) {
    ;     input_do_checking(1);
    ;     update_gamestate();
    ;     fwrite(&state, sizeof(struct GAMESTATE), 1, fout);
    ; }
    ; (unsigned comparison: game_recordedframes is unsigned short, which
    ;  promotes to unsigned int, so the whole comparison is unsigned)
sm_frameloop:
    mov     ax, gameconfig.game_recordedframes
    cmp     ax, state.game_frame
    jbe     short sm_frameloop_end
    mov     ax, 1
    push    ax
    call    input_do_checking
    add     sp, 2
    call    update_gamestate
    push    word ptr [bp+V_FOUT]
    mov     ax, 1
    push    ax
    mov     ax, GAMESTATE_SIZE
    push    ax
    push    ds
    mov     ax, offset state
    push    ax
    call    dos_fwrite
    add     sp, 10

    ; --- snapshot build_track_object's outputs for this frame --------------
    cmp     word ptr [bp+V_GLOBF], 0
    jz      short sm_noglob
    mov     ax, terrainHeight
    mov     [bp+V_GLOB], ax
    mov     ax, planindex
    mov     [bp+V_GLOB+2], ax
    mov     ax, wallindex
    mov     [bp+V_GLOB+4], ax
    mov     al, current_surf_type
    xor     ah, ah
    mov     [bp+V_GLOB+6], ax
    push    word ptr [bp+V_GLOBF]
    mov     ax, 1
    push    ax
    mov     ax, 8
    push    ax
    push    ss
    lea     ax, [bp+V_GLOB]
    push    ax
    call    dos_fwrite
    add     sp, 10
sm_noglob:
    jmp     sm_frameloop
sm_frameloop_end:
    cmp     word ptr [bp+V_GLOBF], 0
    jz      short sm_noglobclose
    push    word ptr [bp+V_GLOBF]
    call    dos_fclose
    add     sp, 2
    mov     si, offset s_m_glob
    call    dos_stdout
sm_noglobclose:
    mov     si, offset s_m_frames
    call    dos_stdout

    ; fclose(fout);
    push    word ptr [bp+V_FOUT]
    call    dos_fclose
    add     sp, 2
    mov     si, offset s_m_closed
    call    dos_stdout

    ; input_do_checking(1);
    mov     ax, 1
    push    ax
    call    input_do_checking
    add     sp, 2
    mov     si, offset s_m_input
    call    dos_stdout

    ; fatal_error("\nDone.\n");
    ;
    ; NOTE: fatal_error (seg012.asm) is the game's fatal-error screen:
    ;   sprite_copy_2_to_1 / printf / flush_stdin / call_exitlist / printf /
    ;   abort.  "flush_stdin" is an IDA misnomer -- it is
    ;       call kb_call_readchar_callback / cmp ax,0 / jz <back to start> / retf
    ;   i.e. it spins WHILE no key is pending, so it WAITS FOR A KEYPRESS.
    ; In an unattended DOSBox batch run nobody presses a key, so repldumo.exe
    ; parks here forever.  That is original behaviour, not a defect of this
    ; module, and it happens strictly after the last fwrite and after fclose:
    ; the "file closed OK" marker above is emitted once the DOS handle has been
    ; closed, so the dump on disk is complete and safe to use from that moment.
    ; An automated runner should wait for that marker and then kill DOSBox.
    ; (Verified by open-coding fatal_error with markers: sprite_copy_2_to_1 and
    ;  printf both return, flush_stdin never does.)
    mov     si, offset s_done
    lea     di, [bp+V_STRBUF]
    call    cs_strcpy
    lea     ax, [bp+V_STRBUF]
    push    ax
    call    fatal_error
    add     sp, 2                       ; not reached (fatal_error never returns)

    ; return 0;
    sub     ax, ax
sm_return:
    pop     si
    pop     di
    mov     sp, bp
    pop     bp
    retf
stuntsmain endp

repldrv ends
    end
