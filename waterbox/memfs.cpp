/* memfs.cpp - the machine's files, in the machine's memory (memfs.h).
 *
 * Two doors lead here. rmxbase's FileIO (patches/0002) asks for what a file
 * system answers: whether a path is there, a file's bytes, a listing, a
 * file saved, renamed or removed. And the C library's fopen, which the link
 * wraps (sources.mk), opens a stream: rmxbase's FileHandle reads and writes
 * through it (the logs, the packages, the Ogg streams, the zips). A stream
 * is a fopencookie over the file's bytes, so reading, writing, appending
 * and seeking all work, and everything it touches is the machine's memory,
 * which a savestate carries.
 *
 * Listings come out in path order, the same on every host and in every run.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <set>

#include "memfs.h"

extern "C" FILE *__real_fopen(const char *path, const char *mode);

namespace
{
	struct Node
	{
		const uint8_t *ext = nullptr;   /* bytes the machine does not own */
		size_t extSize = 0;
		std::vector<uint8_t> own;       /* bytes it does, once written */
		bool owned = false;
		bool written = false;           /* written by the game, or seeded as save data */

		const uint8_t *bytes() const { return owned ? own.data() : ext; }
		size_t size() const { return owned ? own.size() : extSize; }
		void makeOwned()
		{
			if (owned) return;
			own.assign(ext, ext + extSize);
			owned = true;
			ext = nullptr;
			extSize = 0;
		}
	};

	std::map<std::wstring, Node> g_files;
	std::set<std::wstring> g_dirs;
	uint64_t g_writes = 0;

	Node *find(const std::wstring &path)
	{
		auto it = g_files.find(path);
		return it == g_files.end() ? nullptr : &it->second;
	}

	bool isDir(const std::wstring &path)
	{
		if (path.empty()) return true;
		if (g_dirs.count(path)) return true;
		const std::wstring prefix = path + L'/';
		auto it = g_files.lower_bound(prefix);
		if (it != g_files.end() && it->first.compare(0, prefix.size(), prefix) == 0) return true;
		auto dt = g_dirs.lower_bound(prefix);
		return dt != g_dirs.end() && dt->compare(0, prefix.size(), prefix) == 0;
	}

	void addParents(const std::wstring &path)
	{
		size_t slash = path.find(L'/');
		while (slash != std::wstring::npos)
		{
			g_dirs.insert(path.substr(0, slash));
			slash = path.find(L'/', slash + 1);
		}
	}

	Node &create(const std::wstring &path)
	{
		addParents(path);
		Node &n = g_files[path];
		return n;
	}

	/* ---- streams ---- */

	struct Cookie
	{
		std::wstring path;
		size_t pos;
		bool write;
		bool append;
	};

	ssize_t cookieRead(void *c, char *buf, size_t size)
	{
		Cookie *k = (Cookie *)c;
		Node *n = find(k->path);
		if (!n) return 0;
		const size_t total = n->size();
		if (k->pos >= total) return 0;
		const size_t len = std::min(size, total - k->pos);
		memcpy(buf, n->bytes() + k->pos, len);
		k->pos += len;
		return (ssize_t)len;
	}

	ssize_t cookieWrite(void *c, const char *buf, size_t size)
	{
		Cookie *k = (Cookie *)c;
		if (!k->write) { errno = EBADF; return -1; }
		Node &n = create(k->path);
		n.makeOwned();
		n.written = true;
		++g_writes;
		if (k->append) k->pos = n.own.size();
		if (k->pos + size > n.own.size()) n.own.resize(k->pos + size);
		memcpy(n.own.data() + k->pos, buf, size);
		k->pos += size;
		return (ssize_t)size;
	}

	int cookieSeek(void *c, off64_t *offset, int whence)
	{
		Cookie *k = (Cookie *)c;
		Node *n = find(k->path);
		const int64_t size = n ? (int64_t)n->size() : 0;
		int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)k->pos : size;
		const int64_t to = base + (int64_t)*offset;
		if (to < 0) { errno = EINVAL; return -1; }
		k->pos = (size_t)to;
		*offset = to;
		return 0;
	}

	int cookieClose(void *c)
	{
		delete (Cookie *)c;
		return 0;
	}
}

namespace memfs
{
	std::wstring widen(std::string_view s)
	{
		std::wstring out;
		for (size_t i = 0; i < s.size();)
		{
			const unsigned char c = (unsigned char)s[i];
			uint32_t cp = c;
			int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
			if (more) cp = c & (0x3F >> more);
			++i;
			for (; more > 0 && i < s.size(); --more, ++i) cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
			out.push_back((wchar_t)cp);
		}
		return out;
	}

	std::string narrow(std::wstring_view w)
	{
		std::string out;
		for (wchar_t ch : w)
		{
			const uint32_t cp = (uint32_t)ch;
			if (cp < 0x80) out.push_back((char)cp);
			else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
			else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
			else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
		}
		return out;
	}

	std::wstring normalize(std::wstring_view path)
	{
		std::vector<std::wstring> parts;
		std::wstring part;
		auto flush = [&]()
		{
			if (part.empty() || part == L".") { }
			else if (part == L"..") { if (!parts.empty()) parts.pop_back(); }
			else parts.push_back(part);
			part.clear();
		};
		for (wchar_t ch : path)
		{
			if (ch == L'/' || ch == L'\\') flush();
			else part.push_back(ch);
		}
		flush();
		std::wstring out;
		for (size_t i = 0; i < parts.size(); ++i)
		{
			if (i) out.push_back(L'/');
			out += parts[i];
		}
		return out;
	}

	void addExternal(std::wstring_view path, const uint8_t *data, size_t size)
	{
		Node &n = create(normalize(path));
		n.own.clear();
		n.owned = false;
		n.ext = data;
		n.extSize = size;
		n.written = false;
	}

	void addOwned(std::wstring_view path, std::vector<uint8_t> &&data)
	{
		Node &n = create(normalize(path));
		n.own = std::move(data);
		n.owned = true;
		n.ext = nullptr;
		n.extSize = 0;
		n.written = true;
	}

	void addDirectory(std::wstring_view path)
	{
		const std::wstring p = normalize(path);
		addParents(p);
		if (!p.empty()) g_dirs.insert(p);
	}

	bool exists(std::wstring_view path)
	{
		const std::wstring p = normalize(path);
		return find(p) || isDir(p);
	}

	const uint8_t *data(std::wstring_view path, size_t *size)
	{
		Node *n = find(normalize(path));
		if (!n) return nullptr;
		*size = n->size();
		return n->bytes();
	}

	void writtenFiles(std::wstring_view folder, std::vector<std::wstring> &out)
	{
		const std::wstring f = normalize(folder);
		const std::wstring prefix = f.empty() ? f : f + L'/';
		for (auto &kv : g_files)
			if (kv.second.written && kv.first.compare(0, prefix.size(), prefix) == 0)
				out.push_back(kv.first);
	}

	uint64_t writeCount() { return g_writes; }
}

/* ---- rmxbase's FileIO (patches/0002) ---- */

namespace chimera_memfs
{
	bool exists(std::wstring_view path) { return memfs::exists(path); }
	bool isFile(std::wstring_view path) { return find(memfs::normalize(path)) != nullptr; }
	bool isDirectory(std::wstring_view path)
	{
		const std::wstring p = memfs::normalize(path);
		return !find(p) && isDir(p);
	}

	bool fileSize(std::wstring_view path, uint64_t &outSize)
	{
		Node *n = find(memfs::normalize(path));
		if (!n) return false;
		outSize = n->size();
		return true;
	}

	bool readFile(std::wstring_view path, std::vector<uint8_t> &outData)
	{
		Node *n = find(memfs::normalize(path));
		if (!n) return false;
		outData.assign(n->bytes(), n->bytes() + n->size());
		return true;
	}

	bool saveFile(std::wstring_view path, const void *data, size_t size)
	{
		const std::wstring p = memfs::normalize(path);
		if (p.empty() || isDir(p)) return false;
		Node &n = create(p);
		n.ext = nullptr;
		n.extSize = 0;
		n.owned = true;
		n.own.assign((const uint8_t *)data, (const uint8_t *)data + size);
		n.written = true;
		++g_writes;
		return true;
	}

	bool rename(std::wstring_view oldPath, std::wstring_view newPath)
	{
		const std::wstring a = memfs::normalize(oldPath), b = memfs::normalize(newPath);
		if (Node *n = find(a))
		{
			Node moved = std::move(*n);
			g_files.erase(a);
			moved.written = true;
			create(b) = std::move(moved);
			++g_writes;
			return true;
		}
		if (!isDir(a)) return false;
		const std::wstring prefix = a + L'/';
		std::vector<std::wstring> from;
		for (auto &kv : g_files)
			if (kv.first.compare(0, prefix.size(), prefix) == 0) from.push_back(kv.first);
		for (const std::wstring &f : from)
		{
			Node moved = std::move(g_files[f]);
			g_files.erase(f);
			moved.written = true;
			create(b + L'/' + f.substr(prefix.size())) = std::move(moved);
		}
		std::vector<std::wstring> dirs;
		for (const std::wstring &d : g_dirs)
			if (d == a || d.compare(0, prefix.size(), prefix) == 0) dirs.push_back(d);
		for (const std::wstring &d : dirs)
		{
			g_dirs.erase(d);
			g_dirs.insert(b + d.substr(a.size()));
		}
		memfs::addDirectory(b);
		++g_writes;
		return true;
	}

	bool remove(std::wstring_view path, bool directory)
	{
		const std::wstring p = memfs::normalize(path);
		if (!directory)
		{
			++g_writes;
			return g_files.erase(p) > 0;
		}
		const std::wstring prefix = p + L'/';
		for (auto it = g_files.begin(); it != g_files.end();)
			it = it->first.compare(0, prefix.size(), prefix) == 0 ? g_files.erase(it) : std::next(it);
		for (auto it = g_dirs.begin(); it != g_dirs.end();)
			it = (*it == p || it->compare(0, prefix.size(), prefix) == 0) ? g_dirs.erase(it) : std::next(it);
		++g_writes;
		return true;
	}

	bool createDirectory(std::wstring_view path)
	{
		const std::wstring p = memfs::normalize(path);
		if (find(p)) return false;
		memfs::addDirectory(p);
		return true;
	}

	void listDirectory(std::wstring_view path, std::vector<std::wstring> *outFiles, std::vector<size_t> *outSizes,
	                   std::vector<std::wstring> *outDirectories)
	{
		const std::wstring p = memfs::normalize(path);
		const std::wstring prefix = p.empty() ? p : p + L'/';
		std::set<std::wstring> dirs;
		for (auto it = g_files.lower_bound(prefix); it != g_files.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
		{
			const std::wstring rest = it->first.substr(prefix.size());
			const size_t slash = rest.find(L'/');
			if (slash == std::wstring::npos)
			{
				if (outFiles) outFiles->push_back(rest);
				if (outSizes) outSizes->push_back(it->second.size());
			}
			else
				dirs.insert(rest.substr(0, slash));
		}
		for (auto it = g_dirs.lower_bound(prefix); it != g_dirs.end() && it->compare(0, prefix.size(), prefix) == 0; ++it)
		{
			const std::wstring rest = it->substr(prefix.size());
			if (rest.empty()) continue;
			dirs.insert(rest.substr(0, rest.find(L'/')));
		}
		if (outDirectories)
			for (const std::wstring &d : dirs) outDirectories->push_back(d);
	}
}

/* ---- the C library's fopen, wrapped (sources.mk) ---- */

extern "C" FILE *__wrap_fopen(const char *path, const char *mode)
{
	if (!path || !mode) { errno = EINVAL; return nullptr; }
	/* the miniBox kit's own channels, read at Init */
	if (!strcmp(path, "settings") || !strcmp(path, "slots")) return __real_fopen(path, mode);
	const std::wstring p = memfs::normalize(memfs::widen(path));
	const bool write = strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+');
	Node *n = find(p);
	if (!write && !n) { errno = ENOENT; return nullptr; }
	if (write && (p.empty() || isDir(p))) { errno = EISDIR; return nullptr; }
	if (strchr(mode, 'w'))
	{
		Node &w = create(p);
		w.ext = nullptr;
		w.extSize = 0;
		w.own.clear();
		w.owned = true;
		w.written = true;
		++g_writes;
	}
	else if (write && !n)
	{
		Node &w = create(p);
		w.owned = true;
		w.written = true;
	}
	Cookie *c = new Cookie{ p, 0, write, strchr(mode, 'a') != nullptr };
	cookie_io_functions_t io = { cookieRead, cookieWrite, cookieSeek, cookieClose };
	FILE *f = fopencookie(c, mode, io);
	if (!f) delete c;
	return f;
}

extern "C" int __real_fclose(FILE *f);
extern "C" int __wrap_fclose(FILE *f) { return __real_fclose(f); }
