/* music_tool.c - drives src/music_native.c without a sound device.
 *
 *   music_tool --data DIR --dump-events SONG
 *   music_tool --data DIR --dump-opl    SONG SECONDS
 *   music_tool --data DIR --wav         SONG SECONDS OUT.wav
 *   music_tool --data DIR --summary
 *
 * SONG is title | select | over | victory (or 0..3).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/music_native.h"

static int song_from_name(const char *s)
{
    if (!strcmp(s, "title")   || !strcmp(s, "0")) return MUSIC_SONG_TITLE;
    if (!strcmp(s, "select")  || !strcmp(s, "1")) return MUSIC_SONG_SELECT;
    if (!strcmp(s, "over")    || !strcmp(s, "2")) return MUSIC_SONG_GAMEOVER;
    if (!strcmp(s, "victory") || !strcmp(s, "3")) return MUSIC_SONG_VICTORY;
    return -1;
}

int main(int argc, char **argv)
{
    const char *data = "extracted/stunts/stunts";
    int i, rate = 44100;

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) rate = atoi(argv[++i]);

    if (music_native_init(data, rate) != 0) {
        fprintf(stderr, "music_native_init failed (data dir '%s')\n", data);
        return 1;
    }
    fprintf(stderr, "OPL core: %s (real=%d), %d Hz\n",
            music_native_opl_core(), music_native_opl_is_real(), rate);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump-events") && i + 1 < argc) {
            int s = song_from_name(argv[++i]);
            if (s < 0 || music_native_dump_events((music_song_t)s, stdout))
                { fprintf(stderr, "dump-events failed\n"); return 1; }
        } else if (!strcmp(argv[i], "--dump-opl") && i + 2 < argc) {
            int s = song_from_name(argv[i + 1]);
            double sec = atof(argv[i + 2]); i += 2;
            if (s < 0 || music_native_dump_opl((music_song_t)s, sec, stdout))
                { fprintf(stderr, "dump-opl failed\n"); return 1; }
        } else if (!strcmp(argv[i], "--wav") && i + 3 < argc) {
            int s = song_from_name(argv[i + 1]);
            double sec = atof(argv[i + 2]);
            const char *out = argv[i + 3]; i += 3;
            if (s < 0 || music_native_render_wav((music_song_t)s, sec, out))
                { fprintf(stderr, "wav failed\n"); return 1; }
            fprintf(stderr, "wrote %s (%.1f s)\n", out, sec);
        } else if (!strcmp(argv[i], "--summary")) {
            int s;
            for (s = 0; s < MUSIC_SONG_COUNT; s++)
                music_native_dump_events((music_song_t)s, stdout);
        }
    }
    music_native_shutdown();
    return 0;
}
