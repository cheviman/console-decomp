/**
 *    Name:        script.h
 *    Description: scripted controls for test runs: the two pads' buttons and memory pokes, applied
 *                 when VBLANK starts in the frames a text script names
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    A run that applies the same script at the same frame boundaries sees the same inputs, so the game and
 *    tools/core/megadrive/reference/mdref stay comparable frame by frame. The format is TMNT's and
 *    OutRunners', with this board's controls.
 *
 *    Script format, one event per line, '#' starts a comment:
 *      FRAME ACTION [ARGUMENTS]        FRAME is a frame number (the VBLANKs before it), or +N for N frames
 *                                      after the previous event
 *      press INPUT [FRAMES]            pressed for FRAMES frames (4 by default), then released
 *      hold INPUT / release INPUT      pressed / released until another event changes it
 *      poke ADDRESS VALUE              writes a byte of the work RAM (hexadecimal address E00000-FFFFFF
 *                                      and value)
 *      end                             the run ends with this frame
 *    INPUT: for pads 1-2 upN downN leftN rightN aN bN cN startN
 */
#ifndef MD_SCRIPT_H
#define MD_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct md_script md_script;

/* NULL if the script cannot be read or parsed; 'error' then says what and where */
md_script *md_script_load(const char *path, char *error, size_t error_size);
void md_script_free(md_script *script);

/* Applies what is due in 'frame': call it when the frame's VBLANK starts, the moment the program reads the
   pads, with the VBLANKs so far (frame 0 before the program starts) */
void md_script_apply(md_script *script, uint64_t frame);

/* Frames the script asks to run ('end'), or 0 if it does not say */
uint64_t md_script_frames(const md_script *script);

#ifdef __cplusplus
}
#endif

#endif
