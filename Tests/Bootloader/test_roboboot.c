#include "roboboot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int gFails = 0;
static int gPass = 0;
static uint64_t gTime = 0;
static Input gIn;
static char gRoot[512];
static char gLastBoot[64];

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

static uint64_t TNow(void) { return gTime; }
static void TSleep(uint32_t ms) { gTime += ms; }
static void TPoll(Input *out) { *out = gIn; Zero(&gIn, sizeof(gIn)); }
static int TPresent(Surface *s) { (void)s; return 1; }
static void TLog(const char *m) { (void)m; }

static RoboRbStatus TRead(const char *path, void **data, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long sz;
    void *buf;
    if (!f) return RbErrIo;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return RbErrIo; }
    buf = Alloc((size_t)sz + 1);
    if (!buf) { fclose(f); return RbErrNomem; }
    if (sz && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        RoboRbFree(buf);
        fclose(f);
        return RbErrIo;
    }
    fclose(f);
    *data = buf;
    *len = (size_t)sz;
    return RbOk;
}

static RoboRbStatus TWrite(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return RbErrIo;
    if (len && fwrite(data, 1, len, f) != len) { fclose(f); return RbErrIo; }
    fclose(f);
    return RbOk;
}

static RoboRbStatus TList(const char *path, void (*cb)(const char *name, int isDir, void *user), void *user)
{
#ifdef _WIN32
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return RbOk;
    do {
        cb(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, user);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    (void)path; (void)cb; (void)user;
#endif
    return RbOk;
}

static RoboRbStatus TBoot(const Target *t, const Handoff *h)
{
    Strcpy(gLastBoot, sizeof(gLastBoot), t->id);
    (void)h;
    return RbOk;
}

static RoboRbStatus TFw(void) { Strcpy(gLastBoot, sizeof(gLastBoot), "firmware"); return RbOk; }
static RoboRbStatus TReboot(void) { Strcpy(gLastBoot, sizeof(gLastBoot), "reboot"); return RbOk; }
static RoboRbStatus THalt(void) { Strcpy(gLastBoot, sizeof(gLastBoot), "halt"); return RbOk; }

static Platform gPlat = {
    TNow, TSleep, TPoll, TPresent, TRead, TWrite, TList, TLog,
    TBoot, TFw, TReboot, THalt, NULL
};

static void TestPalette(void)
{
    CHECK(Token(RbTokenBrandPrimary, 255) == Rgba(0x3B, 0x9E, 0xFF, 255), "brand primary dark");
    CHECK(Token(RbTokenBgPrimary, 255) == Rgba(0, 0, 0, 255), "bg primary dark");
    CHECK(Token(RbTokenTextSecondary, 255) == Rgba(0xA1, 0xA1, 0xA6, 255), "text secondary dark");
    CHECK(Token(RbTokenDanger, 255) == Rgba(0xFF, 0x45, 0x3A, 255), "danger dark");
    CHECK(Tokenf(RbTokenTextPrimary, 1.f) == Rgba(255, 255, 255, 255), "token fade");
}

static void TestSha256(void)
{
    uint8_t out[32];
    char hex[72];
    RoboRbSha256("abc", 3, out);
    Hex(out, 32, hex, sizeof(hex));
    CHECK(StrEq(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "sha256(abc)");
}

static void TestLock(void)
{
    uint8_t salt[4] = {1, 2, 3, 4};
    uint8_t hash[32];
    char hex[72], spec[128];
    LockHash("ela", salt, 4, 16, hash);
    Hex(hash, 32, hex, sizeof(hex));
    snprintf(spec, sizeof(spec), "16:01020304:%s", hex);
    CHECK(LockVerify(spec, "ela"), "lock accepts password");
    CHECK(!LockVerify(spec, "nope"), "lock rejects wrong password");
}

static void TestSplashMath(void)
{
    float mid;
    CHECK(SplashVisual(1.f, 0, 2000) == 0.f, "fast load starts empty");
    mid = SplashVisual(1.f, 1000, 2000);
    CHECK(mid > 0.70f && mid < 0.80f, "ease-out fills past linear midpoint");
    CHECK(SplashVisual(1.f, 2000, 2000) >= 0.999f, "fast load fills at min duration");
    CHECK(SplashVisual(0.25f, 2000, 2000) == 0.25f, "visual never exceeds real progress");
    CHECK(!SplashComplete(1.f, 500, 2000), "not complete before min ms");
    CHECK(SplashComplete(1.f, 2000, 2000), "complete at min ms when loaded");
    CHECK(!SplashComplete(0.9f, 5000, 2000), "not complete until resources finish");
}

static void TestManifest(void)
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
    Catalog c;
    CHECK(ManifestParse(txt, &c) == RbOk, "parse ok");
    CHECK(c.ntargets == 2, "two targets");
    CHECK(StrEq(c.defaultId, "roboticela"), "default id");
    CHECK(c.splashMinMs == 2000, "splash min");
    CHECK(c.pickerKey == RbModAlt, "alt picker");
    CHECK(StrEq(c.targets[0].icon, "assets/os-roboticela.rbim"), "pic field maps to icon");
    CHECK(CatalogFind(&c, "recovery") == 1, "find recovery");
    CHECK(ManifestParse("target only\n", &c) == RbErrParse, "unclosed target");
}

static void TestPolicy(void)
{
    Catalog c;
    Env env;
    Zero(&c, sizeof(c));
    Zero(&env, sizeof(env));
    c.ntargets = 3;
    Strcpy(c.targets[0].id, sizeof(c.targets[0].id), "a");
    Strcpy(c.targets[1].id, sizeof(c.targets[1].id), "b");
    Strcpy(c.targets[2].id, sizeof(c.targets[2].id), "c");
    c.targets[0].present = c.targets[1].present = c.targets[2].present = 1;
    Strcpy(c.defaultId, sizeof(c.defaultId), "b");
    Strcpy(c.fallbackId, sizeof(c.fallbackId), "c");
    CHECK(PolicyIntent(0, 0, NULL) == RbBootDefault, "quiet boot");
    CHECK(PolicyIntent(1, 0, NULL) == RbBootPicker, "alt opens picker");
    CHECK(PolicyIntent(0, 1, "a") == RbBootPicker, "alt wins over next-boot");
    CHECK(PolicyIntent(0, 0, "a") == RbBootOnce, "next-boot once");
    CHECK(PolicyResolveIndex(&c, &env, 0) == 1, "default b");
    EnvSet(&env, "next_boot", "a");
    CHECK(PolicyResolveIndex(&c, &env, 0) == 0, "once a");
    c.targets[1].present = 0;
    EnvSet(&env, "next_boot", "");
    CHECK(PolicyResolveIndex(&c, &env, 0) == 2, "missing default uses fallback");
    CHECK(PolicyNextIndex(&c, 0, 1) == 2, "skip missing");
}

static void TestEnv(void)
{
    Env e;
    EnvParse("ROBOENV1\nfoo=bar\nnext_boot=recovery\n", &e);
    CHECK(StrEq(EnvGet(&e, "foo"), "bar"), "env get");
    EnvSet(&e, "foo", "baz");
    CHECK(StrEq(EnvGet(&e, "foo"), "baz"), "env set");
    CHECK(!RoboRbDebugEnabled(&e), "debug defaults off");
    DebugSet(&e, 1);
    CHECK(RoboRbDebugEnabled(&e), "debug on");
    DebugSet(&e, 0);
    CHECK(!RoboRbDebugEnabled(&e), "debug off");
    EnvParse("ROBOENV1\r\ndebug=1\r\n", &e);
    CHECK(RoboRbDebugEnabled(&e), "debug survives crlf");
    {
        char path[512];
        Env saved, loaded;
        snprintf(path, sizeof(path), "%s/build/test-persist.env", gRoot);
        Zero(&saved, sizeof(saved));
        Strcpy(saved.path, sizeof(saved.path), path);
        DebugSet(&saved, 1);
        EnvSet(&saved, "last_boot", "roboticela");
        CHECK(EnvSave(&saved) == RbOk, "env save debug on");
        CHECK(EnvLoad(path, &loaded) == RbOk, "env reload debug on");
        CHECK(RoboRbDebugEnabled(&loaded), "debug persists across load");
        CHECK(StrEq(EnvGet(&loaded, "last_boot"), "roboticela"), "last_boot persists");
        DebugSet(&loaded, 0);
        CHECK(EnvSave(&loaded) == RbOk, "env save debug off");
        CHECK(EnvLoad(path, &saved) == RbOk, "env reload debug off");
        CHECK(!RoboRbDebugEnabled(&saved), "debug off persists across load");
    }
}

static void TestElf(void)
{
    char path[512];
    void *data = NULL;
    size_t len = 0;
    uint64_t entry = 0, phoff = 0;
    int phnum = 0;
    snprintf(path, sizeof(path), "%s/volumes/sys/kernel.elf", gRoot);
    CHECK(TRead(path, &data, &len) == RbOk, "read dummy kernel");
    CHECK(Elf64Info((uint8_t *)data, len, &entry, &phoff, &phnum) == RbOk, "elf parse");
    CHECK(entry == 0x401000ull, "elf entry");
    CHECK(phnum >= 1, "load segment");
    RoboRbFree(data);
}

static void TestLoader(void)
{
    Catalog c;
    Handoff h;
    char man[512];
    snprintf(man, sizeof(man), "%s/config/roboboot.manifest", gRoot);
    gPlat.root = gRoot;
    Plat = &gPlat;
    CHECK(ManifestLoadPath(man, &c) == RbOk, "load packed manifest");
    Zero(&h, sizeof(h));
    CHECK(LoaderPrepare(&c.targets[0], gRoot, &h) == RbOk, "prepare roboticela");
    CHECK(h.kernel.valid && h.kernel.size > 64, "kernel blob");
    CHECK(h.ramdisk.valid, "initrd blob");
    HandoffFree(&h);
}

static void DrainBoot(Session *ss)
{
    int n;
    for (n = 0; n < 64 && ss->running; n++) {
        if (ss->phase == RbPhaseKlog || ss->phase == RbPhaseDone)
            break;
        if (gLastBoot[0])
            break;
        gTime += 50;
        SessionTick(ss);
    }
}

static void TestSessionQuiet(void)
{
    Session ss;
    gTime = 0;
    gLastBoot[0] = 0;
    Zero(&gIn, sizeof(gIn));
    SessionInit(&ss, &gPlat);
    ss.fb.w = 320;
    ss.fb.h = 200;
    ss.fb.px = (uint32_t *)RoboRbCalloc((size_t)ss.fb.w * ss.fb.h * 4u);
    SessionTick(&ss); /* load + first splash */
    CHECK(ss.phase == RbPhaseSplash, "enters splash");
    gTime = 300;
    SessionTick(&ss);
    CHECK(ss.phase == RbPhaseSplash, "still splash at 300ms");
    CHECK(ss.loadVisual < 0.35f, "bar not finished early");
    gTime = 800;
    SessionTick(&ss);
    CHECK(ss.loadVisual > 0.39f && ss.loadVisual < 0.41f, "bootloader bar stops at handoff");
    DrainBoot(&ss);
    CHECK(StrEq(gLastBoot, "roboticela"), "quiet boot default OS");
    SessionFree(&ss);
}

static void TestSessionAltMenu(void)
{
    Session ss;
    gTime = 0;
    gLastBoot[0] = 0;
    Zero(&gIn, sizeof(gIn));
    SessionInit(&ss, &gPlat);
    ss.fb.w = 640;
    ss.fb.h = 400;
    ss.fb.px = (uint32_t *)RoboRbCalloc((size_t)ss.fb.w * ss.fb.h * 4u);
    SessionTick(&ss);
    CHECK(ss.phase == RbPhaseSplash, "enters splash");
    gIn.mods = RbModAlt;
    gTime = 500;
    SessionTick(&ss);
    CHECK(ss.phase == RbPhaseSplash, "Alt alone stays on splash");
    CHECK(!ss.altLatched, "Alt alone does not latch picker");
    gIn.mods = 0;
    gIn.nchars = 1;
    gIn.chars[0] = 'm';
    gTime = 600;
    SessionTick(&ss);
    CHECK(ss.phase == RbPhaseSplash, "M alone stays on splash");
    CHECK(!ss.altLatched, "M alone does not latch picker");
    gIn.mods = RbModAlt;
    gIn.nchars = 1;
    gIn.chars[0] = 'm';
    gTime = 800;
    SessionTick(&ss);
    CHECK(ss.altLatched, "Alt+M latches picker");
    CHECK(ss.phase == RbPhasePicker, "Alt+M opens picker");
    gIn.nkeys = 1;
    gIn.keys[0] = RbKeyRight;
    gTime += 16;
    SessionTick(&ss);
    CHECK(ss.selected != 0, "right arrow moves selection");
    gIn.nkeys = 1;
    gIn.keys[0] = RbKeyEnter;
    gTime += 16;
    SessionTick(&ss);
    DrainBoot(&ss);
    CHECK(gLastBoot[0] != 0, "picker enter boots a target");
    SessionFree(&ss);
}

static void TestSessionSplashF8(void)
{
    Session ss;
    gTime = 0;
    gLastBoot[0] = 0;
    Zero(&gIn, sizeof(gIn));
    SessionInit(&ss, &gPlat);
    ss.fb.w = 640;
    ss.fb.h = 400;
    ss.fb.px = (uint32_t *)RoboRbCalloc((size_t)ss.fb.w * ss.fb.h * 4u);
    SessionTick(&ss);
    CHECK(ss.phase == RbPhaseSplash, "F8 test splash");
    gIn.nkeys = 1;
    gIn.keys[0] = RbKeyF8;
    gTime = 500;
    SessionTick(&ss);
    CHECK(ss.altLatched, "F8 latches picker during splash");
    CHECK(ss.phase == RbPhaseSplash, "F8 waits for splash");
    gTime = 800;
    SessionTick(&ss);
    CHECK(ss.phase == RbPhasePicker, "F8 opens picker after splash");
    SessionFree(&ss);
}

int main(int argc, char **argv)
{
    if (argc > 1) Strcpy(gRoot, sizeof(gRoot), argv[1]);
    else Strcpy(gRoot, sizeof(gRoot), ".");
    gPlat.root = gRoot;
    Plat = &gPlat;

    TestPalette();
    TestSha256();
    TestLock();
    TestSplashMath();
    TestManifest();
    TestPolicy();
    TestEnv();
    TestElf();
    TestLoader();
    TestSessionQuiet();
    TestSessionAltMenu();
    TestSessionSplashF8();

    printf("RoboBoot tests: %d passed, %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
