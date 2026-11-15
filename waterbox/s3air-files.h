/* s3air-files.h - what the machine's files are made of at Init (s3air-files.cpp) */
#ifndef S3AIR_FILES_H
#define S3AIR_FILES_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

/* the save data folder (the engine's app data path, "savedata/" in the
 * machine's files) and the ROM's name in it, where upstream keeps its copy */
#define S3_SAVEDATA L"savedata"
#define S3_ROM_ID "Sonic_Knuckles_wSonic3.bin"

/* 0 on failure, with the reason in err (a key of languages/en.json and its
 * arguments) */
int s3files_load(char *err, int errsize);

/* the mods, as the project has them: each zip's name and the mod folders
 * found in it, in the slot's order (s3files_load) */
struct S3Mod
{
	std::string zip;
	std::vector<std::string> folders;
};
const std::vector<S3Mod> &s3files_mods(void);
/* how many soundtrack files the project gave */
int s3files_soundtrack_count(void);

/* a zip's entries (stored or deflated), read from memory */
struct S3ZipEntry
{
	std::string name;
	uint32_t method, compressedSize, size, localOffset;
};
bool s3zip_list(const uint8_t *zip, size_t size, std::vector<S3ZipEntry> &out);
bool s3zip_extract(const uint8_t *zip, size_t size, const S3ZipEntry &e, std::vector<uint8_t> &out);

#endif
