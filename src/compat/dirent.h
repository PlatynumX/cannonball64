#ifndef CANNONBALL64_COMPAT_DIRENT_H
#define CANNONBALL64_COMPAT_DIRENT_H

/*
 * Minimal POSIX dirent compatibility layer for libretro-common on N64.
 *
 * libdragon intentionally does not implement POSIX <dirent.h>; it exposes
 * dir_findfirst()/dir_findnext() and dir_t instead.
 */

#include <dir.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif

#ifndef DT_LNK
#define DT_LNK 3
#endif

struct dirent
{
    char d_name[256];
    int d_type;
};

typedef struct cb64_DIR
{
    char path[1024];

    dir_t current;
    struct dirent entry;

    int first_pending;
    int valid;
} DIR;

static inline void cb64_dirent_copy(DIR *d)
{
    strncpy(d->entry.d_name,
            d->current.d_name,
            sizeof(d->entry.d_name) - 1);

    d->entry.d_name[sizeof(d->entry.d_name) - 1] = '\0';
    d->entry.d_type = d->current.d_type;
}

static inline DIR *opendir(const char *path)
{
    DIR *d;
    size_t len;
    int rc;

    if (!path || !*path)
    {
        errno = EINVAL;
        return NULL;
    }

    len = strlen(path);

    if (len >= 1024)
    {
        errno = ENAMETOOLONG;
        return NULL;
    }

    d = (DIR *)calloc(1, sizeof(*d));

    if (!d)
        return NULL;

    memcpy(d->path, path, len + 1);

    /*
     * Cannonball commonly hands us "sd://cannonball/".
     * Keep filesystem roots intact, but remove trailing directory slashes.
     */
    while (len > 5 && d->path[len - 1] == '/')
        d->path[--len] = '\0';

    rc = dir_findfirst(d->path, &d->current);

    if (rc < 0)
    {
        free(d);
        return NULL;
    }

    d->first_pending = 1;
    d->valid = 1;

    return d;
}

static inline struct dirent *readdir(DIR *d)
{
    if (!d || !d->valid)
        return NULL;

    if (d->first_pending)
    {
        d->first_pending = 0;
    }
    else
    {
        if (dir_findnext(d->path, &d->current) < 0)
        {
            d->valid = 0;
            return NULL;
        }
    }

    cb64_dirent_copy(d);
    return &d->entry;
}

static inline int closedir(DIR *d)
{
    if (!d)
    {
        errno = EBADF;
        return -1;
    }

    /*
     * libdragon keeps enumeration state inside dir_t, so there is no
     * filesystem directory handle that must be closed.
     */
    free(d);
    return 0;
}

static inline void rewinddir(DIR *d)
{
    if (!d)
        return;

    if (dir_findfirst(d->path, &d->current) < 0)
    {
        d->valid = 0;
        d->first_pending = 0;
        return;
    }

    d->valid = 1;
    d->first_pending = 1;
}

#ifdef __cplusplus
}
#endif

#endif
