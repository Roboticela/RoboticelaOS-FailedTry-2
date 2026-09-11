#include "fs/vfs/path.h"

int robo_path_valid(const char *path)
{
    unsigned i;

    if (!path || !path[0])
        return 0;
    for (i = 0; i <= ROBO_PATH_MAX; i++) {
        if (!path[i])
            return 1;
    }
    return 0;
}

int robo_name_valid(const char *name)
{
    unsigned i;

    if (!name || !name[0])
        return 0;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return 0;
    for (i = 0; i <= ROBO_NAME_MAX; i++) {
        if (!name[i])
            return 1;
        if (name[i] == '/')
            return 0;
    }
    return 0;
}

int robo_path_next(const char **cursor, char *name, unsigned name_max)
{
    const char *s;
    unsigned n;

    if (!cursor || !*cursor || !name || !name_max)
        return -ROBO_EINVAL;
    s = *cursor;
    while (*s == '/')
        s++;
    if (!*s) {
        *cursor = s;
        name[0] = 0;
        return 0;
    }
    n = 0;
    while (s[n] && s[n] != '/') {
        if (n + 1 >= name_max)
            return -ROBO_EINVAL;
        name[n] = s[n];
        n++;
    }
    name[n] = 0;
    *cursor = s + n;
    return 1;
}
