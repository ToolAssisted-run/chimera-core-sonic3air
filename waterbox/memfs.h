/* memfs.h - the machine's files: every file Sonic 3 A.I.R. reads or writes is
 * here, in the machine's memory, and nowhere on the host.
 *
 * rmxbase's FileIO (patches/0002) and the C library's fopen (the link wraps
 * it) both come here. A file is either one the core put in place at Init -
 * the data the core carries, the ROM, the mods' zips, the soundtrack's files,
 * the project's save data - or one the game wrote. Paths are kept
 * normalized: '/' between parts, no leading "./" or '/', no "." or ".."
 * parts, and compared exactly (Linux's way, on every host).
 */
#ifndef S3AIR_MEMFS_H
#define S3AIR_MEMFS_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

namespace memfs
{
	std::wstring normalize(std::wstring_view path);
	std::wstring widen(std::string_view utf8);
	std::string narrow(std::wstring_view path);

	/* a file whose bytes stay where they are (sealed memory, the core's
	 * read-only data): copied only if the game writes it */
	void addExternal(std::wstring_view path, const uint8_t *data, size_t size);
	/* a file whose bytes the machine owns */
	void addOwned(std::wstring_view path, std::vector<uint8_t> &&data);
	void addDirectory(std::wstring_view path);

	bool exists(std::wstring_view path);
	const uint8_t *data(std::wstring_view path, size_t *size);

	/* the files the game wrote (or that were seeded as written: the save
	 * data), under a folder, in path order */
	void writtenFiles(std::wstring_view folder, std::vector<std::wstring> &outPaths);
	/* how many times any file was written, for the tests */
	uint64_t writeCount();
}

#endif
