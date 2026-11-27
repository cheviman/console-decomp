/**
 *    Name:        script.h
 *    Description: scripted controls for test runs: the console's buttons and memory pokes, applied when VBLANK
 *                 starts in the frames a text script names
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *    Modified:    26-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    A run that applies the same script at the same frame boundaries sees the same inputs, so the game and
 *    tools/console/ngp/reference/ngpref stay comparable frame by frame. The format is the Mega Drive's
 *    (megadrive/runtime/include/md/script.h), with this console's controls.
 *
 *    Script format, one event per line, '#' starts a comment:
 *      FRAME ACTION [ARGUMENTS]        FRAME is a frame number (the VBLANKs before it), or +N for N frames
 *                                      after the previous event
 *      press INPUT [FRAMES]            pressed for FRAMES frames (4 by default), then released
 *      hold INPUT / release INPUT      pressed / released until another event changes it
 *      poke ADDRESS VALUE              writes a byte of the RAM (hexadecimal address 004000-007FFF and value)
 *      end                             the run ends with this frame
 *    INPUT: up down left right a b option power
 */
#ifndef NGP_SCRIPT_H
#define NGP_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The controls, 1 = held: the pad's bits as the console reads them at 0000B0, and the power button */
#define NGP_UP      0x01
#define NGP_DOWN    0x02
#define NGP_LEFT    0x04
#define NGP_RIGHT   0x08
#define NGP_A       0x10
#define NGP_B       0x20
#define NGP_OPTION  0x40
#define NGP_POWER   0x80     /* not a bit of 0000B0: the console sees it as its power button (an NMI) */

typedef struct ngp_script ngp_script;

/* Writes 'value' at 'address' (004000-007FFF) of the console that runs the script */
typedef void (*ngp_script_poke)(void *context, uint32_t address, uint8_t value);

/* NULL if the script cannot be read or parsed; 'error' then says what and where */
ngp_script *ngp_script_load(const char *path, char *error, size_t error_size);
void ngp_script_free(ngp_script *script);

/* Applies what is due in 'frame' to 'controls' (NGP_UP...) and through 'poke': call it when the frame's VBLANK
   starts, the moment the program reads the pad, with the VBLANKs so far (frame 0 before the program starts) */
void ngp_script_apply(ngp_script *script, uint64_t frame, uint8_t *controls, ngp_script_poke poke, void *context);

/* Frames the script asks to run ('end'), or 0 if it does not say */
uint64_t ngp_script_frames(const ngp_script *script);

#ifdef __cplusplus
}
#endif

#endif
