#ifndef CANNONBALL64_COMPAT_SYS_MMAN_H
#define CANNONBALL64_COMPAT_SYS_MMAN_H

/*
 * Nintendo 64 has no POSIX mmap.
 *
 * This header exists so old libretro-common code can feature-detect the
 * absence cleanly instead of including newlib's nonexistent sys/mman.h.
 */

#ifndef PROT_READ
#define PROT_READ  1
#endif

#ifndef PROT_WRITE
#define PROT_WRITE 2
#endif

#ifndef MAP_SHARED
#define MAP_SHARED 1
#endif

#ifndef MAP_PRIVATE
#define MAP_PRIVATE 2
#endif

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#endif

/*
 * libretro-common's memmap.h defines HAVE_MMAN immediately before including
 * us. Undo that: N64 genuinely has no mmap implementation.
 */
#ifdef HAVE_MMAN
#undef HAVE_MMAN
#endif
