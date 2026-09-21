#include "toolchain.h"
#include <robosys/syscall.h>

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
}

static int seq(const char *a, const char *b)
{
    unsigned i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == 0 && b[i] == 0;
}

static void append(char *dst, unsigned max, const char *src)
{
    unsigned n = slen(dst);
    unsigned i = 0;
    while (src && src[i] && n + 1 < max)
        dst[n++] = src[i++];
    if (max)
        dst[n < max ? n : max - 1] = 0;
}

int TcParseSpec(const char *spec, char *name, unsigned nmax,
                       char *ver, unsigned vmax)
{
    unsigned i = 0, j = 0;
    if (!spec || !name || !ver || !nmax || !vmax)
        return -1;
    while (spec[i] && spec[i] != '@' && i + 1 < nmax) {
        name[i] = spec[i];
        i++;
    }
    name[i] = 0;
    if (!name[0])
        return -1;
    if (spec[i] != '@') {
        ver[0] = 0;
        return 0;
    }
    i++;
    while (spec[i] && j + 1 < vmax)
        ver[j++] = spec[i++];
    ver[j] = 0;
    return ver[0] ? 0 : -1;
}

int TcReadDefault(const char *name, char *ver, unsigned verMax)
{
    char buf[512];
    Long r;
    unsigned i = 0, n;
    int fd;

    if (!name || !ver || !verMax)
        return -1;
    ver[0] = 0;
    fd = (int)Open(RoboTcDefaults, (int)RoboORdonly, 0);
    if (fd < 0)
        return -1;
    r = Read(fd, buf, sizeof(buf) - 1);
    Close(fd);
    if (r < 0)
        return -1;
    n = (unsigned)r;
    buf[n] = 0;
    while (i < n) {
        char key[RoboTcNameMax];
        unsigned k = 0;
        unsigned vs, ve;
        while (i < n && buf[i] != '=' && buf[i] != '\n' && k + 1 < sizeof(key))
            key[k++] = buf[i++];
        key[k] = 0;
        if (i >= n || buf[i] != '=') {
            while (i < n && buf[i] != '\n')
                i++;
            if (i < n)
                i++;
            continue;
        }
        i++;
        vs = i;
        while (i < n && buf[i] != '\n')
            i++;
        ve = i;
        if (seq(key, name)) {
            unsigned v = 0;
            while (vs < ve && v + 1 < verMax)
                ver[v++] = buf[vs++];
            ver[v] = 0;
            return 0;
        }
        if (i < n && buf[i] == '\n')
            i++;
    }
    return -1;
}

int TcWriteDefault(const char *name, const char *ver)
{
    char old[512];
    char out[640];
    Long r;
    unsigned i = 0, n = 0, o = 0;
    int fd;
    int replaced = 0;

    if (!name || !ver)
        return -1;
    old[0] = 0;
    fd = (int)Open(RoboTcDefaults, (int)RoboORdonly, 0);
    if (fd >= 0) {
        r = Read(fd, old, sizeof(old) - 1);
        Close(fd);
        if (r > 0) {
            n = (unsigned)r;
            old[n] = 0;
        }
    }
    while (i < n) {
        char key[RoboTcNameMax];
        unsigned k = 0;
        unsigned vs, ve;
        unsigned line = i;
        while (i < n && old[i] != '=' && old[i] != '\n' && k + 1 < sizeof(key))
            key[k++] = old[i++];
        key[k] = 0;
        if (i >= n || old[i] != '=') {
            while (i < n && old[i] != '\n')
                i++;
            if (i < n)
                i++;
            continue;
        }
        i++;
        vs = i;
        while (i < n && old[i] != '\n')
            i++;
        ve = i;
        if (seq(key, name)) {
            unsigned j = 0;
            while (name[j] && o + 1 < sizeof(out))
                out[o++] = name[j++];
            if (o + 1 < sizeof(out))
                out[o++] = '=';
            j = 0;
            while (ver[j] && o + 1 < sizeof(out))
                out[o++] = ver[j++];
            if (o + 1 < sizeof(out))
                out[o++] = '\n';
            replaced = 1;
        } else {
            while (line < i && o + 1 < sizeof(out))
                out[o++] = old[line++];
            if (i < n && old[i] == '\n' && o + 1 < sizeof(out)) {
                out[o++] = '\n';
            }
            (void)vs;
            (void)ve;
        }
        if (i < n && old[i] == '\n')
            i++;
    }
    if (!replaced) {
        unsigned j = 0;
        while (name[j] && o + 1 < sizeof(out))
            out[o++] = name[j++];
        if (o + 1 < sizeof(out))
            out[o++] = '=';
        j = 0;
        while (ver[j] && o + 1 < sizeof(out))
            out[o++] = ver[j++];
        if (o + 1 < sizeof(out))
            out[o++] = '\n';
    }
    fd = (int)Open(RoboTcDefaults,
                        (int)(RoboOCreat | RoboOWronly | RoboOTrunc), 0);
    if (fd < 0)
        return -1;
    r = Write(fd, out, o);
    Close(fd);
    return r < 0 ? -1 : 0;
}

int TcBinPath(const char *name, const char *ver, const char *bin,
                     char *out, unsigned outMax)
{
    if (!name || !ver || !bin || !out || !outMax)
        return -1;
    out[0] = 0;
    append(out, outMax, RoboTcRoot);
    append(out, outMax, "/");
    append(out, outMax, name);
    append(out, outMax, "/");
    append(out, outMax, ver);
    append(out, outMax, "/bin/");
    append(out, outMax, bin);
    return 0;
}

int TcCatalogPath(const char *name, const char *ver, const char *bin,
                         char *out, unsigned outMax)
{
    if (!name || !ver || !bin || !out || !outMax)
        return -1;
    out[0] = 0;
    append(out, outMax, RoboTcCatalog);
    append(out, outMax, "/");
    append(out, outMax, name);
    append(out, outMax, "/");
    append(out, outMax, ver);
    append(out, outMax, "/bin/");
    append(out, outMax, bin);
    return 0;
}
