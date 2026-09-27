#ifndef ROBOBOOT_H
#define ROBOBOOT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define RbVersionStr "1.0.0"
#define RbMaxTargets 32
#define RbMaxPath 512
#define RbMaxTitle 96
#define RbMaxArgs 512
#define RbMaxId 48
#define RbMaxModules 8
#define RbMaxEnv 64
#define RbMaxLine 1024
#define RbEnvBlock 4096
#define RbSplashMinMs 800
#define RbSplashHandoff 0.40f
#define RbBootlogCols 1024

typedef struct {
    char *text;
    uint32_t ms;
    uint8_t kind;
} LogLine;

/* Roboticela palette, dark mode. Boot is one dark view — do not mix light-mode hex. */
#define RbTokenBrandPrimary          0x3B9EFFu
#define RbTokenBrandPrimaryHover    0x5CADFFu
#define RbTokenBrandPrimaryActive   0x2E8CE6u
#define RbTokenTextPrimary           0xFFFFFFu
#define RbTokenTextSecondary         0xA1A1A6u
#define RbTokenTextTertiary          0x98989Du
#define RbTokenTextDisabled          0x48484Au
#define RbTokenTextOnBrand          0xFFFFFFu
#define RbTokenBgPrimary             0x000000u
#define RbTokenBgSecondary           0x1C1C1Eu
#define RbTokenBgTertiary            0x2C2C2Eu
#define RbTokenBgElevated            0x2C2C2Eu
#define RbTokenBgHover               0x2C2C2Eu
#define RbTokenBgActive              0x3A3A3Cu
#define RbTokenBorderDefault         0x38383Au
#define RbTokenBorderSubtle          0x2C2C2Eu
#define RbTokenBorderStrong          0x48484Au
#define RbTokenBorderFocus           0x3B9EFFu
#define RbTokenDanger                 0xFF453Au
#define RbTokenSuccess                0x30D158u
#define RbTokenWarning                0xFF9F0Au
#define RbTokenAccentTeal            0x2DD4C4u
#define RbTokenBrandSubtleA         0.15f
#define RbTokenBrandFocusA          0.30f

typedef enum {
    RbOk = 0,
    RbErr = -1,
    RbErrNomem = -2,
    RbErrIo = -3,
    RbErrParse = -4,
    RbErrNotfound = -5,
    RbErrAuth = -6,
    RbErrFormat = -7,
    RbErrUnsupported = -8,
    RbErrCancel = -9,
    RbErrFail = -10
} RoboRbStatus;

typedef enum {
    RbKindKernel = 0,
    RbKindLinux,
    RbKindMultiboot2,
    RbKindEfiChain,
    RbKindFirmware,
    RbKindReboot,
    RbKindHalt
} Kind;

typedef enum {
    RbKeyNone = 0,
    RbKeyLeft,
    RbKeyRight,
    RbKeyUp,
    RbKeyDown,
    RbKeyEnter,
    RbKeyEsc,
    RbKeyBackspace,
    RbKeyTab,
    RbKeyF10,
    RbKeyF8,
    RbKeyConsole,
    RbKeyChar
} Key;

enum {
    RbModAlt = 1u << 0,
    RbModShift = 1u << 1,
    RbModCtrl = 1u << 2
};

typedef struct {
    uint32_t mods;
    int keys[24];
    int nkeys;
    int chars[8];
    int nchars;
    int quit;
} Input;

typedef struct {
    int w;
    int h;
    uint32_t *px;
    int damageX;
    int damageY;
    int damageW;
    int damageH;
} Surface;

typedef struct {
    int w;
    int h;
    uint32_t *px;
} Image;

typedef struct {
    uint16_t x, y;
    int16_t w, h;
    uint16_t advance;
    int16_t offx, offy;
} Glyph;

typedef struct {
    int px;
    int first, last, count;
    int atlasW, atlasH;
    Glyph glyphs[128];
    uint8_t *atlas;
} Font;

typedef struct {
    char id[RbMaxId];
    char title[RbMaxTitle];
    Kind kind;
    char icon[RbMaxPath];
    char volume[RbMaxPath];
    char kernel[RbMaxPath];
    char ramdisk[RbMaxPath];
    char chain[RbMaxPath];
    char args[RbMaxArgs];
    char modules[RbMaxModules][RbMaxPath];
    int nmodules;
    int hidden;
    int restricted;
    char lock[192];
    int hotkey;
    int present;
} Target;

typedef struct {
    int version;
    int splashMinMs;
    char splashLogo[RbMaxPath];
    uint32_t pickerKey;
    int pickerTimeoutS;
    char pickerHint[RbMaxTitle];
    char defaultId[RbMaxId];
    char fallbackId[RbMaxId];
    int saveLast;
    char envPath[RbMaxPath];
    Target targets[RbMaxTargets];
    int ntargets;
} Catalog;

typedef struct {
    char keys[RbMaxEnv][64];
    char vals[RbMaxEnv][192];
    int n;
    char path[RbMaxPath];
} Env;

typedef struct {
    char name[64];
    char path[RbMaxPath];
    char fs[16];
    int hasKernel;
    int hasEfi;
} Volume;

typedef struct {
    Volume vols[16];
    int nvols;
} Probe;

typedef struct {
    uint8_t *bytes;
    size_t size;
    uint64_t entry;
    uint64_t phys;
    int valid;
} ImageBlob;

typedef struct {
    ImageBlob kernel;
    ImageBlob ramdisk;
    ImageBlob extras[RbMaxModules];
    int nextras;
    char cmdline[RbMaxArgs];
    Kind protocol;
    int debug;
    int debugTermX, debugTermY, debugTermW, debugTermH;
    int splashBarX, splashBarY, splashBarW, splashBarH;
    uint32_t splashProgress;
} Handoff;

typedef struct Platform {
    uint64_t (*NowMs)(void);
    void (*SleepMs)(uint32_t ms);
    void (*PollInput)(Input *out);
    int (*present)(Surface *s);
    RoboRbStatus (*RoboReadFile)(const char *path, void **data, size_t *len);
    RoboRbStatus (*RoboWriteFile)(const char *path, const void *data, size_t len);
    RoboRbStatus (*ListDir)(const char *path, void (*cb)(const char *name, int isDir, void *user), void *user);
    void (*log)(const char *msg);
    RoboRbStatus (*boot)(const Target *t, const Handoff *h);
    RoboRbStatus (*FirmwareSetup)(void);
    RoboRbStatus (*reboot)(void);
    RoboRbStatus (*halt)(void);
    const char *root;
} Platform;

typedef enum {
    RbPhaseLoad = 0,
    RbPhaseSplash,
    RbPhasePicker,
    RbPhaseAuth,
    RbPhaseConsole,
    RbPhaseSettings,
    RbPhaseVerbose,
    RbPhaseKlog,
    RbPhaseHandoff,
    RbPhaseDone
} Phase;

typedef struct {
    const Platform *plat;
    Catalog catalog;
    Env env;
    Probe probe;
    Surface fb;
    Image logo;
    Image icons[RbMaxTargets];
    Font fontTitle;
    Font fontBody;
    Font fontCaption;
    Input input;
    Phase phase;
    int forceMenu;
    int windowed;
    float loadReal;
    float loadVisual;
    uint64_t splashStartMs;
    int splashDone;
    int splashInited;
    int splashBgReady;
    int splashBarX, splashBarY, splashBarW, splashBarH;
    int splashTicks;
    int logoFitted;
    int iconsLoaded;
    int iconsFitted;
    int pickerDrawn;
    int settingsDrawn;
    int altLatched;
    int selected;
    float selAnim;
    int timeoutLeftMs;
    uint64_t lastMs;
    char authBuf[64];
    int authLen;
    int authFail;
    char consoleIn[256];
    int consoleLen;
    char consoleLog[16][240];
    int consoleLines;
    int running;
    int bootIndex;
    Handoff handoff;
    float fade;
    LogLine *bootLog;
    int bootLogN;
    int bootLogCap;
    int bootLogSeq;
    int bootLogShown;
    int bootLogFollow;
    int bootLogTop;
} Session;

extern const Platform *Plat;

void *Alloc(size_t n);
void *RoboRbCalloc(size_t n);
void RoboRbFree(void *p);
void Zero(void *p, size_t n);
void Copy(void *dst, const void *src, size_t n);
int Strlen(const char *s);
int Strcmp(const char *a, const char *b);
int Strncmp(const char *a, const char *b, size_t n);
void Strcpy(char *dst, size_t cap, const char *src);
int StrEq(const char *a, const char *b);
int Endswith(const char *s, const char *suf);
void Join(char *dst, size_t cap, const char *a, const char *b);
void Logf(const char *fmt, ...);
uint32_t Rgba(int r, int g, int b, int a);
uint32_t Token(uint32_t rgb, int alpha);
uint32_t Tokenf(uint32_t rgb, float fade);
uint32_t Blend(uint32_t dst, uint32_t src);
float Clampf(float v, float lo, float hi);
float Lerpf(float a, float b, float t);
float Smoothstep(float t);
float EaseApple(float t);

RoboRbStatus RoboRbSha256(const void *data, size_t len, uint8_t out[32]);
void Hex(const uint8_t *in, size_t n, char *out, size_t cap);
int Unhex(const char *in, uint8_t *out, size_t cap);
RoboRbStatus LockHash(const char *password, const uint8_t *salt, size_t saltLen, int rounds, uint8_t out[32]);
int LockVerify(const char *spec, const char *password);

RoboRbStatus ImageLoadMem(const void *data, size_t len, Image *out);
RoboRbStatus ImageResize(const Image *src, int w, int h, Image *dst);
void ImageFree(Image *im);
RoboRbStatus FontLoadMem(const void *data, size_t len, Font *out);
void FontFree(Font *f);

void GfxClear(Surface *s, uint32_t c);
void FbDamageFull(Surface *s);
void GfxDot(Surface *s, int x, int y, uint32_t c);
void GfxFillBox(Surface *s, int x, int y, int w, int h, uint32_t c);
void GfxFillRect(Surface *s, float x, float y, float w, float h, uint32_t c);
void GfxRoundRect(Surface *s, float x, float y, float w, float h, float r, uint32_t c);
void GfxRoundRectStroke(Surface *s, float x, float y, float w, float h, float r, float t, uint32_t c);
void GfxCircle(Surface *s, float cx, float cy, float r, uint32_t c);
void GfxRing(Surface *s, float cx, float cy, float r, float t, uint32_t c);
void GfxBlit(Surface *s, const Image *im, float x, float y, float w, float h, float alpha);
int GfxTextWidth(const Font *f, const char *text);
void GfxText(Surface *s, const Font *f, float x, float y, const char *text, uint32_t c);
void GfxTextCenter(Surface *s, const Font *f, float cx, float y, const char *text, uint32_t c);

RoboRbStatus ManifestParse(const char *text, Catalog *out);
RoboRbStatus ManifestLoadPath(const char *path, Catalog *out);
int CatalogFind(const Catalog *c, const char *id);
Kind KindFromName(const char *s);
const char *KindName(Kind k);

RoboRbStatus EnvParse(const char *text, Env *out);
RoboRbStatus EnvLoad(const char *path, Env *out);
RoboRbStatus EnvSave(const Env *env);
const char *EnvGet(const Env *env, const char *key);
void EnvSet(Env *env, const char *key, const char *val);

RoboRbStatus ProbeVolumes(const char *volumesRoot, Probe *out);
void ProbeApply(Catalog *c, const Probe *p);

float SplashVisual(float realProgress, uint64_t elapsedMs, int minMs);
int SplashComplete(float realProgress, uint64_t elapsedMs, int minMs);

typedef enum {
    RbBootDefault = 0,
    RbBootPicker,
    RbBootOnce
} BootIntent;

BootIntent PolicyIntent(int altHeld, int forceMenu, const char *nextBoot);
int PolicyResolveIndex(const Catalog *c, const Env *env, int fallback);
int PolicyNextIndex(const Catalog *c, int from, int dir);

#define RbMaxPhdrs 16

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} Phdr;

int RoboRbDebugEnabled(const Env *env);
void DebugSet(Env *env, int on);

RoboRbStatus LoaderPrepare(const Target *t, const char *root, Handoff *h);
void HandoffFree(Handoff *h);
RoboRbStatus Elf64Info(const uint8_t *data, size_t len, uint64_t *entry, uint64_t *phoff, int *phnum);
int Elf64Phdrs(const uint8_t *data, size_t len, Phdr *out, int cap);

void SplashDraw(Session *ss, float visual, float fade);
void PickerDraw(Session *ss);
void AuthDraw(Session *ss);
void SettingsDraw(Session *ss);
void VerboseDraw(Session *ss);
void KlogDraw(Session *ss);
void Bootlog(Session *ss, const char *line);
void Bootlogf(Session *ss, const char *fmt, ...);
void BootlogFreeze(void);
void BootlogRelease(Session *ss);
int BootlogHandoffBegin(void *buf, uint32_t size);
void BootlogHandoffInfo(uint32_t *used, uint32_t *count);
void DebugTermGeom(const Session *ss, float *x, float *y, float *w, float *h);
void DebugTermDraw(Session *ss);
void DebugFlush(Session *ss);
int DebugLogScroll(Session *ss, int delta);
void ConsoleDraw(Session *ss);
void ConsoleSubmit(Session *ss);
void ConsolePrint(Session *ss, const char *line);

void SessionInit(Session *ss, const Platform *plat);
void SessionFree(Session *ss);
RoboRbStatus SessionLoad(Session *ss);
RoboRbStatus SessionBoot(Session *ss);
int SessionTick(Session *ss);

#endif
