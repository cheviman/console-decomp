/**
 *    Name:        headless.c
 *    Description: runs a game's translated program with no screen or sound, with the controls a script drives, and writes hashes
 *                 of every frame in the formats mdref writes (tools/console/megadrive/reference)
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *    Modified:    20-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Usage: NAME_headless [--frames N] [--script FILE] [--rom FOLDER] [--hashes FILE] [--video-hashes FILE]
 *    The game's data is the cartridge in --rom (or 'rom'), read into the board's memory by game/extract.h.
 *    The board runs with frame timing (md/frames.h), the script is applied when each VBLANK starts, and the
 *    hashes are mdref's (the frame's state with --hashes, the frame's pixels with --video-hashes), so the two
 *    compare line by line.
 */
#include "game/data.h"
#include "game/extract.h"
#include "md/frames.h"
#include "md/machine.h"
#include "md/program.h"
#include "md/script.h"
#include "md/vdp.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USAGE "usage: NAME_headless [--frames N] [--script FILE] [--rom FOLDER] [--hashes FILE] [--video-hashes FILE]\n"

static const char *hashes_path;
static const char *video_hashes_path;
static FILE *hashes;
static FILE *video_hashes;
static md_script *script;
static uint64_t frames;
static bool frames_given;
static bool video;
static uint32_t picture[MD_VDP_WIDTH * MD_VDP_HEIGHT];

/* The picture's bytes as FNV-1a 64, as mdref's picture_hash */
static uint64_t picture_hash(void)
{
	uint64_t hash = 0xcbf29ce484222325ULL;
	const uint8_t *bytes = (const uint8_t *)picture;
	const size_t size = sizeof(picture);

	for (size_t index = 0; index < size; index++)
	{
		hash ^= bytes[index];
		hash *= 0x100000001b3ULL;
	}
	return hash;
}

/* A line begins: the picture's line if video is on */
static void on_line(unsigned line)
{
	if (video && line < MD_VDP_HEIGHT)
		md_vdp_render_line(&md_board.vdp, line, &picture[line * MD_VDP_WIDTH]);
}

/* VBLANK begins: the frame is done (its lines written out at once, so that a run the program never ends, or
   ends with exit(3), keeps every frame it made) */
static void on_frame_done(void)
{
	const uint64_t frame = md_board.frame;

	md_script_apply(script, frame);
	if (hashes)
	{
		fprintf(hashes, "%llu %016llx\n", (unsigned long long)frame, (unsigned long long)md_state_hash());
		fflush(hashes);
	}
	if (video_hashes)
	{
		fprintf(video_hashes, "%llu %016llx\n", (unsigned long long)frame, (unsigned long long)picture_hash());
		fflush(video_hashes);
	}
	if (frame >= frames)
		program_stop();
}

int main(int argc, char **argv)
{
	const char *rom = "rom";
	const char *script_path = NULL;

	for (int index = 1; index < argc; index++)
	{
		if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc)
		{
			frames = strtoull(argv[++index], NULL, 10);
			frames_given = true;
		}
		else if (strcmp(argv[index], "--script") == 0 && index + 1 < argc)
			script_path = argv[++index];
		else if (strcmp(argv[index], "--rom") == 0 && index + 1 < argc)
			rom = argv[++index];
		else if (strcmp(argv[index], "--hashes") == 0 && index + 1 < argc)
			hashes_path = argv[++index];
		else if (strcmp(argv[index], "--video-hashes") == 0 && index + 1 < argc)
			video_hashes_path = argv[++index];
		else
		{
			fprintf(stderr, USAGE);
			return 1;
		}
	}
	{
		const char *problem = game_rom_set_load(rom);

		if (problem)
		{
			fprintf(stderr, "The cartridge in %s cannot be read: %s\n", rom, problem);
			return 1;
		}
	}
	md_insert(game_program, GAME_PROGRAM_SIZE);
	md_board.hooks.now = md_frames_now;
	md_power_on();
	md_board.refresh = 0;
	md_frames_reset();
	if (script_path)
	{
		char error[256];

		script = md_script_load(script_path, error, sizeof(error));
		if (!script)
		{
			fprintf(stderr, "%s\n", error);
			return 1;
		}
		if (!frames_given && md_script_frames(script))
			frames = md_script_frames(script);
		md_script_apply(script, 0);
	}
	if (!frames)
	{
		fprintf(stderr, "no frames to run (--frames or a script's end)\n");
		return 1;
	}
	video = video_hashes_path != NULL;
	if (hashes_path)
	{
		hashes = fopen(hashes_path, "w");
		if (!hashes)
		{
			fprintf(stderr, "%s: cannot be written\n", hashes_path);
			return 1;
		}
	}
	if (video_hashes_path)
	{
		video_hashes = fopen(video_hashes_path, "w");
		if (!video_hashes)
		{
			fprintf(stderr, "%s: cannot be written\n", video_hashes_path);
			return 1;
		}
	}
	program_line = on_line;
	program_frame_done = on_frame_done;
	program_reset();
	program_run();
	if (hashes)
		fclose(hashes);
	if (video_hashes)
		fclose(video_hashes);
	md_script_free(script);
	printf("frames:            %llu\n", (unsigned long long)program_frames());
	printf("interrupts:        %u\n", program_interrupts());
	return 0;
}