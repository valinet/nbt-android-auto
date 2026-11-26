// QNX 6.5 spawn() declarations for the open toolchain, whose sysroot has no <spawn.h> (toolchain/README.md).
//
// Only what aa_entry_plugin.cpp uses. Layout and values taken from the SDP-built plugin (release v2026.11.10.2,
// libNbtAndroidAuto.so, launchService at 0x1320, same source): memset(&inh, 0, 0x58); flags at offset 0 =
// 0x2007 (SPAWN_SETGROUP | SPAWN_SETSIGMASK | SPAWN_SETSIGDEF | SPAWN_NOZOMBIE); pgroup at 4 = SPAWN_NEWPGROUP (0);
// sigemptyset(offset 8) = sigmask; sigfillset(offset 16) = sigdefault. The checks below fail the build if the open
// sysroot's types would move any of these fields.
#ifndef NBT_QNX_SPAWN_COMPAT_H
#define NBT_QNX_SPAWN_COMPAT_H

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

#define SPAWN_SETGROUP 0x0001
#define SPAWN_SETSIGMASK 0x0002
#define SPAWN_SETSIGDEF 0x0004
#define SPAWN_NOZOMBIE 0x2000
#define SPAWN_NEWPGROUP 0

struct inheritance {
    unsigned long flags;
    pid_t pgroup;
    sigset_t sigmask;
    sigset_t sigdefault;
    unsigned char rest_[88 - 24];   // sigignore, stack, policy, node, runmask, sched_param: always zero here
};

typedef char nbt_spawn_size_check[sizeof(struct inheritance) == 88 ? 1 : -1];
typedef char nbt_spawn_mask_check[offsetof(struct inheritance, sigmask) == 8 ? 1 : -1];
typedef char nbt_spawn_def_check[offsetof(struct inheritance, sigdefault) == 16 ? 1 : -1];
typedef char nbt_spawn_flags_check[(SPAWN_SETGROUP | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK | SPAWN_NOZOMBIE) == 0x2007 ? 1 : -1];

extern "C" pid_t spawn(const char* path, int fd_count, const int fd_map[], const struct inheritance* inherit,
                       char* const argv[], char* const envp[]);

#endif
