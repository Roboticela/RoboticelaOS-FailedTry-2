#include <robosys/syscall.h>
#include "lib/toolchain.h"

/*
 * /bin/clang and /bin/clang++ shims — pick default llvm version and spawn it.
 * Build twice as clang.elf and clangxx.elf (same source).
 */

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void put(const char *s)
{
    unsigned n = slen(s);
    if (n)
        robo_write(1, s, n);
}

static int is_cxx_name(const char *s)
{
    unsigned n;
    if (!s)
        return 0;
    n = slen(s);
    if (n >= 7) {
        const char *p = s + n - 7;
        if (p[0] == 'c' && p[1] == 'l' && p[2] == 'a' && p[3] == 'n' &&
            p[4] == 'g' && p[5] == '+' && p[6] == '+')
            return 1;
    }
    if (n >= 8) {
        const char *p = s + n - 8;
        if (p[0] == 'c' && p[1] == 'l' && p[2] == 'a' && p[3] == 'n' &&
            p[4] == 'g' && p[5] == 'x' && p[6] == 'x')
            return 1;
    }
    return 0;
}

static const char *base_name(const char *path)
{
    const char *p = path;
    const char *last = path;
    if (!path)
        return "clang";
    while (*p) {
        if (*p == '/')
            last = p + 1;
        p++;
    }
    return last;
}

int main(int argc, char **argv)
{
    char ver[ROBO_TC_VER_MAX];
    char path[ROBO_TC_PATH_MAX];
    const char *bin;
    char *nargv[32];
    int i, status = 0;
    robo_long pid;
    RoboStat st;

    bin = "clang";
    if (argc > 0 && is_cxx_name(base_name(argv[0])))
        bin = "clang++";

    if (robo_tc_read_default("llvm", ver, sizeof(ver)) != 0) {
        put("no default llvm — run: robo install llvm@18.1.8\n");
        return 1;
    }
    if (robo_tc_bin_path("llvm", ver, bin, path, sizeof(path)) != 0)
        return 1;
    if (robo_stat(path, &st) != 0 || st.size == 0) {
        put(bin);
        put(" not installed for llvm@");
        put(ver);
        put("\n");
        return 1;
    }
    if (argc > 31)
        argc = 31;
    nargv[0] = (char *)path;
    for (i = 1; i < argc; i++)
        nargv[i] = argv[i];
    nargv[argc] = 0;
    pid = robo_process_spawn(path, nargv, argc);
    if ((uint64_t)pid >= (uint64_t)-4095ll || pid <= 0) {
        put("spawn failed\n");
        return 1;
    }
    if (robo_wait((robo_pid)pid, 0, &status) < 0)
        return 1;
    return status;
}
