#pragma once

// Tryled: a libretro VFS (file access) for the cores. Cores that ask for it (PCSX ReARMed...)
// open every file through us. That lets Tryled hand them a game that lives behind an Android
// content:// URI: the game gets a virtual path, and reads go to a file descriptor Tryled
// already opened, so nothing is copied and the core needs no storage permission.
// Everything else (BIOS, saves) is plain file access.

#include <string>

struct retro_vfs_interface;

namespace RetroVFS {

// The interface to give to cores (version 3), or nullptr where it isn't available.
const retro_vfs_interface *Interface();

// Serve `virtualPath` from the open descriptor `fd` (owned by the caller). Pass fd < 0 to stop.
void SetGameFile(const std::string &virtualPath, int fd);

}  // namespace RetroVFS
