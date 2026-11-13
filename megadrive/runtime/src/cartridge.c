/**
 *    Name:        cartridge.c
 *    Description: the player's cartridge from a .zip, a ROM file or a folder (md/cartridge.h)
 *    Version:     0.1.0
 *    Created:     13-11-2026
 *    Modified:    13-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The cartridge is known by the CRC32 and size the game gives: in a .zip by its entry, whatever its name
 *    (stored or deflated: inflate.c); as a file of any name. A folder is searched for either. The reference
 *    runs the original code, so it reads the cartridge when it starts, as MAME does.
 */
#include "md/cartridge.h"

#include "inflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

#define ZIP_END             0x06054B50u    /* the end of the central directory */
#define ZIP_ENTRY           0x02014B50u    /* a central directory entry */
#define ZIP_LOCAL           0x04034B50u    /* a local header */
#define ZIP_STORED          0
#define ZIP_DEFLATED        8
#define ZIP_COMMENT_MOST    0xFFFFu

/* The cartridge being looked for */
static uint32_t wanted_crc, wanted_size;
static uint8_t *wanted_out;

static uint32_t crc32_of(const uint8_t *data, size_t size)
{
	static uint32_t table[256];
	uint32_t crc = 0xFFFFFFFFu, index;
	size_t at;
	int bit;

	if (table[1] == 0u)
		for (index = 0; index < 256u; index++)
		{
			table[index] = index;
			for (bit = 0; bit < 8; bit++)
				table[index] = (table[index] >> 1) ^ (0xEDB88320u & (0u - (table[index] & 1u)));
		}
	for (at = 0; at < size; at++)
		crc = (crc >> 8) ^ table[(crc ^ data[at]) & 0xFFu];
	return ~crc;
}

static uint32_t little16(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
}

static uint32_t little32(const uint8_t *bytes)
{
	return little16(bytes) | little16(bytes + 2) << 16;
}

/* A whole file into memory (the caller frees it), or NULL */
static uint8_t *read_whole(const char *path, size_t *size)
{
	FILE *input = fopen(path, "rb");
	uint8_t *data = NULL;
	long length;

	if (!input)
		return NULL;
	if (fseek(input, 0, SEEK_END) == 0 && (length = ftell(input)) > 0 && fseek(input, 0, SEEK_SET) == 0
		&& (data = malloc((size_t)length)) != NULL && fread(data, 1, (size_t)length, input) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(input);
	if (data)
		*size = (size_t)length;
	return data;
}

/* The cartridge from the .zip 'zip' ('size' bytes) into wanted_out: 1 when found, 0 when not (or no .zip) */
static int read_zip(const uint8_t *zip, size_t size)
{
	size_t end, entry, local;
	uint32_t entries, index;

	if (size < 22)
		return 0;
	for (end = size - 22;; end--)
	{
		if (little32(zip + end) == ZIP_END)
			break;
		if (end == 0 || size - end > 22 + ZIP_COMMENT_MOST)
			return 0;
	}
	entries = little16(zip + end + 10);
	entry = little32(zip + end + 16);
	for (index = 0; index < entries; index++)
	{
		uint32_t method, crc, packed, unpacked;

		if (entry > size || size - entry < 46 || little32(zip + entry) != ZIP_ENTRY)
			return 0;
		method = little16(zip + entry + 10);
		crc = little32(zip + entry + 16);
		packed = little32(zip + entry + 20);
		unpacked = little32(zip + entry + 24);
		local = little32(zip + entry + 42);
		entry += 46 + little16(zip + entry + 28) + little16(zip + entry + 30) + little16(zip + entry + 32);
		if (crc != wanted_crc || unpacked != wanted_size || local > size || size - local < 30
			|| little32(zip + local) != ZIP_LOCAL)
			continue;
		local += 30 + little16(zip + local + 26) + little16(zip + local + 28);
		if (local > size || packed > size - local || (method == ZIP_STORED && packed != unpacked))
			continue;    /* (what the entry says does not fit the file) */
		if ((method == ZIP_STORED ? (memcpy(wanted_out, zip + local, unpacked), 1)
				: method == ZIP_DEFLATED && inflate_raw(zip + local, packed, wanted_out, unpacked))
			&& crc32_of(wanted_out, unpacked) == wanted_crc)
			return 1;
	}
	return 0;
}

/* The cartridge from the file 'path' (a ROM or a .zip): 1 when it is there */
static int read_file(const char *path)
{
	size_t size = 0;
	uint8_t *data = read_whole(path, &size);
	int found = 0;

	if (!data)
		return 0;
	if (size == wanted_size && crc32_of(data, size) == wanted_crc)
	{
		memcpy(wanted_out, data, wanted_size);
		found = 1;
	}
	else
		found = read_zip(data, size);
	free(data);
	return found;
}

/* The cartridge from any file of the folder 'folder': 1 when one of them is it */
static int read_folder(const char *folder)
{
	char path[1024];
	int found = 0;
#ifdef _WIN32
	struct _finddata_t entry;
	intptr_t search;

	snprintf(path, sizeof(path), "%s/*", folder);
	search = _findfirst(path, &entry);
	if (search == -1)
		return 0;
	do
	{
		if (entry.attrib & _A_SUBDIR)
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, entry.name);
		found = read_file(path);
	} while (!found && _findnext(search, &entry) == 0);
	_findclose(search);
#else
	DIR *directory = opendir(folder);
	struct dirent *entry;

	if (!directory)
		return 0;
	while (!found && (entry = readdir(directory)) != NULL)
	{
		if (entry->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
		found = read_file(path);
	}
	closedir(directory);
#endif
	return found;
}

int md_cartridge_load(const char *source, uint32_t crc, uint32_t size, uint8_t *out)
{
	wanted_crc = crc;
	wanted_size = size;
	wanted_out = out;
	return read_file(source) || read_folder(source);
}
