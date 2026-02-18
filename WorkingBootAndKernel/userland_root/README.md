# TomahawkOS Userland Root Filesystem Layout

This directory (`userland_root/`) defines the root filesystem hierarchy for
TomahawkOS.  During the build the tree is packed into a **cpio newc** archive
(`initrd.img`) and placed on the UEFI boot disk.  The kernel can then unpack it
into the ramfs root at startup.

## Directory Structure

```
userland_root/
├── bin/            Essential user-space binaries
│   └── user.elf    Default userland program (copied by build)
├── sbin/           System administration binaries (future)
├── dev/            Device nodes (populated at runtime by kernel)
├── etc/            System configuration files
│   ├── passwd      User account database  (username:x:uid:gid:info:home:shell)
│   ├── shadow      Password hashes         (username:sha256_hash)
│   ├── group       Group database           (groupname:x:gid:members)
│   ├── fstab       Filesystem mount table   (device mountpoint fstype flags)
│   ├── hostname    System hostname
│   └── motd        Message of the day (login banner)
├── home/           User home directories
│   └── admin/      Home for the default admin user
├── proc/           Process info pseudo-filesystem (future procfs)
├── tmp/            Temporary files (separate ramfs mount at runtime)
├── usr/            User programs and libraries
│   ├── bin/        Non-essential user binaries (future)
│   ├── lib/        Shared libraries (future)
│   └── include/    Userland development headers (future)
└── var/            Variable runtime data
    ├── log/        Log files (future: kernel.log, auth.log)
    └── run/        Runtime state (PID files, etc.)
```

## How It Maps to the Kernel

| Path      | Kernel code                        | Notes                                   |
|-----------|------------------------------------|-----------------------------------------|
| `/`       | `fs_init_root()` in mount.c        | Mounted as ramfs on RAM block device    |
| `/dev`    | `fs_mount_system_dirs()` (ramfs)   | Will become devfs later                 |
| `/tmp`    | `fs_mount_system_dirs()` (ramfs)   | Separate mount, noexec                  |
| `/bin/*`  | `exec_process("/bin/hello", …)`    | Looked up via VFS path resolution       |
| `/etc/passwd` | Future VFS-backed auth         | Currently in-memory (password_store.c)  |

## Build Integration

The top-level `src/makefile` runs:

```sh
cp  user/build/user.elf  ../userland_root/bin/user.elf
cd ../userland_root && find . -not -name '.gitkeep' | cpio -o -H newc > ../build/initrd.img
```

The resulting `initrd.img` is copied onto the boot disk alongside `kernel.elf`.

## Adding a New Userland Binary

1. Add/modify source under `src/user/src/`.
2. Build produces `src/user/build/user.elf`.
3. (Optional) Copy additional ELFs into `userland_root/bin/` in the Makefile.
4. They will be included in `initrd.img` automatically.

## Git & Empty Directories

Git does not track empty directories.  Placeholder `.gitkeep` files are used
where a directory must exist but has no real content yet (e.g. `/dev`, `/tmp`).
These are excluded from the initrd archive.
