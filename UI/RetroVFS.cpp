// Tryled: libretro VFS for the cores. See RetroVFS.h.

#include <mutex>
#include <string>

#include "UI/RetroVFS.h"
#include "UI/libretro.h"

#if defined(_WIN32)

namespace RetroVFS {
const retro_vfs_interface *Interface() { return nullptr; }
void SetGameFile(const std::string &, int) {}
}  // namespace RetroVFS

#else

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

struct retro_vfs_file_handle {
	int fd = -1;
	int64_t pos = 0;
	std::string path;
};

struct retro_vfs_dir_handle {
	DIR *dir = nullptr;
	struct dirent *entry = nullptr;
	std::string path;
	bool includeHidden = false;
};

namespace RetroVFS {

static std::mutex g_lock;
static std::string g_gamePath;
static int g_gameFd = -1;

void SetGameFile(const std::string &virtualPath, int fd) {
	std::lock_guard<std::mutex> guard(g_lock);
	g_gamePath = fd >= 0 ? virtualPath : std::string();
	g_gameFd = fd;
}

// The game's descriptor, duplicated, if `path` is the game's virtual path.
static int GameFd(const char *path) {
	std::lock_guard<std::mutex> guard(g_lock);
	if (g_gameFd >= 0 && path && g_gamePath == path)
		return dup(g_gameFd);
	return -1;
}

static const char *GetPath(retro_vfs_file_handle *h) {
	return h ? h->path.c_str() : "";
}

static retro_vfs_file_handle *Open(const char *path, unsigned mode, unsigned hints) {
	if (!path)
		return nullptr;
	int fd = -1;
	if (mode == RETRO_VFS_FILE_ACCESS_READ)
		fd = GameFd(path);
	if (fd < 0) {
		int flags = 0;
		const bool update = (mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) != 0;
		switch (mode & RETRO_VFS_FILE_ACCESS_READ_WRITE) {
		case RETRO_VFS_FILE_ACCESS_READ: flags = O_RDONLY; break;
		case RETRO_VFS_FILE_ACCESS_WRITE: flags = O_WRONLY | O_CREAT | (update ? 0 : O_TRUNC); break;
		case RETRO_VFS_FILE_ACCESS_READ_WRITE: flags = O_RDWR | O_CREAT | (update ? 0 : O_TRUNC); break;
		default: return nullptr;
		}
		fd = open(path, flags | O_CLOEXEC, 0644);
	}
	if (fd < 0)
		return nullptr;
	retro_vfs_file_handle *h = new retro_vfs_file_handle();
	h->fd = fd;
	h->path = path;
	return h;
}

static int Close(retro_vfs_file_handle *h) {
	if (!h)
		return -1;
	close(h->fd);
	delete h;
	return 0;
}

static int64_t Size(retro_vfs_file_handle *h) {
	struct stat st;
	if (!h || fstat(h->fd, &st) != 0)
		return -1;
	return st.st_size;
}

static int64_t Truncate(retro_vfs_file_handle *h, int64_t length) {
	if (!h || ftruncate(h->fd, length) != 0)
		return -1;
	return 0;
}

static int64_t Tell(retro_vfs_file_handle *h) {
	return h ? h->pos : -1;
}

static int64_t Seek(retro_vfs_file_handle *h, int64_t offset, int whence) {
	if (!h)
		return -1;
	int64_t base = 0;
	if (whence == RETRO_VFS_SEEK_POSITION_CURRENT)
		base = h->pos;
	else if (whence == RETRO_VFS_SEEK_POSITION_END)
		base = Size(h);
	if (base < 0 || base + offset < 0)
		return -1;
	h->pos = base + offset;
	return h->pos;
}

static int64_t Read(retro_vfs_file_handle *h, void *s, uint64_t len) {
	if (!h)
		return -1;
	// pread: each handle keeps its own position, even on a duplicated descriptor.
	ssize_t n = pread(h->fd, s, (size_t)len, (off_t)h->pos);
	if (n < 0)
		return -1;
	h->pos += n;
	return n;
}

static int64_t Write(retro_vfs_file_handle *h, const void *s, uint64_t len) {
	if (!h)
		return -1;
	ssize_t n = pwrite(h->fd, s, (size_t)len, (off_t)h->pos);
	if (n < 0)
		return -1;
	h->pos += n;
	return n;
}

static int Flush(retro_vfs_file_handle *h) {
	return h ? 0 : -1;
}

static int Remove(const char *path) {
	if (!path)
		return -1;
	if (unlink(path) == 0 || rmdir(path) == 0)
		return 0;
	return -1;
}

static int Rename(const char *from, const char *to) {
	return (from && to && rename(from, to) == 0) ? 0 : -1;
}

static int Stat(const char *path, int32_t *size) {
	if (!path)
		return 0;
	struct stat st;
	int gameFd = GameFd(path);
	int ok;
	if (gameFd >= 0) {
		ok = fstat(gameFd, &st) == 0;
		close(gameFd);
	} else {
		ok = stat(path, &st) == 0;
	}
	if (!ok)
		return 0;
	if (size)
		*size = (int32_t)st.st_size;
	int flags = RETRO_VFS_STAT_IS_VALID;
	if (S_ISDIR(st.st_mode))
		flags |= RETRO_VFS_STAT_IS_DIRECTORY;
	if (S_ISCHR(st.st_mode))
		flags |= RETRO_VFS_STAT_IS_CHARACTER_SPECIAL;
	return flags;
}

static int MkDir(const char *dir) {
	if (!dir)
		return -1;
	if (mkdir(dir, 0755) == 0)
		return 0;
	return errno == EEXIST ? -2 : -1;
}

static retro_vfs_dir_handle *OpenDir(const char *dir, bool includeHidden) {
	if (!dir)
		return nullptr;
	DIR *d = opendir(dir);
	if (!d)
		return nullptr;
	retro_vfs_dir_handle *h = new retro_vfs_dir_handle();
	h->dir = d;
	h->path = dir;
	h->includeHidden = includeHidden;
	return h;
}

static bool ReadDir(retro_vfs_dir_handle *h) {
	if (!h)
		return false;
	while ((h->entry = readdir(h->dir)) != nullptr) {
		const char *name = h->entry->d_name;
		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;
		if (!h->includeHidden && name[0] == '.')
			continue;
		return true;
	}
	return false;
}

static const char *DirentName(retro_vfs_dir_handle *h) {
	return h && h->entry ? h->entry->d_name : nullptr;
}

static bool DirentIsDir(retro_vfs_dir_handle *h) {
	if (!h || !h->entry)
		return false;
	if (h->entry->d_type == DT_DIR)
		return true;
	if (h->entry->d_type != DT_UNKNOWN)
		return false;
	struct stat st;
	const std::string full = h->path + "/" + h->entry->d_name;
	return stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static int CloseDir(retro_vfs_dir_handle *h) {
	if (!h)
		return -1;
	closedir(h->dir);
	delete h;
	return 0;
}

const retro_vfs_interface *Interface() {
	static const retro_vfs_interface iface = {
		GetPath, Open, Close, Size, Tell, Seek, Read, Write, Flush, Remove, Rename,
		Truncate,
		Stat, MkDir, OpenDir, ReadDir, DirentName, DirentIsDir, CloseDir,
	};
	return &iface;
}

}  // namespace RetroVFS

#endif
