#include "efi.h"
#include "roboboot.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop;
static EFI_FILE_PROTOCOL *g_root;
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *g_inex;
static uint32_t g_pixfmt;
static int g_w, g_h;
static uint32_t *g_fb;
static uint32_t g_stride;
static uint32_t g_mods;
static int g_selftest;
static int g_menu;
static int g_splash_ms = -1;
static char g_last_boot[64];

static EFI_GUID k_gop_guid = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
static EFI_GUID k_li_guid = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EFI_GUID k_fs_guid = {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EFI_GUID k_inex_guid = {0xdd9e7534, 0x7762, 0x4698, {0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa}};
static EFI_GUID k_fileinfo_guid = {0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

int efi_flag_selftest(void) { return g_selftest; }
int efi_flag_menu(void) { return g_menu; }
int efi_flag_splash_ms(void) { return g_splash_ms; }
const char *efi_last_boot(void) { return g_last_boot; }

int efi_start_kernel(const rb_target *t, const rb_handoff *h, int debug);

static void ascii_to_ucs2(const char *in, CHAR16 *out, int cap)
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

static void ucs2_to_ascii(const CHAR16 *in, char *out, int cap)
{
    int i = 0;
    if (!in) { out[0] = 0; return; }
    while (in[i] && i + 1 < cap) {
        out[i] = (char)(in[i] & 0xFF);
        i++;
    }
    out[i] = 0;
}

static uint64_t g_stall_ms;

static uint64_t efi_now_ms(void)
{
    return g_stall_ms + 1;
}

static void efi_sleep_ms(uint32_t ms)
{
    if (gBS && ms) gBS->Stall((UINTN)ms * 1000ull);
    g_stall_ms += ms ? ms : 1;
}

static int map_key(EFI_KEY_DATA *kd)
{
    uint16_t scan = kd->Key.ScanCode;
    CHAR16 ch = kd->Key.UnicodeChar;
    if (scan == SCAN_LEFT) return RB_KEY_LEFT;
    if (scan == SCAN_RIGHT) return RB_KEY_RIGHT;
    if (scan == SCAN_UP) return RB_KEY_UP;
    if (scan == SCAN_DOWN) return RB_KEY_DOWN;
    if (scan == SCAN_ESC) return RB_KEY_ESC;
    if (scan == SCAN_F10) return RB_KEY_F10;
    if (ch == 0x0D || ch == 0x0A) return RB_KEY_ENTER;
    if (ch == 0x08) return RB_KEY_BACKSPACE;
    if (ch == 0x09) return RB_KEY_TAB;
    if (ch == '`' || ch == '~') return RB_KEY_CONSOLE;
    return RB_KEY_NONE;
}

static void efi_poll(rb_input *out)
{
    rb_zero(out, sizeof(*out));
    out->mods = g_mods;
    if (g_inex) {
        EFI_KEY_DATA kd;
        int reads = 0;
        while (reads++ < 8 && g_inex->ReadKeyStrokeEx(g_inex, &kd) == EFI_SUCCESS) {
            int key;
            uint32_t sh = kd.KeyState.KeyShiftState;
            CHAR16 ch = kd.Key.UnicodeChar;
            if (sh & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED | EFI_SHIFT_STATE_VALID)) {
                if (sh & EFI_SHIFT_STATE_VALID) g_mods = 0;
                if (sh & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED)) g_mods |= RB_MOD_ALT;
                if (sh & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) g_mods |= RB_MOD_SHIFT;
                if (sh & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) g_mods |= RB_MOD_CTRL;
                out->mods = g_mods;
            }
            if ((ch == 'm' || ch == 'M') && (g_mods & RB_MOD_ALT) && out->nchars < 8) {
                out->chars[out->nchars++] = 'm';
            } else if (ch >= 32 && ch < 127 && out->nchars < 8) {
                out->chars[out->nchars++] = (int)ch;
            }
            key = map_key(&kd);
            if (key != RB_KEY_NONE && out->nkeys < 24)
                out->keys[out->nkeys++] = key;
        }
    }
    if (gST && gST->ConIn && gST->ConIn->ReadKeyStroke) {
        EFI_INPUT_KEY k;
        int reads = 0;
        while (reads++ < 8 && gST->ConIn->ReadKeyStroke(gST->ConIn, &k) == EFI_SUCCESS) {
            CHAR16 ch = k.UnicodeChar;
            int key = RB_KEY_NONE;
            if (ch == 0x0D || ch == 0x0A) key = RB_KEY_ENTER;
            if (key != RB_KEY_NONE && out->nkeys < 24)
                out->keys[out->nkeys++] = key;
            if (ch == 'm' || ch == 'M') {
                int i, have = 0;
                for (i = 0; i < out->nchars; i++)
                    if (out->chars[i] == 'm' || out->chars[i] == 'M') have = 1;
                if (!have && out->nchars < 8)
                    out->chars[out->nchars++] = 'm';
            }
        }
    }
    out->mods = g_mods;
}

static int efi_present(rb_surface *s)
{
    UINTN dw, dh, dx, dy, sx, sy, sw, sh;
    EFI_STATUS st;
    if (g_selftest) return 1;
    if (!s || !s->px || !g_gop) return 0;
    dw = (UINTN)s->w;
    dh = (UINTN)s->h;
    dx = 0;
    dy = 0;
    if (g_w > 0 && (int)dw > g_w) dw = (UINTN)g_w;
    if (g_h > 0 && (int)dh > g_h) dh = (UINTN)g_h;
    if (g_w > (int)dw) dx = (UINTN)((g_w - (int)dw) / 2);
    if (g_h > (int)dh) dy = (UINTN)((g_h - (int)dh) / 2);
    sx = 0;
    sy = 0;
    sw = dw;
    sh = dh;
    if (s->damage_w > 0 && s->damage_h > 0) {
        int x0 = s->damage_x;
        int y0 = s->damage_y;
        int x1 = s->damage_x + s->damage_w;
        int y1 = s->damage_y + s->damage_h;
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
    if (g_gop->Blt) {
        st = g_gop->Blt(g_gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)s->px, EfiBltBufferToVideo,
                        sx, sy, dx + sx, dy + sy, sw, sh, (UINTN)s->w * 4u);
        if (st == EFI_SUCCESS)
            return 1;
    }
    if (g_fb && g_stride) {
        int y;
        for (y = 0; y < (int)sh; y++) {
            uint32_t *src = s->px + ((int)sy + y) * s->w + (int)sx;
            uint32_t *dst = g_fb + ((int)sy + y + (int)dy) * (int)g_stride + (int)dx + (int)sx;
            int x, w = (int)sw;
            if (g_pixfmt == PixelBlueGreenRedReserved8BitPerColor) {
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

static EFI_FILE_PROTOCOL *open_path(const char *path, uint64_t mode)
{
    CHAR16 wpath[RB_MAX_PATH];
    EFI_FILE_PROTOCOL *file = 0;
    if (!g_root || !path) return 0;
    while (*path == '/' || *path == '\\') path++;
    ascii_to_ucs2(path, wpath, RB_MAX_PATH);
    if (g_root->Open(g_root, &file, wpath, mode, 0) != EFI_SUCCESS)
        return 0;
    return file;
}

static rb_status efi_read(const char *path, void **data, size_t *len)
{
    EFI_FILE_PROTOCOL *f;
    UINTN info_sz = sizeof(EFI_FILE_INFO) + 512;
    EFI_FILE_INFO *info;
    UINTN n;
    void *buf;
    EFI_STATUS st;
    f = open_path(path, EFI_FILE_MODE_READ);
    if (!f) return RB_ERR_IO;
    info = (EFI_FILE_INFO *)malloc(info_sz);
    if (!info) { f->Close(f); return RB_ERR_NOMEM; }
    st = f->GetInfo(f, &k_fileinfo_guid, &info_sz, info);
    if (st != EFI_SUCCESS) {
        free(info);
        f->Close(f);
        return RB_ERR_IO;
    }
    n = (UINTN)info->FileSize;
    free(info);
    buf = malloc(n ? n : 1);
    if (!buf) { f->Close(f); return RB_ERR_NOMEM; }
    st = f->Read(f, &n, buf);
    f->Close(f);
    if (st != EFI_SUCCESS) {
        free(buf);
        return RB_ERR_IO;
    }
    *data = buf;
    *len = (size_t)n;
    return RB_OK;
}

static rb_status efi_write(const char *path, const void *data, size_t len)
{
    EFI_FILE_PROTOCOL *f;
    UINTN n = (UINTN)len;
    EFI_STATUS st;
    f = open_path(path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE);
    if (!f) return RB_ERR_IO;
    st = f->Write(f, &n, (void *)data);
    if (f->Flush) f->Flush(f);
    f->Close(f);
    return st == EFI_SUCCESS ? RB_OK : RB_ERR_IO;
}

static rb_status efi_list(const char *path, void (*cb)(const char *name, int is_dir, void *user), void *user)
{
    EFI_FILE_PROTOCOL *dir;
    if (!cb) return RB_OK;
    if (!path || !path[0] || rb_str_eq(path, "/") || rb_str_eq(path, "\\"))
        dir = g_root;
    else
        dir = open_path(path, EFI_FILE_MODE_READ);
    if (!dir) return RB_OK;
    for (;;) {
        UINTN sz = sizeof(EFI_FILE_INFO) + 512;
        uint8_t raw[sizeof(EFI_FILE_INFO) + 512];
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)raw;
        char name[256];
        EFI_STATUS st;
        memset(raw, 0, sizeof(raw));
        st = dir->Read(dir, &sz, raw);
        if (st != EFI_SUCCESS || sz == 0) break;
        ucs2_to_ascii(info->FileName, name, sizeof(name));
        cb(name, (info->Attribute & EFI_FILE_DIRECTORY) != 0, user);
    }
    if (dir != g_root) dir->Close(dir);
    return RB_OK;
}

static void efi_log_plat(const char *msg)
{
    efi_log(msg);
}

static rb_status efi_boot(const rb_target *t, const rb_handoff *h)
{
    rb_logf("QEMU handoff id=%s kind=%s kernel=%zu", t->id, rb_kind_name(t->kind),
            h && h->kernel.valid ? h->kernel.size : 0);
    rb_strcpy(g_last_boot, sizeof(g_last_boot), t->id);
    if (g_selftest) return RB_OK;
    if (t->kind == RB_KIND_KERNEL || t->kind == RB_KIND_LINUX || t->kind == RB_KIND_MULTIBOOT2)
        return efi_start_kernel(t, h, h && h->debug);
    return RB_OK;
}

static rb_status efi_fw(void)
{
    rb_logf("firmware setup (EFI)");
    rb_strcpy(g_last_boot, sizeof(g_last_boot), "firmware");
    if (gRT) gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, 0);
    return RB_OK;
}

static rb_status efi_reboot(void)
{
    rb_strcpy(g_last_boot, sizeof(g_last_boot), "reboot");
    if (gRT) gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, 0);
    return RB_OK;
}

static rb_status efi_halt(void)
{
    rb_strcpy(g_last_boot, sizeof(g_last_boot), "halt");
    if (gRT) gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, 0);
    return RB_OK;
}

static void parse_flags(const char *text)
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
        if (rb_str_eq(line, "selftest")) g_selftest = 1;
        else if (rb_str_eq(line, "menu")) g_menu = 1;
        else if (rb_strncmp(line, "splash_ms ", 10) == 0) g_splash_ms = atoi(line + 10);
    }
}

static int gop_usable(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info)
{
    if (!info) return 0;
    if (info->HorizontalResolution < 640 || info->VerticalResolution < 480) return 0;
    if (info->HorizontalResolution > 8192 || info->VerticalResolution > 8192) return 0;
    if (info->PixelFormat >= PixelFormatMax) return 0;
    return 1;
}

static void efi_hide_firmware_cursor(void)
{
    if (gBS && gBS->SetWatchdogTimer)
        gBS->SetWatchdogTimer(0, 0, 0, 0);
    if (gST && gST->ConOut && gST->ConOut->EnableCursor)
        gST->ConOut->EnableCursor(gST->ConOut, 0);
}

static void pick_gop_mode(void)
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
    if (!g_gop || !g_gop->Mode) return;
    info = g_gop->Mode->Info;
    if (!gop_usable(info) && g_gop->QueryMode) {
        UINTN sz = 0;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *q = 0;
        if (g_gop->QueryMode(g_gop, g_gop->Mode->Mode, &sz, &q) == EFI_SUCCESS)
            info = q;
    }
    /* Keep the firmware's current mode. SetMode on a laptop panel often
       blanks the eDP output; high-res screens also fail a 1920 cap. */
    if (!gop_usable(info)) return;
    g_w = (int)info->HorizontalResolution;
    g_h = (int)info->VerticalResolution;
    g_pixfmt = (uint32_t)info->PixelFormat;
    g_stride = info->PixelsPerScanLine;
    if (info->PixelFormat < PixelBltOnly && g_gop->Mode->FrameBufferBase)
        g_fb = (uint32_t *)(uintptr_t)g_gop->Mode->FrameBufferBase;
    else
        g_fb = 0;
}

static void efi_take_graphics(void)
{
    g_gop = 0;
    g_fb = 0;
    g_w = 0;
    g_h = 0;
    gBS->LocateProtocol(&k_gop_guid, 0, (void **)&g_gop);
    if (!g_gop && gST && gST->ConsoleOutHandle)
        gBS->HandleProtocol(gST->ConsoleOutHandle, &k_gop_guid, (void **)&g_gop);
    if (g_gop) pick_gop_mode();
}

static rb_platform g_plat;

static int try_open_volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs)
{
    EFI_FILE_PROTOCOL *root = 0;
    void *blob = 0;
    size_t blob_len = 0;
    if (!fs || fs->OpenVolume(fs, &root) != EFI_SUCCESS || !root)
        return 0;
    g_root = root;
    if (efi_read("config/roboboot.manifest", &blob, &blob_len) == RB_OK) {
        free(blob);
        return 1;
    }
    if (efi_read("qemu.flags", &blob, &blob_len) == RB_OK) {
        free(blob);
        return 1;
    }
    root->Close(root);
    g_root = 0;
    return 0;
}

static int open_esp(EFI_HANDLE image)
{
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    UINTN count = 0;
    EFI_HANDLE *handles = 0;
    UINTN i;

    if (gBS->HandleProtocol(image, &k_li_guid, (void **)&li) == EFI_SUCCESS && li && li->DeviceHandle) {
        if (gBS->ConnectController)
            gBS->ConnectController(li->DeviceHandle, 0, 0, 1);
        if (gBS->HandleProtocol(li->DeviceHandle, &k_fs_guid, (void **)&fs) == EFI_SUCCESS && fs) {
            if (try_open_volume(fs))
                return 1;
        }
        if (gBS->OpenProtocol(li->DeviceHandle, &k_fs_guid, (void **)&fs, image, 0,
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL) == EFI_SUCCESS && fs) {
            if (try_open_volume(fs))
                return 1;
        }
    }

    if (!gBS->LocateHandleBuffer)
        return 0;
    if (gBS->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, &k_fs_guid, 0, &count, &handles) != EFI_SUCCESS)
        return 0;
    for (i = 0; i < count; i++) {
        fs = 0;
        if (gBS->HandleProtocol(handles[i], &k_fs_guid, (void **)&fs) != EFI_SUCCESS || !fs)
            continue;
        if (try_open_volume(fs)) {
            gBS->FreePool(handles);
            return 1;
        }
    }
    if (handles)
        gBS->FreePool(handles);
    return 0;
}

int efi_platform_init(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    void *blob = 0;
    size_t blob_len = 0;

    efi_init_lib(image, st);
    if (!gBS) return 0;
    g_stall_ms = 0;
    efi_log("RoboBoot EFI start");

    g_plat.now_ms = efi_now_ms;
    g_plat.sleep_ms = efi_sleep_ms;
    g_plat.poll_input = efi_poll;
    g_plat.present = efi_present;
    g_plat.read_file = efi_read;
    g_plat.write_file = efi_write;
    g_plat.list_dir = efi_list;
    g_plat.log = efi_log_plat;
    g_plat.boot = efi_boot;
    g_plat.firmware_setup = efi_fw;
    g_plat.reboot = efi_reboot;
    g_plat.halt = efi_halt;
    g_plat.root = "";
    rb_plat = &g_plat;

    if (!open_esp(image))
        efi_log("RoboBoot ESP not found");

    if (efi_read("qemu.flags", &blob, &blob_len) == RB_OK) {
        char *text = (char *)malloc(blob_len + 1);
        if (text) {
            memcpy(text, blob, blob_len);
            text[blob_len] = 0;
            parse_flags(text);
            free(text);
        }
        free(blob);
    }

    efi_install_boot_entry();

    if (!g_selftest) {
        efi_log("RoboBoot taking graphics");
        efi_hide_firmware_cursor();
        efi_take_graphics();
        efi_log("RoboBoot graphics ready");
        gBS->HandleProtocol(st->ConsoleInHandle, &k_inex_guid, (void **)&g_inex);
        if (!g_inex)
            gBS->LocateProtocol(&k_inex_guid, 0, (void **)&g_inex);
    }

    rb_logf("RoboBoot EFI GOP=%dx%d fs=%s selftest=%d menu=%d",
            g_w, g_h, g_root ? "ok" : "missing", g_selftest, g_menu);
    return g_root != 0;
}

const rb_platform *efi_platform(void)
{
    return &g_plat;
}

void efi_fb_size(int *w, int *h)
{
    *w = g_w > 0 ? g_w : 1024;
    *h = g_h > 0 ? g_h : 768;
}

void efi_graphics_info(uint64_t *fb, uint32_t *w, uint32_t *h, uint32_t *pitch, uint32_t *bpp, uint32_t *pixfmt)
{
    uint64_t addr = 0;
    uint32_t pw = (uint32_t)(g_w > 0 ? g_w : 0);
    uint32_t ph = (uint32_t)(g_h > 0 ? g_h : 0);
    uint32_t ppitch = g_stride ? g_stride * 4u : pw * 4u;
    if (g_gop && g_gop->Mode && g_gop->Mode->FrameBufferBase)
        addr = (uint64_t)g_gop->Mode->FrameBufferBase;
    else if (g_fb)
        addr = (uint64_t)(uintptr_t)g_fb;
    if (fb) *fb = addr;
    if (w) *w = pw;
    if (h) *h = ph;
    if (pitch) *pitch = ppitch;
    if (bpp) *bpp = 32;
    if (pixfmt) *pixfmt = (g_pixfmt == PixelRedGreenBlueReserved8BitPerColor) ? 1u : 0u;
}

void efi_shutdown(void)
{
    if (gRT) gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, 0);
    for (;;) { }
}
