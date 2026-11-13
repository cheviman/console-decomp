/**
 *    Name:        cartridge.h
 *    Description: the player's cartridge, read from a .zip, a ROM file or a folder and known by its CRC32,
 *                 for the tools that run the original code
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The game says which cartridge it is (its CRC32 and size); the name of the file does not matter. Only the
 *    reference and the extractor read it: the game itself never opens the ROM.
 */
#ifndef MD_CARTRIDGE_H
#define MD_CARTRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads into 'out' ('size' bytes) the cartridge whose CRC32 is 'crc' from 'source': a ROM file, a .zip
   holding it (stored or deflated), or a folder holding either. 1 when found, 0 when not */
int md_cartridge_load(const char *source, uint32_t crc, uint32_t size, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif
