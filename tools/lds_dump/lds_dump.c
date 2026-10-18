/*
 * lds_dump — run OpenTyrian's Loudness (LDS) player on one song of
 * music.mus and log every OPL2 register write with its tick number.
 * Optionally renders the same run through OpenTyrian's OPL emulator
 * (opl.c, DOSBox) to a 44100 Hz mono 16-bit WAV for listening checks.
 *
 *   lds_dump music.mus SONG out.log [out.wav]      (SONG is 0-based, 0..40)
 *
 * Build (Makefile rule `out/lds_dump`, or by hand):
 *   cc -O2 -std=c99 $(sdl2-config --cflags) -Iopen-tyrian/src \
 *      tools/lds_dump/lds_dump.c open-tyrian/src/opl.c \
 *      open-tyrian/src/memreader.c -lm -o out/lds_dump
 * Only SDL's headers are needed (types); nothing links against SDL.
 *
 * lds_play.c is #included so the dump can see the player's static state
 * (posplay, pattplay, tempo_now, jumppos) to find the loop exactly.
 *
 * Timing: loudness.c:audioCallback calls lds_update() at 69.5 Hz
 * (ldsUpdate2Rate = 139 half-updates per second; the DOS game ran it from
 * the ~69.57 Hz game timer).  Tick n starts at sample floor(n*88200/139)
 * at 44100 Hz, the same integer accumulation as loudness.c.
 *
 * End of song: the player stops on command 0xFC (playing = false), or
 * sets songlooped on a backward 0xF9 jump.  On a loop the log ends at the
 * tick that would process row 0 of the jump target again, and the loop
 * start is the tick that first processed row 0 of that position.  Without
 * either, the run stops after MAX_SECONDS.
 *
 * Log format (text, one record per line, numbers in decimal unless noted):
 *   # comment lines
 *   R <ticks_per_second_x2>            always "R 139" (69.5 Hz)
 *   W <tick> <reg hex> <val hex>       OPL2 write in tick <tick>
 *   E <end_tick> <loop_tick|-1>        last line: song length in ticks
 *                                      (writes have tick < end_tick) and
 *                                      the loop start tick (-1 = none)
 * Writes from lds_rewind (chip init) carry tick 0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "opl.h"

int audioSampleRate = 44100;

static FILE *log_f;
static long cur_tick;
static int render;

/* lds_play.c calls opl_write (opl.h macro -> adlib_write); route it here. */
static void dump_write(unsigned reg, unsigned val)
{
	fprintf(log_f, "W %ld %02x %02x\n", cur_tick, reg & 0xff, val & 0xff);
	if (render)
		adlib_write(reg, (Bit8u)val);
}
#undef opl_write
#define opl_write(reg, val) dump_write((reg), (val))
#undef opl_init
#define opl_init() do { if (render) adlib_init(audioSampleRate); } while (0)

#include "lds_play.c"

#define MAX_SECONDS 360
#define LDS_RATE2 139

static void wav_u32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void wav_u16(FILE *f, uint16_t v) { fputc(v, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s music.mus SONG out.log [out.wav]\n", argv[0]);
		return 2;
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *data = malloc(fsize);
	if (fread(data, 1, fsize, f) != (size_t)fsize) { fprintf(stderr, "read error\n"); return 1; }
	fclose(f);

	/* offset table: u16 count; u32 offset[count]; last song ends at EOF
	   (loudness.c:load_song) */
	int count = data[0] | data[1] << 8, song = atoi(argv[2]);
	if (song < 0 || song >= count) { fprintf(stderr, "song %d out of 0..%d\n", song, count - 1); return 2; }
	#define OFS(i) ((uint32_t)data[2 + 4 * (i)] | (uint32_t)data[3 + 4 * (i)] << 8 | \
	                (uint32_t)data[4 + 4 * (i)] << 16 | (uint32_t)data[5 + 4 * (i)] << 24)
	uint32_t start = OFS(song), end = song + 1 < count ? OFS(song + 1) : (uint32_t)fsize;

	log_f = fopen(argv[3], "w");
	if (!log_f) { perror(argv[3]); return 1; }
	FILE *wav = NULL;
	if (argc > 4) {
		render = 1;
		wav = fopen(argv[4], "wb");
		if (!wav) { perror(argv[4]); return 1; }
		fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, wav);
		wav_u32(wav, 16); wav_u16(wav, 1); wav_u16(wav, 1);
		wav_u32(wav, 44100); wav_u32(wav, 88200); wav_u16(wav, 2); wav_u16(wav, 16);
		fwrite("data\0\0\0\0", 1, 8, wav);
	}

	fprintf(log_f, "# lds_dump song %d of %s (OPL2 writes per LDS tick)\nR %d\n", song, argv[1], LDS_RATE2);
	cur_tick = 0;
	if (!lds_load(data + start, end - start)) { fprintf(stderr, "lds_load failed\n"); return 1; }

	long first_row0[256];
	for (int i = 0; i < 256; i++) first_row0[i] = -1;
	long loop_tick = -1, max_ticks = (long)MAX_SECONDS * LDS_RATE2 / 2;
	uint64_t samples_done = 0;
	Bit16s buf[1024];

	for (cur_tick = 0; cur_tick < max_ticks; cur_tick++) {
		if (!playing)
			break;
		if (tempo_now == 0 && pattplay == 0) {   /* this tick processes row 0 of posplay */
			if (songlooped) {
				loop_tick = first_row0[posplay];
				if (loop_tick >= 0)
					break;
				songlooped = false;               /* jump target never started: keep going */
			}
			if (first_row0[posplay] < 0)
				first_row0[posplay] = cur_tick;
		}
		lds_update();
		if (render) {
			uint64_t until = (uint64_t)(cur_tick + 1) * 88200 / LDS_RATE2;
			while (samples_done < until) {
				int n = (int)(until - samples_done > 1024 ? 1024 : until - samples_done);
				adlib_getsample(buf, n);
				for (int i = 0; i < n; i++) wav_u16(wav, (uint16_t)buf[i]);
				samples_done += n;
			}
		}
	}
	fprintf(log_f, "E %ld %ld\n", cur_tick, loop_tick);
	fclose(log_f);
	if (wav) {
		uint32_t bytes = (uint32_t)(samples_done * 2);
		fseek(wav, 4, SEEK_SET); wav_u32(wav, 36 + bytes);
		fseek(wav, 40, SEEK_SET); wav_u32(wav, bytes);
		fclose(wav);
	}
	lds_free();
	free(data);
	return 0;
}
