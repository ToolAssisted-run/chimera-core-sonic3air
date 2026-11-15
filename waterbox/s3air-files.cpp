/* s3air-files.cpp - the machine's files at power-on, and the save data out.
 *
 * At Init, before the machine is sealed (so all of it is the sealed
 * baseline, and a savestate carries only what changes later), the core puts
 * in the machine's files (memfs.cpp):
 *
 *   - the data the core carries: upstream's game data (data/, without the
 *     remastered soundtrack), its engine data (merged under data/, as its
 *     enginedata.bin package is) and its scripts (scripts/), linked into
 *     the core as one blob (data-pack.py) and read where they are;
 *   - the ROM, the project's firmware, where upstream keeps its own copy
 *     once it has found one (savedata/Sonic_Knuckles_wSonic3.bin), checked
 *     first as upstream checks it, so a wrong file is refused with a reason;
 *   - the mods: the project's "mods" slot, zips of upstream's mod folders,
 *     put in upstream's mods folder (savedata/mods/), and a list of the
 *     active mods (savedata/mods/active-mods.json) naming every mod folder
 *     found in them, zip by zip in the slot's order. Upstream's own mod
 *     manager then finds them, mounts the zips and loads the mods;
 *   - the remastered soundtrack: the project's "soundtrack" slot, either
 *     upstream's package audioremaster.bin (data/audioremaster.bin, where
 *     the game loads it from) or the files of its data/audio/remastered
 *     folder (put there);
 *   - the save data: the project's "savedata" slot, a zip as Export Save
 *     Data writes it, unpacked into savedata/.
 *
 * Every file is read from the sandbox's mount once, at Init, and the bytes
 * of the large ones (the ROM, the zips, the soundtrack) are kept in sealed
 * memory: read-only from then on, and in no savestate.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <string>
#include <vector>

#include <zlib.h>

#include "rmxbase.h"

#include <emulibc.h>
#include <waterbox_slots.h>

#include "memfs.h"
#include "s3air-driver.h"
#include "s3air-files.h"

#define TEXT_ARG "\x1f"

extern "C" FILE *__real_fopen(const char *path, const char *mode);
extern "C" int __real_fclose(FILE *f);

/* the data the core carries (data.S, data-pack.py) */
extern "C" const uint8_t s3air_data[];
extern "C" const uint8_t s3air_data_end[];


namespace
{
	std::vector<S3Mod> g_mods;
	int g_soundtrack = 0;

	/* a mounted file, read whole into sealed memory (or the heap) */
	const uint8_t *read_mounted(const char *name, size_t *size, bool sealed)
	{
		FILE *f = __real_fopen(name, "rb");
		if (!f) return nullptr;
		fseek(f, 0, SEEK_END);
		const long n = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (n < 0) { __real_fclose(f); return nullptr; }
		uint8_t *buf = (uint8_t *)(sealed ? alloc_sealed((size_t)n + 1) : malloc((size_t)n + 1));
		if (!buf) { __real_fclose(f); return nullptr; }
		const size_t got = fread(buf, 1, (size_t)n, f);
		__real_fclose(f);
		if (got != (size_t)n) return nullptr;
		*size = (size_t)n;
		return buf;
	}

	uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
	uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
	uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

	bool ends_with(const std::string &s, const char *suffix)
	{
		const size_t n = strlen(suffix);
		return s.size() >= n && !strcasecmp(s.c_str() + s.size() - n, suffix);
	}

	/* the blob: "S3AIRDAT", a count, then (path length, path, offset, size)
	 * each, offsets from the blob's start */
	bool load_blob(char *err, int errsize)
	{
		const uint8_t *b = s3air_data;
		const size_t total = (size_t)(s3air_data_end - s3air_data);
		if (total < 12 || memcmp(b, "S3AIRDAT", 8) != 0)
		{
			snprintf(err, (size_t)errsize, "error.core_data_missing");
			return false;
		}
		const uint32_t count = rd32(b + 8);
		const uint8_t *p = b + 12;
		for (uint32_t i = 0; i < count; ++i)
		{
			const uint32_t len = rd32(p);
			const std::string path((const char *)p + 4, len);
			p += 4 + len;
			const uint64_t off = rd64(p), size = rd64(p + 8);
			p += 16;
			if (off + size > total)
			{
				snprintf(err, (size_t)errsize, "error.core_data_missing");
				return false;
			}
			memfs::addExternal(memfs::widen(path), b + off, (size_t)size);
		}
		return true;
	}

	/* the ROM, as upstream checks it (ResourcesCache::checkRomContent): 4 MiB,
	 * byte 0x2001f0 set to 0x4a, and then its checksum */
	bool load_rom(char *err, int errsize)
	{
		size_t size = 0;
		const uint8_t *rom = read_mounted(S3_ROM_ID, &size, true);
		if (!rom)
		{
			snprintf(err, (size_t)errsize, "error.rom_missing");
			return false;
		}
		if (size != 0x400000)
		{
			snprintf(err, (size_t)errsize, "error.rom_wrong_size" TEXT_ARG "%zu", size);
			return false;
		}
		std::vector<uint8_t> check(rom, rom + size);
		check[0x2001f0] = 0x4a;
		if (rmx::getMurmur2_64(check.data(), check.size()) != 0x344983ffcfeff8cbull)
		{
			snprintf(err, (size_t)errsize, "error.rom_not_s3k");
			return false;
		}
		memfs::addExternal(std::wstring(S3_SAVEDATA) + L"/" + memfs::widen(S3_ROM_ID), rom, size);
		return true;
	}

	/* the mods slot: each zip into upstream's mods folder, and the mod folders
	 * in it (a folder holding mod.json; not one inside another mod, nor in a
	 * folder whose name starts with '#', as upstream's ModManager scans) */
	bool load_mods(char *err, int errsize)
	{
		char name[512];
		for (int i = 0; wbx_slot_name("mods", i, name, (int)sizeof name); ++i)
		{
			size_t size = 0;
			const uint8_t *zip = read_mounted(name, &size, true);
			std::vector<S3ZipEntry> entries;
			if (!zip || !s3zip_list(zip, size, entries))
			{
				snprintf(err, (size_t)errsize, "error.mod_not_a_zip" TEXT_ARG "%s", name);
				return false;
			}
			S3Mod mod;
			mod.zip = name;
			std::vector<std::string> dirs;
			for (const S3ZipEntry &e : entries)
			{
				std::string n = e.name;
				std::replace(n.begin(), n.end(), '\\', '/');
				if (n == "mod.json") dirs.push_back("");
				else if (ends_with(n, "/mod.json")) dirs.push_back(n.substr(0, n.size() - 9));
			}
			std::sort(dirs.begin(), dirs.end());
			for (const std::string &d : dirs)
			{
				bool skip = false;
				for (const std::string &kept : mod.folders)
					if (kept.empty() || (d.size() > kept.size() && d.compare(0, kept.size(), kept) == 0 && d[kept.size()] == '/')) skip = true;
				size_t at = 0;
				while (!skip && at <= d.size())
				{
					if (at < d.size() && d[at] == '#') skip = true;
					const size_t slash = d.find('/', at);
					if (slash == std::string::npos) break;
					at = slash + 1;
				}
				if (!skip) mod.folders.push_back(d);
			}
			if (mod.folders.empty())
			{
				snprintf(err, (size_t)errsize, "error.mod_has_no_mod_json" TEXT_ARG "%s", name);
				return false;
			}
			memfs::addExternal(std::wstring(S3_SAVEDATA) + L"/mods/" + memfs::widen(name), zip, size);
			g_mods.push_back(mod);
		}
		memfs::addDirectory(std::wstring(S3_SAVEDATA) + L"/mods");
		/* the active mods, in the slot's order: upstream's own list, read by
		 * its ModManager at start-up */
		std::string json = "{\n\t\"ActiveMods\": [";
		bool first = true;
		for (const S3Mod &m : g_mods)
			for (const std::string &f : m.folders)
			{
				std::string local = m.zip + (f.empty() ? "" : "/" + f);
				std::string quoted;
				for (char c : local)
				{
					if (c == '"' || c == '\\') quoted.push_back('\\');
					quoted.push_back(c);
				}
				json += std::string(first ? "\n" : ",\n") + "\t\t\"" + quoted + "\"";
				first = false;
			}
		json += "\n\t]\n}\n";
		if (!g_mods.empty())
		{
			std::vector<uint8_t> bytes(json.begin(), json.end());
			memfs::addOwned(std::wstring(S3_SAVEDATA) + L"/mods/active-mods.json", std::move(bytes));
		}
		return true;
	}

	/* the soundtrack slot: audioremaster.bin, or the files of the remastered
	 * folder (its Ogg files and audio_replacements.json) */
	bool load_soundtrack(char *err, int errsize)
	{
		char name[512];
		for (int i = 0; wbx_slot_name("soundtrack", i, name, (int)sizeof name); ++i)
		{
			const std::string n = name;
			size_t size = 0;
			const uint8_t *bytes = read_mounted(name, &size, true);
			if (!bytes)
			{
				snprintf(err, (size_t)errsize, "error.soundtrack_unreadable" TEXT_ARG "%s", name);
				return false;
			}
			if (!strcasecmp(name, "audioremaster.bin"))
				memfs::addExternal(L"data/audioremaster.bin", bytes, size);
			else if (ends_with(n, ".ogg") || ends_with(n, ".json"))
				memfs::addExternal(L"data/audio/remastered/" + memfs::widen(n), bytes, size);
			else
			{
				snprintf(err, (size_t)errsize, "error.soundtrack_not_one" TEXT_ARG "%s", name);
				return false;
			}
			++g_soundtrack;
		}
		return true;
	}

	/* the save data slot: a zip as Export Save Data writes it */
	bool load_saves(char *err, int errsize)
	{
		char name[512];
		for (int i = 0; wbx_slot_name("savedata", i, name, (int)sizeof name); ++i)
		{
			size_t size = 0;
			const uint8_t *zip = read_mounted(name, &size, false);
			std::vector<S3ZipEntry> entries;
			if (!zip || !s3zip_list(zip, size, entries))
			{
				snprintf(err, (size_t)errsize, "error.savedata_not_a_zip" TEXT_ARG "%s", name);
				return false;
			}
			for (const S3ZipEntry &e : entries)
			{
				if (e.name.empty() || e.name.back() == '/') continue;
				const std::wstring path = memfs::normalize(memfs::widen(e.name));
				if (path.empty() || path.compare(0, 5, L"mods/") == 0 || path == memfs::widen(S3_ROM_ID))
				{
					snprintf(err, (size_t)errsize, "error.savedata_not_one_this" TEXT_ARG "%s", e.name.c_str());
					free((void *)zip);
					return false;
				}
				std::vector<uint8_t> bytes;
				if (!s3zip_extract(zip, size, e, bytes))
				{
					snprintf(err, (size_t)errsize, "error.savedata_not_a_zip" TEXT_ARG "%s", name);
					free((void *)zip);
					return false;
				}
				memfs::addOwned(std::wstring(S3_SAVEDATA) + L"/" + path, std::move(bytes));
			}
			free((void *)zip);
		}
		return true;
	}
}

/* ------------------------------------------------------------ zips */

bool s3zip_list(const uint8_t *zip, size_t size, std::vector<S3ZipEntry> &out)
{
	if (size < 22) return false;
	/* the end of the central directory: the last signature in the last 64 KiB */
	size_t eocd = SIZE_MAX;
	const size_t lowest = size > 22 + 65535 ? size - 22 - 65535 : 0;
	for (size_t p = size - 22 + 1; p-- > lowest;)
		if (rd32(zip + p) == 0x06054b50) { eocd = p; break; }
	if (eocd == SIZE_MAX) return false;
	const uint16_t count = rd16(zip + eocd + 10);
	const uint32_t cdSize = rd32(zip + eocd + 12), cdOffset = rd32(zip + eocd + 16);
	if ((uint64_t)cdOffset + cdSize > size) return false;
	const uint8_t *p = zip + cdOffset, *end = zip + cdOffset + cdSize;
	for (uint16_t i = 0; i < count; ++i)
	{
		if (p + 46 > end || rd32(p) != 0x02014b50) return false;
		S3ZipEntry e;
		e.method = rd16(p + 10);
		e.compressedSize = rd32(p + 20);
		e.size = rd32(p + 24);
		const uint16_t nameLen = rd16(p + 28), extraLen = rd16(p + 30), commentLen = rd16(p + 32);
		e.localOffset = rd32(p + 42);
		if (p + 46 + nameLen > end) return false;
		e.name.assign((const char *)p + 46, nameLen);
		out.push_back(e);
		p += 46 + nameLen + extraLen + commentLen;
	}
	return true;
}

bool s3zip_extract(const uint8_t *zip, size_t size, const S3ZipEntry &e, std::vector<uint8_t> &out)
{
	if ((uint64_t)e.localOffset + 30 > size || rd32(zip + e.localOffset) != 0x04034b50) return false;
	const size_t data = (size_t)e.localOffset + 30 + rd16(zip + e.localOffset + 26) + rd16(zip + e.localOffset + 28);
	if ((uint64_t)data + e.compressedSize > size) return false;
	out.resize(e.size);
	if (e.method == 0)
	{
		if (e.compressedSize != e.size) return false;
		memcpy(out.data(), zip + data, e.size);
		return true;
	}
	if (e.method != 8) return false;
	z_stream s;
	memset(&s, 0, sizeof s);
	if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;
	s.next_in = (Bytef *)(zip + data);
	s.avail_in = e.compressedSize;
	s.next_out = out.data();
	s.avail_out = e.size;
	const int r = inflate(&s, Z_FINISH);
	inflateEnd(&s);
	return r == Z_STREAM_END && s.total_out == e.size;
}

/* ------------------------------------------------------------ the load */

int s3files_load(char *err, int errsize)
{
	memfs::addDirectory(S3_SAVEDATA);
	return load_blob(err, errsize) && load_rom(err, errsize) && load_mods(err, errsize) &&
		load_soundtrack(err, errsize) && load_saves(err, errsize);
}

const std::vector<S3Mod> &s3files_mods(void) { return g_mods; }
int s3files_soundtrack_count(void) { return g_soundtrack; }

/* ------------------------------------------------------------ save data out */

/* what the game wrote in its save data folder: its settings, its saves and
 * unlocks (storage/), the mods' own settings - not the ROM's copy, not the
 * log, not the compiled scripts' cache, not the mods themselves */
namespace
{
	std::vector<std::wstring> g_export;
	std::vector<std::string> g_export_names;

	bool exported(const std::wstring &rel)
	{
		if (rel == memfs::widen(S3_ROM_ID) || rel == L"logfile.txt" || rel.compare(0, 5, L"mods/") == 0) return false;
		return true;
	}
}

extern "C" int s3drv_saves_count(void)
{
	g_export.clear();
	g_export_names.clear();
	std::vector<std::wstring> paths;
	memfs::writtenFiles(S3_SAVEDATA, paths);
	const size_t prefix = wcslen(S3_SAVEDATA) + 1;
	for (const std::wstring &p : paths)
	{
		const std::wstring rel = p.substr(prefix);
		size_t size = 0;
		if (!exported(rel) || !memfs::data(p, &size) || !size) continue;
		g_export.push_back(p);
		g_export_names.push_back(memfs::narrow(rel));
	}
	return (int)g_export.size();
}

extern "C" const char *s3drv_saves_name(int i) { return i >= 0 && i < (int)g_export.size() ? g_export_names[i].c_str() : ""; }
extern "C" int64_t s3drv_saves_size(int i)
{
	size_t size = 0;
	if (i < 0 || i >= (int)g_export.size() || !memfs::data(g_export[i], &size)) return 0;
	return (int64_t)size;
}
extern "C" const uint8_t *s3drv_saves_data(int i)
{
	size_t size = 0;
	return i >= 0 && i < (int)g_export.size() ? memfs::data(g_export[i], &size) : nullptr;
}
