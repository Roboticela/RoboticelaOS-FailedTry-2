#include "roboboot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int g_fails = 0;
static int g_pass = 0;
static uint64_t g_time = 0;
static rb_input g_in;
static char g_root[512];
static char g_last_boot[64];

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

static uint64_t t_now(void) { return g_time; }
static void t_sleep(uint32_t ms) { g_time += ms; }
static void t_poll(rb_input *out) { *out = g_in; rb_zero(&g_in, sizeof(g_in)); }
static int t_present(rb_surface *s) { (void)s; return 1; }
static void t_log(const char *m) { (void)m; }

static rb_status t_read(const char *path, void **data, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long sz;
    void *buf;
    if (!f) return RB_ERR_IO;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return RB_ERR_IO; }
    buf = rb_alloc((size_t)sz + 1);
    if (!buf) { fclose(f); return RB_ERR_NOMEM; }
    if (sz && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        rb_free(buf);
        fclose(f);
        return RB_ERR_IO;
    }
    fclose(f);
    *data = buf;
    *len = (size_t)sz;
    return RB_OK;
}

static rb_status t_write(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return RB_ERR_IO;
    if (len && fwrite(data, 1, len, f) != len) { fclose(f); return RB_ERR_IO; }
    fclose(f);
    return RB_OK;
}

static rb_status t_list(const char *path, void (*cb)(const char *name, int is_dir, void *user), void *user)
{
#ifdef _WIN32
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return RB_OK;
    do {
        cb(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, user);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    (void)path; (void)cb; (void)user;
#endif
    return RB_OK;
}

static rb_status t_boot(const rb_target *t, const rb_handoff *h)
{
    rb_strcpy(g_last_boot, sizeof(g_last_boot), t->id);
    (void)h;
    return RB_OK;
}

static rb_status t_fw(void) { rb_strcpy(g_last_boot, sizeof(g_last_boot), "firmware"); return RB_OK; }
static rb_status t_reboot(void) { rb_strcpy(g_last_boot, sizeof(g_last_boot), "reboot"); return RB_OK; }
static rb_status t_halt(void) { rb_strcpy(g_last_boot, sizeof(g_last_boot), "halt"); return RB_OK; }

static rb_platform g_plat = {
    t_now, t_sleep, t_poll, t_present, t_read, t_write, t_list, t_log,
    t_boot, t_fw, t_reboot, t_halt, NULL
};

static void test_palette(void)
{
    CHECK(rb_token(RB_TOKEN_BRAND_PRIMARY, 255) == rb_rgba(0x3B, 0x9E, 0xFF, 255), "brand primary dark");
    CHECK(rb_token(RB_TOKEN_BG_PRIMARY, 255) == rb_rgba(0, 0, 0, 255), "bg primary dark");
    CHECK(rb_token(RB_TOKEN_TEXT_SECONDARY, 255) == rb_rgba(0xA1, 0xA1, 0xA6, 255), "text secondary dark");
    CHECK(rb_token(RB_TOKEN_DANGER, 255) == rb_rgba(0xFF, 0x45, 0x3A, 255), "danger dark");
    CHECK(rb_tokenf(RB_TOKEN_TEXT_PRIMARY, 1.f) == rb_rgba(255, 255, 255, 255), "token fade");
}

static void test_sha256(void)
{
    uint8_t out[32];
    char hex[72];
    rb_sha256("abc", 3, out);
    rb_hex(out, 32, hex, sizeof(hex));
    CHECK(rb_str_eq(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "sha256(abc)");
}

static void test_lock(void)
{
    uint8_t salt[4] = {1, 2, 3, 4};
    uint8_t hash[32];
    char hex[72], spec[128];
    rb_lock_hash("ela", salt, 4, 16, hash);
    rb_hex(hash, 32, hex, sizeof(hex));
    snprintf(spec, sizeof(spec), "16:01020304:%s", hex);
    CHECK(rb_lock_verify(spec, "ela"), "lock accepts password");
    CHECK(!rb_lock_verify(spec, "nope"), "lock rejects wrong password");
}

static void test_splash_math(void)
{
    CHECK(rb_splash_visual(1.f, 0, 2000) == 0.f, "fast load starts empty");
    CHECK(rb_splash_visual(1.f, 1000, 2000) > 0.49f && rb_splash_visual(1.f, 1000, 2000) < 0.51f,
          "fast load is time-stretched");
    CHECK(rb_splash_visual(1.f, 2000, 2000) >= 0.999f, "fast load fills at min duration");
    CHECK(rb_splash_visual(0.25f, 2000, 2000) == 0.25f, "visual never exceeds real progress");
    CHECK(!rb_splash_complete(1.f, 500, 2000), "not complete before min ms");
    CHECK(rb_splash_complete(1.f, 2000, 2000), "complete at min ms when loaded");
    CHECK(!rb_splash_complete(0.9f, 5000, 2000), "not complete until resources finish");
}

static void test_manifest(void)
{
    const char *txt =
        "manifest 1\n"
        "splash.min_ms 2000\n"
        "picker.key alt\n"
        "default roboticela\n"
        "fallback recovery\n"
        "target roboticela\n"
        "  title RoboticelaOS\n"
        "  kind kernel\n"
        "  pic assets/os-roboticela.rbim\n"
        "  kernel kernel.elf\n"
        "end\n"
        "target recovery\n"
        "  title Recovery\n"
        "  kind kernel\n"
        "  hidden 0\n"
        "end\n";
    rb_catalog c;
    CHECK(rb_manifest_parse(txt, &c) == RB_OK, "parse ok");
    CHECK(c.ntargets == 2, "two targets");
    CHECK(rb_str_eq(c.default_id, "roboticela"), "default id");
    CHECK(c.splash_min_ms == 2000, "splash min");
    CHECK(c.picker_key == RB_MOD_ALT, "alt picker");
    CHECK(rb_str_eq(c.targets[0].icon, "assets/os-roboticela.rbim"), "pic field maps to icon");
    CHECK(rb_catalog_find(&c, "recovery") == 1, "find recovery");
    CHECK(rb_manifest_parse("target only\n", &c) == RB_ERR_PARSE, "unclosed target");
}

static void test_policy(void)
{
    rb_catalog c;
    rb_env env;
    rb_zero(&c, sizeof(c));
    rb_zero(&env, sizeof(env));
    c.ntargets = 3;
    rb_strcpy(c.targets[0].id, sizeof(c.targets[0].id), "a");
    rb_strcpy(c.targets[1].id, sizeof(c.targets[1].id), "b");
    rb_strcpy(c.targets[2].id, sizeof(c.targets[2].id), "c");
    c.targets[0].present = c.targets[1].present = c.targets[2].present = 1;
    rb_strcpy(c.default_id, sizeof(c.default_id), "b");
    rb_strcpy(c.fallback_id, sizeof(c.fallback_id), "c");
    CHECK(rb_policy_intent(0, 0, NULL) == RB_BOOT_DEFAULT, "quiet boot");
    CHECK(rb_policy_intent(1, 0, NULL) == RB_BOOT_PICKER, "alt opens picker");
    CHECK(rb_policy_intent(0, 1, "a") == RB_BOOT_PICKER, "alt wins over next-boot");
    CHECK(rb_policy_intent(0, 0, "a") == RB_BOOT_ONCE, "next-boot once");
    CHECK(rb_policy_resolve_index(&c, &env, 0) == 1, "default b");
    rb_env_set(&env, "next_boot", "a");
    CHECK(rb_policy_resolve_index(&c, &env, 0) == 0, "once a");
    c.targets[1].present = 0;
    rb_env_set(&env, "next_boot", "");
    CHECK(rb_policy_resolve_index(&c, &env, 0) == 2, "missing default uses fallback");
    CHECK(rb_policy_next_index(&c, 0, 1) == 2, "skip missing");
}

static void test_env(void)
{
    rb_env e;
    rb_env_parse("ROBOENV1\nfoo=bar\nnext_boot=recovery\n", &e);
    CHECK(rb_str_eq(rb_env_get(&e, "foo"), "bar"), "env get");
    rb_env_set(&e, "foo", "baz");
    CHECK(rb_str_eq(rb_env_get(&e, "foo"), "baz"), "env set");
}

static void test_elf(void)
{
    char path[512];
    void *data = NULL;
    size_t len = 0;
    uint64_t entry = 0, phoff = 0;
    int phnum = 0;
    snprintf(path, sizeof(path), "%s/volumes/sys/kernel.elf", g_root);
    CHECK(t_read(path, &data, &len) == RB_OK, "read dummy kernel");
    CHECK(rb_elf64_info((uint8_t *)data, len, &entry, &phoff, &phnum) == RB_OK, "elf parse");
    CHECK(entry == 0x401000ull, "elf entry");
    CHECK(phnum == 1, "one phdr");
    rb_free(data);
}

static void test_loader(void)
{
    rb_catalog c;
    rb_handoff h;
    char man[512];
    snprintf(man, sizeof(man), "%s/config/roboboot.manifest", g_root);
    g_plat.root = g_root;
    rb_plat = &g_plat;
    CHECK(rb_manifest_load_path(man, &c) == RB_OK, "load packed manifest");
    rb_zero(&h, sizeof(h));
    CHECK(rb_loader_prepare(&c.targets[0], g_root, &h) == RB_OK, "prepare roboticela");
    CHECK(h.kernel.valid && h.kernel.size > 64, "kernel blob");
    CHECK(h.ramdisk.valid, "initrd blob");
    rb_handoff_free(&h);
}

static void run_session_until_phase(rb_session *ss, rb_phase want, uint64_t step, uint64_t limit)
{
    uint64_t start = g_time;
    while (ss->running && ss->phase != want && g_time - start < limit) {
        g_time += step;
        rb_session_tick(ss);
    }
}

static void test_session_quiet(void)
{
    rb_session ss;
    g_time = 0;
    g_last_boot[0] = 0;
    rb_zero(&g_in, sizeof(g_in));
    rb_session_init(&ss, &g_plat);
    ss.fb.w = 320;
    ss.fb.h = 200;
    ss.fb.px = (uint32_t *)rb_calloc((size_t)ss.fb.w * ss.fb.h * 4u);
    rb_session_tick(&ss); /* load + first splash */
    CHECK(ss.phase == RB_PHASE_SPLASH, "enters splash");
    g_time = 500;
    rb_session_tick(&ss);
    CHECK(ss.phase == RB_PHASE_SPLASH, "still splash at 500ms");
    CHECK(ss.load_visual < 0.3f, "bar not finished early");
    g_time = 2000;
    rb_session_tick(&ss);
    if (ss.phase == RB_PHASE_HANDOFF) rb_session_tick(&ss);
    CHECK(rb_str_eq(g_last_boot, "roboticela"), "quiet boot default OS");
    rb_session_free(&ss);
}

static void test_session_alt_menu(void)
{
    rb_session ss;
    g_time = 0;
    g_last_boot[0] = 0;
    rb_zero(&g_in, sizeof(g_in));
    rb_session_init(&ss, &g_plat);
    ss.fb.w = 640;
    ss.fb.h = 400;
    ss.fb.px = (uint32_t *)rb_calloc((size_t)ss.fb.w * ss.fb.h * 4u);
    rb_session_tick(&ss);
    CHECK(ss.phase == RB_PHASE_SPLASH, "enters splash");
    g_in.mods = RB_MOD_ALT;
    g_time = 500;
    rb_session_tick(&ss);
    CHECK(ss.phase == RB_PHASE_SPLASH, "Alt alone stays on splash");
    CHECK(!ss.alt_latched, "Alt alone does not latch picker");
    g_in.mods = RB_MOD_ALT;
    g_in.nchars = 1;
    g_in.chars[0] = 'm';
    g_time = 2000;
    rb_session_tick(&ss);
    CHECK(ss.alt_latched, "Alt+M latches picker");
    CHECK(ss.phase == RB_PHASE_PICKER, "Alt+M opens picker");
    g_in.nkeys = 1;
    g_in.keys[0] = RB_KEY_RIGHT;
    g_time += 16;
    rb_session_tick(&ss);
    CHECK(ss.selected != 0, "right arrow moves selection");
    g_in.nkeys = 1;
    g_in.keys[0] = RB_KEY_ENTER;
    g_time += 16;
    rb_session_tick(&ss);
    if (ss.phase == RB_PHASE_HANDOFF) rb_session_tick(&ss);
    CHECK(g_last_boot[0] != 0, "picker enter boots a target");
    rb_session_free(&ss);
}

int main(int argc, char **argv)
{
    if (argc > 1) rb_strcpy(g_root, sizeof(g_root), argv[1]);
    else rb_strcpy(g_root, sizeof(g_root), ".");
    g_plat.root = g_root;
    rb_plat = &g_plat;

    test_palette();
    test_sha256();
    test_lock();
    test_splash_math();
    test_manifest();
    test_policy();
    test_env();
    test_elf();
    test_loader();
    test_session_quiet();
    test_session_alt_menu();

    printf("RoboBoot tests: %d passed, %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
