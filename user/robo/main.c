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
        robo_write(1, s, n);
}

static void line(const char *s)
{
    put(s);
    put("\n");
}

static void put_u(unsigned long long v)
{
    char buf[24];
    unsigned n = 0, i;
    if (!v) {
        robo_write(1, "0", 1);
        return;
    }
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        robo_write(1, &buf[i - 1], 1);
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

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int mkdir_p_file(const char *path)
{
    char tmp[ROBO_TC_PATH_MAX];
    unsigned i = 0, n = slen(path);
    if (n >= sizeof(tmp))
        return -1;
    while (i < n && path[i] != 0) {
        tmp[i] = path[i];
        if (path[i] == '/' && i > 0) {
            tmp[i] = 0;
            robo_mkdir(tmp, 0);
            tmp[i] = '/';
        }
        i++;
    }
    return 0;
}

static int copy_file(const char *src, const char *dst)
{
    char buf[4096];
    robo_long r, w;
    int in, out;

    if (mkdir_p_file(dst) != 0)
        return -1;
    in = (int)robo_open(src, (int)ROBO_O_RDONLY, 0);
    if (in < 0)
        return -1;
    out = (int)robo_open(dst, (int)(ROBO_O_CREAT | ROBO_O_WRONLY | ROBO_O_TRUNC), 0);
    if (out < 0) {
        robo_close(in);
        return -1;
    }
    for (;;) {
        r = robo_read(in, buf, sizeof(buf));
        if (r < 0) {
            robo_close(in);
            robo_close(out);
            return -1;
        }
        if (r == 0)
            break;
        w = robo_write(out, buf, (robo_ulong)r);
        if (w != r) {
            robo_close(in);
            robo_close(out);
            return -1;
        }
    }
    robo_close(in);
    robo_close(out);
    return 0;
}

static int pkg_allowed(const char *name)
{
    return seq(name, "llvm") || seq(name, "c") || seq(name, "cxx");
}

static const char *pkg_canon(const char *name)
{
    if (seq(name, "c") || seq(name, "cxx") || seq(name, "clang") ||
        seq(name, "clang++"))
        return "llvm";
    return name;
}

static int install_llvm(const char *ver)
{
    char src[ROBO_TC_PATH_MAX];
    char dst[ROBO_TC_PATH_MAX];
    RoboStat st;

    if (robo_tc_catalog_path("llvm", ver, "clang", src, sizeof(src)) != 0)
        return 1;
    if (robo_stat(src, &st) != 0 || st.size == 0) {
        put("package not in catalog: llvm@");
        put(ver);
        put("\n");
        return 1;
    }
    if (robo_tc_bin_path("llvm", ver, "clang", dst, sizeof(dst)) != 0)
        return 1;
    if (copy_file(src, dst) != 0) {
        line("install failed: clang");
        return 1;
    }
    if (robo_tc_catalog_path("llvm", ver, "clang++", src, sizeof(src)) != 0)
        return 1;
    if (robo_tc_bin_path("llvm", ver, "clang++", dst, sizeof(dst)) != 0)
        return 1;
    if (copy_file(src, dst) != 0) {
        line("install failed: clang++");
        return 1;
    }
    if (robo_tc_write_default("llvm", ver) != 0)
        line("warn: could not set default llvm version");
    put("installed llvm@");
    put(ver);
    put(" (clang, clang++)\n");
    return 0;
}

static int cmd_install(int argc, char **argv)
{
    char name[ROBO_TC_NAME_MAX];
    char ver[ROBO_TC_VER_MAX];
    const char *canon;

    if (argc < 1) {
        line("usage: robo install llvm@<version>");
        line("       robo install c@<version>     (alias for llvm)");
        line("       robo install cxx@<version>   (alias for llvm)");
        return 1;
    }
    if (robo_tc_parse_spec(argv[0], name, sizeof(name), ver, sizeof(ver)) != 0 ||
        !ver[0]) {
        line("usage: robo install llvm@18.1.8");
        return 1;
    }
    if (!pkg_allowed(name)) {
        put("unsupported package: ");
        put(name);
        put("\nonly llvm (C/C++) is available via robo\n");
        return 1;
    }
    canon = pkg_canon(name);
    if (seq(canon, "llvm"))
        return install_llvm(ver);
    return 1;
}

static int cmd_list(void)
{
    char buf[512];
    char def[ROBO_TC_VER_MAX];
    robo_long r;
    unsigned i = 0, n;
    int fd;

    line("catalog:");
    fd = (int)robo_open(ROBO_TC_CATALOG "/index", (int)ROBO_O_RDONLY, 0);
    if (fd < 0) {
        line("  (no catalog index)");
    } else {
        r = robo_read(fd, buf, sizeof(buf) - 1);
        robo_close(fd);
        if (r > 0) {
            n = (unsigned)r;
            buf[n] = 0;
            put(buf);
            if (n && buf[n - 1] != '\n')
                put("\n");
        }
    }
    line("defaults:");
    if (robo_tc_read_default("llvm", def, sizeof(def)) == 0) {
        put("  llvm=");
        put(def);
        put("\n");
    } else {
        line("  llvm=(none)");
    }
    (void)i;
    return 0;
}

static int cmd_use(int argc, char **argv)
{
    char name[ROBO_TC_NAME_MAX];
    char ver[ROBO_TC_VER_MAX];
    char path[ROBO_TC_PATH_MAX];
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
    if (!pkg_allowed(name)) {
        line("only llvm (C/C++) is supported");
        return 1;
    }
    canon = pkg_canon(name);
    if (robo_tc_bin_path(canon, ver, "clang", path, sizeof(path)) != 0)
        return 1;
    if (robo_stat(path, &st) != 0 || st.size == 0) {
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
    if (robo_tc_write_default(canon, ver) != 0) {
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

static int cmd_uninstall(int argc, char **argv)
{
    char name[ROBO_TC_NAME_MAX];
    char ver[ROBO_TC_VER_MAX];
    char path[ROBO_TC_PATH_MAX];
    const char *canon;

    if (argc < 1 ||
        robo_tc_parse_spec(argv[0], name, sizeof(name), ver, sizeof(ver)) != 0 ||
        !ver[0]) {
        line("usage: robo uninstall llvm@<version>");
        return 1;
    }
    if (!pkg_allowed(name)) {
        line("only llvm (C/C++) is supported");
        return 1;
    }
    canon = pkg_canon(name);
    if (robo_tc_bin_path(canon, ver, "clang", path, sizeof(path)) != 0)
        return 1;
    if (robo_unlink(path) != 0)
        line("warn: clang remove failed");
    if (robo_tc_bin_path(canon, ver, "clang++", path, sizeof(path)) != 0)
        return 1;
    if (robo_unlink(path) != 0)
        line("warn: clang++ remove failed");
    put("removed ");
    put(canon);
    put("@");
    put(ver);
    put("\n");
    return 0;
}

static int driver_status(void)
{
    RoboDriverStatus st;
    robo_long r = robo_driver_query(&st);
    if (is_err(r)) {
        line("Driver Manager query failed.");
        return 1;
    }
    line("Driver Manager");
    put("PCI devices       : ");
    put_u(st.pci_count);
    put("\n");
    put("Drivers registered: ");
    put_u(st.drivers_registered);
    put("\n");
    put("Drivers loaded    : ");
    put_u(st.drivers_loaded);
    put("\n");
    put("Storage           : ");
    line(st.storage_ready ? "READY" : "NOT READY");
    put("Graphics          : ");
    put_u(st.gfx_width);
    put(" x ");
    put_u(st.gfx_height);
    put("\n");
    put("Keyboard          : ");
    line(st.keyboard ? "READY" : "NOT READY");
    put("Mouse             : ");
    line(st.mouse ? "READY" : "NOT READY");
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
        return cmd_install(argc - 2, argv + 2);
    if (seq(argv[1], "list"))
        return cmd_list();
    if (seq(argv[1], "use"))
        return cmd_use(argc - 2, argv + 2);
    if (seq(argv[1], "uninstall"))
        return cmd_uninstall(argc - 2, argv + 2);
    if (seq(argv[1], "driver")) {
        if (argc >= 3 && seq(argv[2], "status"))
            return driver_status();
        line("usage: robo driver status");
        return 1;
    }
    usage();
    return 1;
}
