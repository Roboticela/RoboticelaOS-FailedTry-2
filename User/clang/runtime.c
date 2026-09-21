#include <robosys/syscall.h>

/*
 * Guest C / C++ compiler entry (RoboClang).
 * Full LLVM/Clang port is future work; this binary provides versioning,
 * multi-install via robo, and clear UX until the real compiler lands.
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
        Write(1, s, n);
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

static int IsCxx(const char *argv0)
{
    unsigned n = slen(argv0);
    if (n >= 7) {
        const char *p = argv0 + n - 7;
        if (p[0] == 'c' && p[1] == 'l' && p[2] == 'a' && p[3] == 'n' &&
            p[4] == 'g' && p[5] == '+' && p[6] == '+')
            return 1;
    }
    return 0;
}

static void version(int cxx)
{
    if (cxx)
        put("RoboClang++ 18.1.8 (Roboticela C++ toolchain stub)\n");
    else
        put("RoboClang 18.1.8 (Roboticela C toolchain stub)\n");
    put("Target: x86_64-roboticela-none\n");
    put("Full LLVM codegen: not linked yet — use host clang to build OS images.\n");
}

int main(int argc, char **argv)
{
    int i;
    int cxx = (argc > 0 && IsCxx(argv[0]));

    for (i = 1; i < argc; i++) {
        if (seq(argv[i], "--version") || seq(argv[i], "-v") ||
            seq(argv[i], "-V")) {
            version(cxx);
            return 0;
        }
        if (seq(argv[i], "--help") || seq(argv[i], "-h")) {
            put("Usage: ");
            put(cxx ? "clang++" : "clang");
            put(" [options] file.c|.cpp\n");
            put("  --version   show RoboClang version\n");
            put("Managed by: robo install llvm@18.1.8\n");
            return 0;
        }
    }
    if (argc <= 1) {
        version(cxx);
        return 0;
    }
    put("RoboClang: compile not available in this stub build.\n");
    put("Install/select versions with robo; LLVM backend lands with the\n");
    put("Roboticela C/C++ platform port.\n");
    return 1;
}
