#include "efi.h"
#include "roboboot.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gGop;
static EFI_FILE_PROTOCOL *gRoot;
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *gInex;
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *gInexList[8];
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL *gCinList[8];
static int gInexN;
static int gCinN;
static uint32_t gPixfmt;
static int gW, gH;
static uint32_t *gFb;
static uint32_t gStride;
static uint32_t gMods;
static uint64_t gAltUntil;
static int gPickerChord;
static uint64_t gUsbKickMs;
static int gSelftest;
static int gKtest;
static int gMenu;
static int gSplashMs = -1;
static char gLastBoot[64];

static EFI_GUID kGopGuid = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
static EFI_GUID kLiGuid = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EFI_GUID kFsGuid = {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EFI_GUID kInexGuid = {0xdd9e7534, 0x7762, 0x4698, {0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa}};
static EFI_GUID kFileinfoGuid = {0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EFI_GUID kUsbIoGuid = {0x2B2F68D6, 0x0CD2, 0x4cb0, {0xB3, 0x62, 0x6F, 0x19, 0x1E, 0xA2, 0x2F, 0x96}};
static EFI_GUID kConinGuid = {0x387477c1, 0x69c7, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

int EfiFlagSelftest(void) { return gSelftest; }
int EfiFlagKtest(void) { return gKtest; }
int EfiFlagMenu(void) { return gMenu; }
int EfiFlagSplashMs(void) { return gSplashMs; }
const char *EfiLastBoot(void) { return gLastBoot; }

int EfiStartKernel(const Target *t, const Handoff *h, int debug);

static void AsciiToUcs2(const char *in, CHAR16 *out, int cap)
{
    int i = 0;
    if (!in) { out[0] = 0; return; }
    while (in[i] && i + 1 < cap) {
        char c = in[i];
        out[i] = (CHAR16)((c == '/') ? '\\' : (unsigned char)c);
        i++;
    }
    out[i] = 0;
}

static void Ucs2ToAscii(const CHAR16 *in, char *out, int cap)
{
    int i = 0;
    if (!in) { out[0] = 0; return; }
    while (in[i] && i + 1 < cap) {
        out[i] = (char)(in[i] & 0xFF);
        i++;
    }
    out[i] = 0;
}

static uint64_t gStallMs;

static uint64_t EfiNowMs(void)
{
    return gStallMs + 1;
}

static void EfiSleepMs(uint32_t ms)
{
    if (gBS && ms) gBS->Stall((UINTN)ms * 1000ull);
    gStallMs += ms ? ms : 1;
}

static int MapKey(EFI_KEY_DATA *kd)
{
    uint16_t scan = kd->Key.ScanCode;
    CHAR16 ch = kd->Key.UnicodeChar;
    if (scan == SCAN_LEFT) return RbKeyLeft;
    if (scan == SCAN_RIGHT) return RbKeyRight;
    if (scan == SCAN_UP) return RbKeyUp;
    if (scan == SCAN_DOWN) return RbKeyDown;
    if (scan == SCAN_ESC) return RbKeyEsc;
    if (scan == SCAN_F8) return RbKeyF8;
    if (scan == SCAN_F10) return RbKeyF10;
    if (ch == 0x0D || ch == 0x0A) return RbKeyEnter;
    if (ch == 0x08) return RbKeyBackspace;
    if (ch == 0x09) return RbKeyTab;
    if (ch == '`' || ch == '~') return RbKeyConsole;
    return RbKeyNone;
}

static int IsAltShift(uint32_t sh)
{
    return (sh & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED)) != 0;
}

static void NoteAlt(void)
{
    gMods |= RbModAlt;
    gAltUntil = gStallMs + 1500;
}

static void ApplyShift(uint32_t sh, CHAR16 ch, uint16_t scan)
{
    int alt = IsAltShift(sh);
    int valid = (sh & EFI_SHIFT_STATE_VALID) != 0;
    /* USB hub keyboards often omit VALID, or send M without repeating Alt bits.
       Do not clear Alt on empty key-up packets — those are common on hubs. */
    if (alt || scan == 0x38)
        NoteAlt();
    if (valid) {
        if (sh & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) gMods |= RbModShift;
        if (sh & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) gMods |= RbModCtrl;
    }
    if (gAltUntil && gStallMs > gAltUntil) {
        gMods &= ~RbModAlt;
        gAltUntil = 0;
    }
}

static void AddChar(Input *out, int ch)
{
    int i;
    if (!out || out->nchars >= 8) return;
    for (i = 0; i < out->nchars; i++)
        if (out->chars[i] == ch) return;
    out->chars[out->nchars++] = ch;
}

static void AddKey(Input *out, int key)
{
    if (!out || key == RbKeyNone || out->nkeys >= 24) return;
    out->keys[out->nkeys++] = key;
}

static EFI_STATUS EFIAPI EfiOnAlt(EFI_KEY_DATA *kd)
{
    (void)kd;
    NoteAlt();
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI EfiOnM(EFI_KEY_DATA *kd)
{
    uint32_t sh = kd ? kd->KeyState.KeyShiftState : 0;
    if (IsAltShift(sh) || (gMods & RbModAlt) || (gAltUntil && gStallMs <= gAltUntil)) {
        gPickerChord = 1;
        NoteAlt();
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI EfiOnPicker(EFI_KEY_DATA *kd)
{
    (void)kd;
    gPickerChord = 1;
    NoteAlt();
    return EFI_SUCCESS;
}

static void EfiNotify(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *ex, CHAR16 ch, uint16_t scan,
                      uint32_t shift, EFI_KEY_NOTIFY_FUNCTION fn)
{
    EFI_KEY_DATA want;
    void *cookie = 0;
    if (!ex || !ex->RegisterKeyNotify) return;
    memset(&want, 0, sizeof(want));
    want.Key.UnicodeChar = ch;
    want.Key.ScanCode = scan;
    want.KeyState.KeyShiftState = shift;
    ex->RegisterKeyNotify(ex, &want, fn, &cookie);
}

static void EfiRegisterPicker(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *ex)
{
    static const CHAR16 letters[2] = { 'm', 'M' };
    static const uint32_t alts[2] = { EFI_LEFT_ALT_PRESSED, EFI_RIGHT_ALT_PRESSED };
    int i, j;
    if (!ex || !ex->RegisterKeyNotify) return;
    for (i = 0; i < 2; i++) {
        EfiNotify(ex, 0, 0, EFI_SHIFT_STATE_VALID | alts[i], EfiOnAlt);
        EfiNotify(ex, letters[i], 0, 0, EfiOnM);
        for (j = 0; j < 2; j++)
            EfiNotify(ex, letters[i], 0, EFI_SHIFT_STATE_VALID | alts[j], EfiOnPicker);
    }
}

static void EfiConnectHandles(EFI_GUID *guid)
{
    UINTN n = 0, i;
    EFI_HANDLE *h = 0;
    if (!gBS || !gBS->LocateHandleBuffer || !gBS->ConnectController) return;
    if (gBS->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, guid, 0, &n, &h) != EFI_SUCCESS || !h)
        return;
    for (i = 0; i < n; i++)
        gBS->ConnectController(h[i], 0, 0, 1);
    gBS->FreePool(h);
}

static int EfiKickUsb(void)
{
    static int kicks;
    if (gSelftest) return 0;
    if (kicks >= 3) return 0;
    if (gUsbKickMs && gStallMs < gUsbKickMs + 800)
        return 0;
    gUsbKickMs = gStallMs ? gStallMs : 1;
    kicks++;
    Bootlogf(NULL, "USB  connect  pass %d", kicks);
    EfiConnectHandles(&kUsbIoGuid);
    if (gST && gST->ConsoleInHandle && gBS->ConnectController)
        gBS->ConnectController(gST->ConsoleInHandle, 0, 0, 1);
    return 1;
}

static int EfiAlreadyEx(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *ex)
{
    int j;
    for (j = 0; j < gInexN; j++)
        if (gInexList[j] == ex) return 1;
    return 0;
}

static void EfiAddEx(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *ex)
{
    if (!ex || gInexN >= 8 || EfiAlreadyEx(ex)) return;
    gInexList[gInexN++] = ex;
    if (!gInex) gInex = ex;
    EfiRegisterPicker(ex);
    Bootlogf(NULL, "Keyboard  SimpleTextInputEx  #%d", gInexN);
}

static void EfiAddCin(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *cin)
{
    int j;
    if (!cin || gCinN >= 8) return;
    for (j = 0; j < gCinN; j++)
        if (gCinList[j] == cin) return;
    gCinList[gCinN++] = cin;
    Bootlogf(NULL, "Keyboard  ConIn  #%d", gCinN);
}

static void EfiBindKeyboards(void)
{
    UINTN n = 0, i;
    EFI_HANDLE *h = 0;
    if (!gBS) return;
    if (gST && gST->ConsoleInHandle) {
        EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *ex = 0;
        gBS->HandleProtocol(gST->ConsoleInHandle, &kInexGuid, (void **)&ex);
        EfiAddEx(ex);
    }
    if (!gInex && gBS->LocateProtocol) {
        EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *ex = 0;
        gBS->LocateProtocol(&kInexGuid, 0, (void **)&ex);
        EfiAddEx(ex);
    }
    if (gST && gST->ConIn)
        EfiAddCin(gST->ConIn);
    if (!gBS->LocateHandleBuffer) return;
    if (gBS->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, &kInexGuid, 0, &n, &h) == EFI_SUCCESS && h) {
        for (i = 0; i < n && gInexN < 8; i++) {
            EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *ex = 0;
            if (gBS->HandleProtocol(h[i], &kInexGuid, (void **)&ex) == EFI_SUCCESS)
                EfiAddEx(ex);
        }
        gBS->FreePool(h);
    }
    n = 0;
    h = 0;
    if (gBS->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, &kConinGuid, 0, &n, &h) == EFI_SUCCESS && h) {
        for (i = 0; i < n && gCinN < 8; i++) {
            EFI_SIMPLE_TEXT_INPUT_PROTOCOL *cin = 0;
            if (gBS->HandleProtocol(h[i], &kConinGuid, (void **)&cin) == EFI_SUCCESS)
                EfiAddCin(cin);
        }
        gBS->FreePool(h);
    }
}

static void EfiPollEx(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *ex, Input *out)
{
    EFI_KEY_DATA kd;
    int reads = 0;
    if (!ex || !ex->ReadKeyStrokeEx) return;
    if (ex->WaitForKeyEx && gBS && gBS->CheckEvent)
        gBS->CheckEvent(ex->WaitForKeyEx);
    while (reads++ < 8 && ex->ReadKeyStrokeEx(ex, &kd) == EFI_SUCCESS) {
        CHAR16 ch = kd.Key.UnicodeChar;
        ApplyShift(kd.KeyState.KeyShiftState, ch, kd.Key.ScanCode);
        out->mods = gMods;
        if (ch == 'm' || ch == 'M')
            AddChar(out, 'm');
        else if (ch >= 32 && ch < 127)
            AddChar(out, (int)ch);
        AddKey(out, MapKey(&kd));
    }
}

static void EfiPollCin(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *cin, Input *out)
{
    EFI_INPUT_KEY k;
    int reads = 0;
    if (!cin || !cin->ReadKeyStroke) return;
    if (cin->WaitForKey && gBS && gBS->CheckEvent)
        gBS->CheckEvent(cin->WaitForKey);
    while (reads++ < 8 && cin->ReadKeyStroke(cin, &k) == EFI_SUCCESS) {
        EFI_KEY_DATA kd;
        memset(&kd, 0, sizeof(kd));
        kd.Key = k;
        if (k.ScanCode == 0x38)
            NoteAlt();
        AddKey(out, MapKey(&kd));
        if (k.UnicodeChar == 'm' || k.UnicodeChar == 'M')
            AddChar(out, 'm');
        else if (k.UnicodeChar >= 32 && k.UnicodeChar < 127)
            AddChar(out, (int)k.UnicodeChar);
    }
}

static void EfiPoll(Input *out)
{
    int i;
    Zero(out, sizeof(*out));
    if (!gSelftest && gStallMs < 6000) {
        if (EfiKickUsb())
            EfiBindKeyboards();
    }
    if (gAltUntil && gStallMs > gAltUntil) {
        gMods &= ~RbModAlt;
        gAltUntil = 0;
    }
    out->mods = gMods;
    if (gPickerChord) {
        gPickerChord = 0;
        NoteAlt();
        AddChar(out, 'm');
    }
    for (i = 0; i < gInexN; i++)
        EfiPollEx(gInexList[i], out);
    if (gInexN == 0 && gInex)
        EfiPollEx(gInex, out);
    for (i = 0; i < gCinN; i++)
        EfiPollCin(gCinList[i], out);
    if (gCinN == 0 && gST && gST->ConIn)
        EfiPollCin(gST->ConIn, out);
    out->mods = gMods;
}

static int EfiPresent(Surface *s)
{
    UINTN dw, dh, dx, dy, sx, sy, sw, sh;
    EFI_STATUS st;
    if (gSelftest) return 1;
    if (!s || !s->px || !gGop) return 0;
    dw = (UINTN)s->w;
    dh = (UINTN)s->h;
    dx = 0;
    dy = 0;
    if (gW > 0 && (int)dw > gW) dw = (UINTN)gW;
    if (gH > 0 && (int)dh > gH) dh = (UINTN)gH;
    if (gW > (int)dw) dx = (UINTN)((gW - (int)dw) / 2);
    if (gH > (int)dh) dy = (UINTN)((gH - (int)dh) / 2);
    sx = 0;
    sy = 0;
    sw = dw;
    sh = dh;
    if (s->damageW > 0 && s->damageH > 0) {
        int x0 = s->damageX;
        int y0 = s->damageY;
        int x1 = s->damageX + s->damageW;
        int y1 = s->damageY + s->damageH;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > (int)dw) x1 = (int)dw;
        if (y1 > (int)dh) y1 = (int)dh;
        if (x1 > x0 && y1 > y0) {
            sx = (UINTN)x0;
            sy = (UINTN)y0;
            sw = (UINTN)(x1 - x0);
            sh = (UINTN)(y1 - y0);
        }
    }
    if (gGop->Blt) {
        st = gGop->Blt(gGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)s->px, EfiBltBufferToVideo,
                        sx, sy, dx + sx, dy + sy, sw, sh, (UINTN)s->w * 4u);
        if (st == EFI_SUCCESS)
            return 1;
    }
    if (gFb && gStride) {
        int y;
        for (y = 0; y < (int)sh; y++) {
            uint32_t *src = s->px + ((int)sy + y) * s->w + (int)sx;
            uint32_t *dst = gFb + ((int)sy + y + (int)dy) * (int)gStride + (int)dx + (int)sx;
            int x, w = (int)sw;
            if (gPixfmt == PixelBlueGreenRedReserved8BitPerColor) {
                memcpy(dst, src, (size_t)w * 4u);
            } else {
                for (x = 0; x < w; x++) {
                    uint32_t p = src[x];
                    unsigned r = (p >> 16) & 255u;
                    unsigned b = p & 255u;
                    dst[x] = (p & 0xFF00FF00u) | (b << 16) | r;
                }
            }
        }
        return 1;
    }
    return 0;
}

static EFI_FILE_PROTOCOL *OpenPath(const char *path, uint64_t mode)
{
    CHAR16 wpath[RbMaxPath];
    EFI_FILE_PROTOCOL *file = 0;
    if (!gRoot || !path) return 0;
    while (*path == '/' || *path == '\\') path++;
    AsciiToUcs2(path, wpath, RbMaxPath);
    if (gRoot->Open(gRoot, &file, wpath, mode, 0) != EFI_SUCCESS)
        return 0;
    return file;
}

static RoboRbStatus EfiRead(const char *path, void **data, size_t *len)
{
    EFI_FILE_PROTOCOL *f;
    UINTN infoSz = sizeof(EFI_FILE_INFO) + 512;
    EFI_FILE_INFO *info;
    UINTN n;
    void *buf;
    EFI_STATUS st;
    f = OpenPath(path, EFI_FILE_MODE_READ);
    if (!f) return RbErrIo;
    info = (EFI_FILE_INFO *)malloc(infoSz);
    if (!info) { f->Close(f); return RbErrNomem; }
    st = f->GetInfo(f, &kFileinfoGuid, &infoSz, info);
    if (st != EFI_SUCCESS) {
        free(info);
        f->Close(f);
        return RbErrIo;
    }
    n = (UINTN)info->FileSize;
    free(info);
    buf = malloc(n ? n : 1);
    if (!buf) { f->Close(f); return RbErrNomem; }
    st = f->Read(f, &n, buf);
    f->Close(f);
    if (st != EFI_SUCCESS) {
        free(buf);
        return RbErrIo;
    }
    *data = buf;
    *len = (size_t)n;
    return RbOk;
}

static RoboRbStatus EfiWrite(const char *path, const void *data, size_t len)
{
    EFI_FILE_PROTOCOL *f;
    UINTN n = (UINTN)len;
    EFI_STATUS st;
    UINTN infoSz = sizeof(EFI_FILE_INFO) + 512;
    EFI_FILE_INFO *info;
    f = OpenPath(path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE);
    if (!f) return RbErrIo;
    info = (EFI_FILE_INFO *)malloc(infoSz);
    if (info) {
        memset(info, 0, infoSz);
        st = f->GetInfo(f, &kFileinfoGuid, &infoSz, info);
        if (st == EFI_SUCCESS && f->SetInfo) {
            info->FileSize = (uint64_t)len;
            f->SetInfo(f, &kFileinfoGuid, infoSz, info);
        }
        free(info);
    }
    if (f->SetPosition) f->SetPosition(f, 0);
    st = f->Write(f, &n, (void *)data);
    if (f->Flush) f->Flush(f);
    f->Close(f);
    if (gRoot && gRoot->Flush) gRoot->Flush(gRoot);
    if (st != EFI_SUCCESS || n != (UINTN)len) {
        EfiLog("RoboBoot env write failed");
        return RbErrIo;
    }
    return RbOk;
}

static RoboRbStatus EfiList(const char *path, void (*cb)(const char *name, int isDir, void *user), void *user)
{
    EFI_FILE_PROTOCOL *dir;
    if (!cb) return RbOk;
    if (!path || !path[0] || StrEq(path, "/") || StrEq(path, "\\"))
        dir = gRoot;
    else
        dir = OpenPath(path, EFI_FILE_MODE_READ);
    if (!dir) return RbOk;
    for (;;) {
        UINTN sz = sizeof(EFI_FILE_INFO) + 512;
        uint8_t raw[sizeof(EFI_FILE_INFO) + 512];
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)raw;
        char name[256];
        EFI_STATUS st;
        memset(raw, 0, sizeof(raw));
        st = dir->Read(dir, &sz, raw);
        if (st != EFI_SUCCESS || sz == 0) break;
        Ucs2ToAscii(info->FileName, name, sizeof(name));
        cb(name, (info->Attribute & EFI_FILE_DIRECTORY) != 0, user);
    }
    if (dir != gRoot) dir->Close(dir);
    return RbOk;
}

static void EfiLogPlat(const char *msg)
{
    EfiLog(msg);
}

static RoboRbStatus EfiBoot(const Target *t, const Handoff *h)
{
    if (gSelftest) {
        Bootlog(NULL, "Boot  selftest skip jump");
        return RbOk;
    }
    Bootlogf(NULL, "Boot  %s  %s  kernel %zu", t->id, KindName(t->kind),
                h && h->kernel.valid ? h->kernel.size : 0);
    Strcpy(gLastBoot, sizeof(gLastBoot), t->id);
    if (t->kind == RbKindKernel || t->kind == RbKindLinux || t->kind == RbKindMultiboot2)
        return EfiStartKernel(t, h, h && h->debug);
    Bootlog(NULL, "Boot  no kernel jump");
    return RbOk;
}

static RoboRbStatus EfiFw(void)
{
    Bootlog(NULL, "Firmware  setup");
    Strcpy(gLastBoot, sizeof(gLastBoot), "firmware");
    if (gRT) gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, 0);
    return RbOk;
}

static RoboRbStatus EfiReboot(void)
{
    Strcpy(gLastBoot, sizeof(gLastBoot), "reboot");
    if (gRT) gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, 0);
    return RbOk;
}

static RoboRbStatus EfiHalt(void)
{
    Strcpy(gLastBoot, sizeof(gLastBoot), "halt");
    if (gRT) gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, 0);
    return RbOk;
}

static void ParseFlags(const char *text)
{
    const char *p = text ? text : "";
    while (*p) {
        char line[128];
        int n = 0;
        while (p[n] && p[n] != '\n' && p[n] != '\r') n++;
        if (n > 127) n = 127;
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        p += n;
        while (*p == '\n' || *p == '\r') p++;
        if (StrEq(line, "selftest")) gSelftest = 1;
        else if (StrEq(line, "ktest")) gKtest = 1;
        else if (StrEq(line, "menu")) gMenu = 1;
        else if (Strncmp(line, "splash_ms ", 10) == 0) gSplashMs = atoi(line + 10);
    }
}

static int GopUsable(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info)
{
    if (!info) return 0;
    if (info->HorizontalResolution < 640 || info->VerticalResolution < 480) return 0;
    if (info->HorizontalResolution > 8192 || info->VerticalResolution > 8192) return 0;
    if (info->PixelFormat >= PixelFormatMax) return 0;
    return 1;
}

static void EfiHideFirmwareCursor(void)
{
    if (gBS && gBS->SetWatchdogTimer)
        gBS->SetWatchdogTimer(0, 0, 0, 0);
    if (gST && gST->ConOut && gST->ConOut->EnableCursor)
        gST->ConOut->EnableCursor(gST->ConOut, 0);
}

static void PickGopMode(void)
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
    if (!gGop || !gGop->Mode) return;
    info = gGop->Mode->Info;
    if (!GopUsable(info) && gGop->QueryMode) {
        UINTN sz = 0;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *q = 0;
        if (gGop->QueryMode(gGop, gGop->Mode->Mode, &sz, &q) == EFI_SUCCESS)
            info = q;
    }
    /* Keep the firmware's current mode. SetMode on a laptop panel often
       blanks the eDP output; high-res screens also fail a 1920 cap. */
    if (!GopUsable(info)) return;
    gW = (int)info->HorizontalResolution;
    gH = (int)info->VerticalResolution;
    gPixfmt = (uint32_t)info->PixelFormat;
    gStride = info->PixelsPerScanLine;
    if (info->PixelFormat < PixelBltOnly && gGop->Mode->FrameBufferBase)
        gFb = (uint32_t *)(uintptr_t)gGop->Mode->FrameBufferBase;
    else
        gFb = 0;
}

static void EfiTakeGraphics(void)
{
    gGop = 0;
    gFb = 0;
    gW = 0;
    gH = 0;
    gBS->LocateProtocol(&kGopGuid, 0, (void **)&gGop);
    if (!gGop && gST && gST->ConsoleOutHandle)
        gBS->HandleProtocol(gST->ConsoleOutHandle, &kGopGuid, (void **)&gGop);
    if (gGop) PickGopMode();
    if (gGop && gGop->Mode) {
        Bootlogf(NULL, "GOP  protocol  mode %u", (unsigned)gGop->Mode->Mode);
        Bootlogf(NULL, "GOP  %d x %d  stride %u  fmt %u",
                    gW, gH, (unsigned)gStride, (unsigned)gPixfmt);
        Bootlogf(NULL, "GOP  framebuffer  0x%llx  %u bytes/scan",
                    (unsigned long long)(gGop->Mode->FrameBufferBase ?
                                         (uint64_t)gGop->Mode->FrameBufferBase : 0),
                    (unsigned)(gStride * 4u));
        Bootlogf(NULL, "GOP  Blt %s  FB %s",
                    gGop->Blt ? "yes" : "no", gFb ? "mapped" : "none");
    } else
        Bootlog(NULL, "GOP  missing");
}

static Platform gPlat;

static int TryOpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs)
{
    EFI_FILE_PROTOCOL *root = 0;
    void *blob = 0;
    size_t blobLen = 0;
    if (!fs || fs->OpenVolume(fs, &root) != EFI_SUCCESS || !root)
        return 0;
    gRoot = root;
    if (EfiRead("config/roboboot.manifest", &blob, &blobLen) == RbOk) {
        Bootlogf(NULL, "ESP  manifest  %zu bytes", blobLen);
        free(blob);
        return 1;
    }
    Bootlog(NULL, "ESP  no manifest on this volume");
    if (EfiRead("qemu.flags", &blob, &blobLen) == RbOk) {
        Bootlogf(NULL, "ESP  qemu.flags  %zu bytes", blobLen);
        free(blob);
        return 1;
    }
    root->Close(root);
    gRoot = 0;
    return 0;
}

static int OpenEsp(EFI_HANDLE image)
{
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    UINTN count = 0;
    EFI_HANDLE *handles = 0;
    UINTN i;

    if (gBS->HandleProtocol(image, &kLiGuid, (void **)&li) == EFI_SUCCESS && li && li->DeviceHandle) {
        Bootlog(NULL, "ESP  LoadedImage  device handle");
        if (gBS->ConnectController)
            gBS->ConnectController(li->DeviceHandle, 0, 0, 1);
        if (gBS->HandleProtocol(li->DeviceHandle, &kFsGuid, (void **)&fs) == EFI_SUCCESS && fs) {
            if (TryOpenVolume(fs))
                return 1;
        }
        if (gBS->OpenProtocol(li->DeviceHandle, &kFsGuid, (void **)&fs, image, 0,
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL) == EFI_SUCCESS && fs) {
            if (TryOpenVolume(fs))
                return 1;
        }
    }

    if (!gBS->LocateHandleBuffer)
        return 0;
    if (gBS->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, &kFsGuid, 0, &count, &handles) != EFI_SUCCESS)
        return 0;
    Bootlogf(NULL, "ESP  scan  %u file systems", (unsigned)count);
    for (i = 0; i < count; i++) {
        fs = 0;
        if (gBS->HandleProtocol(handles[i], &kFsGuid, (void **)&fs) != EFI_SUCCESS || !fs)
            continue;
        if (TryOpenVolume(fs)) {
            gBS->FreePool(handles);
            return 1;
        }
    }
    if (handles)
        gBS->FreePool(handles);
    return 0;
}

int EfiPlatformInit(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    void *blob = 0;
    size_t blobLen = 0;

    EfiInitLib(image, st);
    if (!gBS) return 0;
    gStallMs = 0;
    Bootlog(NULL, "EfiMain");
    Bootlog(NULL, "Boot services");
    if (gRT)
        Bootlog(NULL, "Runtime services");
    if (gBS->SetWatchdogTimer) {
        gBS->SetWatchdogTimer(0, 0, 0, 0);
        Bootlog(NULL, "Watchdog  off");
    }

    gPlat.NowMs = EfiNowMs;
    gPlat.SleepMs = EfiSleepMs;
    gPlat.PollInput = EfiPoll;
    gPlat.present = EfiPresent;
    gPlat.RoboReadFile = EfiRead;
    gPlat.RoboWriteFile = EfiWrite;
    gPlat.ListDir = EfiList;
    gPlat.log = EfiLogPlat;
    gPlat.boot = EfiBoot;
    gPlat.FirmwareSetup = EfiFw;
    gPlat.reboot = EfiReboot;
    gPlat.halt = EfiHalt;
    gPlat.root = "";
    Plat = &gPlat;
    Bootlog(NULL, "Platform  table ready");

    if (!OpenEsp(image))
        Bootlog(NULL, "ESP  missing");
    else
        Bootlog(NULL, "ESP  mounted");

    if (EfiRead("qemu.flags", &blob, &blobLen) == RbOk) {
        char *text = (char *)malloc(blobLen + 1);
        Bootlogf(NULL, "Flags  qemu.flags  %zu bytes", blobLen);
        if (text) {
            memcpy(text, blob, blobLen);
            text[blobLen] = 0;
            ParseFlags(text);
            free(text);
        }
        free(blob);
        Bootlogf(NULL, "Flags  selftest=%d  ktest=%d  menu=%d  splash=%d",
                    gSelftest, gKtest, gMenu, gSplashMs);
    } else
        Bootlog(NULL, "Flags  none");

    EfiInstallBootEntry();

    if (!gSelftest) {
        Bootlog(NULL, "Graphics  take GOP");
        EfiHideFirmwareCursor();
        Bootlog(NULL, "Console  cursor off");
        EfiTakeGraphics();
        EfiKickUsb();
        EfiBindKeyboards();
        Bootlogf(NULL, "Keyboard  %d extended  %d simple", gInexN, gCinN);
    } else
        Bootlog(NULL, "Selftest  skip graphics");

    Bootlogf(NULL, "Ready  GOP %dx%d  ESP %s  menu %d",
                gW, gH, gRoot ? "ok" : "missing", gMenu);
    return gRoot != 0;
}

const Platform *EfiPlatform(void)
{
    return &gPlat;
}

void EfiFbSize(int *w, int *h)
{
    *w = gW > 0 ? gW : 1024;
    *h = gH > 0 ? gH : 768;
}

void EfiGraphicsInfo(uint64_t *fb, uint32_t *w, uint32_t *h, uint32_t *pitch, uint32_t *bpp, uint32_t *pixfmt)
{
    uint64_t addr = 0;
    uint32_t pw = (uint32_t)(gW > 0 ? gW : 0);
    uint32_t ph = (uint32_t)(gH > 0 ? gH : 0);
    uint32_t ppitch = gStride ? gStride * 4u : pw * 4u;
    if (gGop && gGop->Mode && gGop->Mode->FrameBufferBase)
        addr = (uint64_t)gGop->Mode->FrameBufferBase;
    else if (gFb)
        addr = (uint64_t)(uintptr_t)gFb;
    if (fb) *fb = addr;
    if (w) *w = pw;
    if (h) *h = ph;
    if (pitch) *pitch = ppitch;
    if (bpp) *bpp = 32;
    if (pixfmt) *pixfmt = (gPixfmt == PixelRedGreenBlueReserved8BitPerColor) ? 1u : 0u;
}

void EfiShutdown(void)
{
    if (gRT) gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, 0);
    for (;;) { }
}
