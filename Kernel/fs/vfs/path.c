#include "fs/vfs/path.h"

int PathValid(const char *path)
{
    unsigned i;

    if (!path || !path[0])
        return 0;
    for (i = 0; i <= RoboPathMax; i++) {
        if (!path[i])
            return 1;
    }
    return 0;
}

int NameValid(const char *name)
{
    unsigned i;

    if (!name || !name[0])
        return 0;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return 0;
    for (i = 0; i <= RoboNameMax; i++) {
        if (!name[i])
            return 1;
        if (name[i] == '/')
            return 0;
    }
    return 0;
}

int PathNext(const char **cursor, char *name, unsigned nameMax)
{
    const char *s;
    unsigned n;

    if (!cursor || !*cursor || !name || !nameMax)
        return -RoboEinval;
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
        if (n + 1 >= nameMax)
            return -RoboEinval;
        name[n] = s[n];
        n++;
    }
    name[n] = 0;
    *cursor = s + n;
    return 1;
}
