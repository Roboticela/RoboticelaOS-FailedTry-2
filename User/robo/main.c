#include <robosys/syscall.h>
#include "lib/toolchain.h"

/*
 * robo — Roboticela package manager + driver tools.
 * Guest C/C++ toolchains only (llvm → clang / clang++).
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

static void line(const char *s)
{
    put(s);
    put("\n");
}

static void PutU(unsigned long long v)
{
    char buf[24];
    unsigned n = 0, i;
    if (!v) {
        Write(1, "0", 1);
        return;
    }
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        Write(1, &buf[i - 1], 1);
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

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int MkdirPFile(const char *path)
{
    char tmp[RoboTcPathMax];
    unsigned i = 0, n = slen(path);
    if (n >= sizeof(tmp))
        return -1;
    while (i < n && path[i] != 0) {
        tmp[i] = path[i];
        if (path[i] == '/' && i > 0) {
            tmp[i] = 0;
            RoboMkdir(tmp, 0);
            tmp[i] = '/';
        }
        i++;
    }
    return 0;
}

static int CopyFile(const char *src, const char *dst)
{
    char buf[4096];
    Long r, w;
    int in, out;

    if (MkdirPFile(dst) != 0)
        return -1;
    in = (int)Open(src, (int)RoboORdonly, 0);
    if (in < 0)
        return -1;
    out = (int)Open(dst, (int)(RoboOCreat | RoboOWronly | RoboOTrunc), 0);
    if (out < 0) {
        Close(in);
        return -1;
    }
    for (;;) {
        r = Read(in, buf, sizeof(buf));
        if (r < 0) {
            Close(in);
            Close(out);
            return -1;
        }
        if (r == 0)
            break;
        w = Write(out, buf, (Ulong)r);
        if (w != r) {
            Close(in);
            Close(out);
            return -1;
        }
    }
    Close(in);
    Close(out);
    return 0;
}

static int PkgAllowed(const char *name)
{
    return seq(name, "llvm") || seq(name, "c") || seq(name, "cxx");
}

static const char *PkgCanon(const char *name)
{
    if (seq(name, "c") || seq(name, "cxx") || seq(name, "clang") ||
        seq(name, "clang++"))
        return "llvm";
    return name;
}

static int InstallLlvm(const char *ver)
{
    char src[RoboTcPathMax];
    char dst[RoboTcPathMax];
    RoboStat st;

    if (TcCatalogPath("llvm", ver, "clang", src, sizeof(src)) != 0)
        return 1;
    if (Stat(src, &st) != 0 || st.size == 0) {
        put("package not in catalog: llvm@");
        put(ver);
        put("\n");
        return 1;
    }
    if (TcBinPath("llvm", ver, "clang", dst, sizeof(dst)) != 0)
        return 1;
    if (CopyFile(src, dst) != 0) {
        line("install failed: clang");
        return 1;
    }
    if (TcCatalogPath("llvm", ver, "clang++", src, sizeof(src)) != 0)
        return 1;
    if (TcBinPath("llvm", ver, "clang++", dst, sizeof(dst)) != 0)
        return 1;
    if (CopyFile(src, dst) != 0) {
        line("install failed: clang++");
        return 1;
    }
    if (TcWriteDefault("llvm", ver) != 0)
        line("warn: could not set default llvm version");
    put("installed llvm@");
    put(ver);
    put(" (clang, clang++)\n");
    return 0;
}

static int CmdInstall(int argc, char **argv)
{
    char name[RoboTcNameMax];
    char ver[RoboTcVerMax];
    const char *canon;

    if (argc < 1) {
        line("usage: robo install llvm@<version>");
        line("       robo install c@<version>     (alias for llvm)");
        line("       robo install cxx@<version>   (alias for llvm)");
        return 1;
    }
    if (TcParseSpec(argv[0], name, sizeof(name), ver, sizeof(ver)) != 0 ||
        !ver[0]) {
        line("usage: robo install llvm@18.1.8");
        return 1;
    }
    if (!PkgAllowed(name)) {
        put("unsupported package: ");
        put(name);
        put("\nonly llvm (C/C++) is available via robo\n");
        return 1;
    }
    canon = PkgCanon(name);
    if (seq(canon, "llvm"))
        return InstallLlvm(ver);
    return 1;
}

static int CmdList(void)
{
    char buf[512];
    char def[RoboTcVerMax];
    Long r;
    unsigned i = 0, n;
    int fd;

    line("catalog:");
    fd = (int)Open(RoboTcCatalog "/index", (int)RoboORdonly, 0);
    if (fd < 0) {
        line("  (no catalog index)");
    } else {
        r = Read(fd, buf, sizeof(buf) - 1);
        Close(fd);
        if (r > 0) {
            n = (unsigned)r;
            buf[n] = 0;
            put(buf);
            if (n && buf[n - 1] != '\n')
                put("\n");
        }
    }
    line("defaults:");
    if (TcReadDefault("llvm", def, sizeof(def)) == 0) {
        put("  llvm=");
        put(def);
        put("\n");
    } else {
        line("  llvm=(none)");
    }
    (void)i;
    return 0;
}

static int CmdUse(int argc, char **argv)
{
    char name[RoboTcNameMax];
    char ver[RoboTcVerMax];
    char path[RoboTcPathMax];
    const char *canon;
    RoboStat st;

    if (argc < 2) {
        line("usage: robo use llvm <version>");
        return 1;
    }
    if (slen(argv[0]) >= sizeof(name) || slen(argv[1]) >= sizeof(ver))
        return 1;
    {
        unsigned i = 0;
        while (argv[0][i] && i + 1 < sizeof(name)) {
            name[i] = argv[0][i];
            i++;
        }
        name[i] = 0;
        i = 0;
        while (argv[1][i] && i + 1 < sizeof(ver)) {
            ver[i] = argv[1][i];
            i++;
        }
        ver[i] = 0;
    }
    if (!PkgAllowed(name)) {
        line("only llvm (C/C++) is supported");
        return 1;
    }
    canon = PkgCanon(name);
    if (TcBinPath(canon, ver, "clang", path, sizeof(path)) != 0)
        return 1;
    if (Stat(path, &st) != 0 || st.size == 0) {
        put("not installed: ");
        put(canon);
        put("@");
        put(ver);
        put("\nrun: robo install ");
        put(canon);
        put("@");
        put(ver);
        put("\n");
        return 1;
    }
    if (TcWriteDefault(canon, ver) != 0) {
        line("failed to update defaults");
        return 1;
    }
    put("now using ");
    put(canon);
    put("@");
    put(ver);
    put("\n");
    return 0;
}

static int CmdUninstall(int argc, char **argv)
{
    char name[RoboTcNameMax];
    char ver[RoboTcVerMax];
    char path[RoboTcPathMax];
    const char *canon;

    if (argc < 1 ||
        TcParseSpec(argv[0], name, sizeof(name), ver, sizeof(ver)) != 0 ||
        !ver[0]) {
        line("usage: robo uninstall llvm@<version>");
        return 1;
    }
    if (!PkgAllowed(name)) {
        line("only llvm (C/C++) is supported");
        return 1;
    }
    canon = PkgCanon(name);
    if (TcBinPath(canon, ver, "clang", path, sizeof(path)) != 0)
        return 1;
    if (Unlink(path) != 0)
        line("warn: clang remove failed");
    if (TcBinPath(canon, ver, "clang++", path, sizeof(path)) != 0)
        return 1;
    if (Unlink(path) != 0)
        line("warn: clang++ remove failed");
    put("removed ");
    put(canon);
    put("@");
    put(ver);
    put("\n");
    return 0;
}

static int DriverStatus(void)
{
    RoboDriverStatus st;
    Long r = DriverQuery(&st);
    if (IsErr(r)) {
        line("Driver Manager query failed.");
        return 1;
    }
    line("Driver Manager");
    put("PCI devices       : ");
    PutU(st.pciCount);
    put("\n");
    put("Drivers registered: ");
    PutU(st.driversRegistered);
    put("\n");
    put("Drivers loaded    : ");
    PutU(st.driversLoaded);
    put("\n");
    put("Storage           : ");
    line(st.storageReady ? "READY" : "NOT READY");
    put("Graphics          : ");
    PutU(st.gfxWidth);
    put(" x ");
    PutU(st.gfxHeight);
    put("\n");
    put("Keyboard          : ");
    line(st.keyboard ? "READY" : "NOT READY");
    put("Mouse             : ");
    line(st.mouse ? "READY" : "NOT READY");
    return 0;
}

static int CmdRuntime(int argc, char **argv)
{
    char buf[512];
    int fd;
    Long n;
    (void)argc;
    (void)argv;
    line("Roboticela Runtime");
    line("------------------");
    fd = (int)Open("/System/runtime/engines.manifest", (int)RoboORdonly, 0);
    if (fd < 0) {
        line("engines.manifest: missing");
        line("Expected under /System/runtime/");
        return 1;
    }
    for (;;) {
        n = Read(fd, buf, sizeof(buf) - 1);
        if (n <= 0)
            break;
        buf[n] = 0;
        put(buf);
    }
    Close(fd);
    line("");
    line("Bins: /System/runtime/robosail /bin/robospark /bin/robolayer-demo");
    return 0;
}

static void usage(void)
{
    line("robo — Roboticela package manager");
    line("");
    line("  robo install llvm@18.1.8");
    line("  robo install c@18.1.8");
    line("  robo install cxx@18.1.8");
    line("  robo list");
    line("  robo use llvm 18.1.8");
    line("  robo uninstall llvm@18.1.8");
    line("");
    line("  robo driver status");
    line("  robo runtime");
    line("");
    line("C/C++ only. After install: clang / clang++");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 0;
    }
    if (seq(argv[1], "help") || seq(argv[1], "-h") || seq(argv[1], "--help")) {
        usage();
        return 0;
    }
    if (seq(argv[1], "install"))
        return CmdInstall(argc - 2, argv + 2);
    if (seq(argv[1], "list"))
        return CmdList();
    if (seq(argv[1], "use"))
        return CmdUse(argc - 2, argv + 2);
    if (seq(argv[1], "uninstall"))
        return CmdUninstall(argc - 2, argv + 2);
    if (seq(argv[1], "driver")) {
        if (argc >= 3 && seq(argv[2], "status"))
            return DriverStatus();
        line("usage: robo driver status");
        return 1;
    }
    if (seq(argv[1], "runtime"))
        return CmdRuntime(argc - 2, argv + 2);
    usage();
    return 1;
}
