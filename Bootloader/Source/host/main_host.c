#include "roboboot.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    HWND hwnd;
    HDC hdc;
    BITMAPINFO bmi;
    int w, h;
    int running;
    int fullscreen;
    Input pending;
    char root[RbMaxPath];
    int lastBootCode;
    char lastBootId[RbMaxId];
} hostState;

static hostState gHost;

static void ToWinPath(char *dst, size_t cap, const char *src)
{
    size_t i;
    Strcpy(dst, cap, src ? src : "");
    for (i = 0; dst[i]; i++)
        if (dst[i] == '/') dst[i] = '\\';
}

static uint64_t HostNowMs(void)
{
    static LARGE_INTEGER freq;
    static LARGE_INTEGER origin;
    LARGE_INTEGER c;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&origin);
    }
    QueryPerformanceCounter(&c);
    if (c.QuadPart < origin.QuadPart) return 0;
    return (uint64_t)(((c.QuadPart - origin.QuadPart) * 1000ull) / (uint64_t)freq.QuadPart);
}

static void HostSleepMs(uint32_t ms)
{
    Sleep(ms);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            gHost.running = 0;
            gHost.pending.quit = 1;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static int MapVk(WPARAM vk)
{
    switch (vk) {
    case VK_LEFT: return RbKeyLeft;
    case VK_RIGHT: return RbKeyRight;
    case VK_UP: return RbKeyUp;
    case VK_DOWN: return RbKeyDown;
    case VK_RETURN: return RbKeyEnter;
    case VK_ESCAPE: return RbKeyEsc;
    case VK_BACK: return RbKeyBackspace;
    case VK_TAB: return RbKeyTab;
    case VK_F10: return RbKeyF10;
    case VK_OEM_3: return RbKeyConsole; /* ` ~ */
    default: return RbKeyNone;
    }
}

static void PushKey(int key)
{
    if (key == RbKeyNone) return;
    if (gHost.pending.nkeys < 24)
        gHost.pending.keys[gHost.pending.nkeys++] = key;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CLOSE:
        gHost.running = 0;
        gHost.pending.quit = 1;
        DestroyWindow(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CHAR:
        if (wparam >= 32 && wparam < 127 && gHost.pending.nchars < 8)
            gHost.pending.chars[gHost.pending.nchars++] = (int)wparam;
        return 0;
    case WM_SYSCHAR:
        /* Alt+letter must not activate a window mnemonic. */
        if ((wparam == 'm' || wparam == 'M') && gHost.pending.nchars < 8)
            gHost.pending.chars[gHost.pending.nchars++] = 'm';
        return 0;
    case WM_SYSKEYDOWN:
        if (wparam == 'M' || wparam == 'm') {
            if (gHost.pending.nchars < 8)
                gHost.pending.chars[gHost.pending.nchars++] = 'm';
            return 0;
        }
        PushKey(MapVk(wparam));
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_OEM_3) PushKey(RbKeyConsole);
        else if ((wparam == 'M' || wparam == 'm') && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
            if (gHost.pending.nchars < 8)
                gHost.pending.chars[gHost.pending.nchars++] = 'm';
        } else {
            PushKey(MapVk(wparam));
        }
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
}

static void HostPoll(Input *out)
{
    pump();
    *out = gHost.pending;
    Zero(&gHost.pending, sizeof(gHost.pending));
    if (!gHost.running) out->quit = 1;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) out->mods |= RbModAlt;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) out->mods |= RbModShift;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) out->mods |= RbModCtrl;
    if ((out->mods & RbModAlt) && (GetAsyncKeyState('M') & 0x8000)) {
        int i, have = 0;
        for (i = 0; i < out->nchars; i++)
            if (out->chars[i] == 'm' || out->chars[i] == 'M') have = 1;
        if (!have && out->nchars < 8)
            out->chars[out->nchars++] = 'm';
    }
}

static int HostPresent(Surface *s)
{
    if (!gHost.hwnd || !s || !s->px) return 0;
    pump();
    StretchDIBits(gHost.hdc, 0, 0, gHost.w, gHost.h,
                  0, 0, s->w, s->h, s->px, &gHost.bmi, DIB_RGB_COLORS, SRCCOPY);
    return 1;
}

static RoboRbStatus HostRead(const char *path, void **data, size_t *len)
{
    char win[MAX_PATH];
    HANDLE h;
    DWORD size, got;
    void *buf;
    ToWinPath(win, sizeof(win), path);
    h = CreateFileA(win, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return RbErrIo;
    size = GetFileSize(h, NULL);
    buf = Alloc(size ? size : 1);
    if (!buf) {
        CloseHandle(h);
        return RbErrNomem;
    }
    if (!ReadFile(h, buf, size, &got, NULL)) {
        RoboRbFree(buf);
        CloseHandle(h);
        return RbErrIo;
    }
    CloseHandle(h);
    *data = buf;
    *len = got;
    return RbOk;
}

static RoboRbStatus RoboHostWrite(const char *path, const void *data, size_t len)
{
    char win[MAX_PATH];
    HANDLE h;
    DWORD got;
    ToWinPath(win, sizeof(win), path);
    h = CreateFileA(win, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return RbErrIo;
    if (!WriteFile(h, data, (DWORD)len, &got, NULL)) {
        CloseHandle(h);
        return RbErrIo;
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    return got == (DWORD)len ? RbOk : RbErrIo;
}

static RoboRbStatus HostList(const char *path, void (*cb)(const char *name, int isDir, void *user), void *user)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    ToWinPath(pattern, sizeof(pattern), path);
    {
        size_t n = strlen(pattern);
        if (n && pattern[n - 1] != '\\') Strcpy(pattern + n, sizeof(pattern) - n, "\\*");
        else Strcpy(pattern + n, sizeof(pattern) - n, "*");
    }
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return RbOk;
    do {
        if (cb) cb(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, user);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return RbOk;
}

static void HostLog(const char *msg)
{
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", msg);
}

static RoboRbStatus HostBoot(const Target *t, const Handoff *h)
{
    Logf("RoboBoot handoff: id=%s kind=%s kernel=%zu ramdisk=%zu entry=0x%llx args=%s",
            t->id, KindName(t->kind),
            h && h->kernel.valid ? h->kernel.size : 0,
            h && h->ramdisk.valid ? h->ramdisk.size : 0,
            (unsigned long long)(h ? h->kernel.entry : 0),
            t->args);
    Strcpy(gHost.lastBootId, sizeof(gHost.lastBootId), t->id);
    gHost.lastBootCode = 0;
    return RbOk;
}

static RoboRbStatus HostFw(void)
{
    Logf("firmware setup requested (host simulation)");
    gHost.running = 0;
    return RbOk;
}

static RoboRbStatus HostReboot(void)
{
    Logf("reboot requested (host simulation)");
    gHost.running = 0;
    return RbOk;
}

static RoboRbStatus HostHalt(void)
{
    Logf("halt requested (host simulation)");
    gHost.running = 0;
    return RbOk;
}

static const Platform gPlat = {
    HostNowMs,
    HostSleepMs,
    HostPoll,
    HostPresent,
    HostRead,
    RoboHostWrite,
    HostList,
    HostLog,
    HostBoot,
    HostFw,
    HostReboot,
    HostHalt,
    NULL
};

static int MakeWindow(int w, int h, int fullscreen)
{
    WNDCLASSA wc;
    DWORD style;
    RECT r;
    int sx, sy;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "RoboBootFrame";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    style = WS_POPUP;
    sx = GetSystemMetrics(SM_CXSCREEN);
    sy = GetSystemMetrics(SM_CYSCREEN);
    if (fullscreen) {
        w = sx;
        h = sy;
        gHost.hwnd = CreateWindowExA(WS_EX_TOPMOST, wc.lpszClassName, "RoboBoot",
                                      style, 0, 0, w, h, NULL, NULL, wc.hInstance, NULL);
    } else {
        r.left = 0; r.top = 0; r.right = w; r.bottom = h;
        style = WS_POPUP | WS_VISIBLE;
        gHost.hwnd = CreateWindowExA(0, wc.lpszClassName, "RoboBoot",
                                      style, (sx - w) / 2, (sy - h) / 2, w, h,
                                      NULL, NULL, wc.hInstance, NULL);
    }
    if (!gHost.hwnd) return 0;
    gHost.hdc = GetDC(gHost.hwnd);
    gHost.w = w;
    gHost.h = h;
    gHost.fullscreen = fullscreen;
    memset(&gHost.bmi, 0, sizeof(gHost.bmi));
    gHost.bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    gHost.bmi.bmiHeader.biWidth = w;
    gHost.bmi.bmiHeader.biHeight = -h;
    gHost.bmi.bmiHeader.biPlanes = 1;
    gHost.bmi.bmiHeader.biBitCount = 32;
    gHost.bmi.bmiHeader.biCompression = BI_RGB;
    ShowWindow(gHost.hwnd, SW_SHOW);
    {
        RECT rc;
        GetClientRect(gHost.hwnd, &rc);
        FillRect(gHost.hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    UpdateWindow(gHost.hwnd);
    return 1;
}

static RoboRbStatus WriteBmp(const char *path, Surface *s)
{
    FILE *f;
    int w, h, y, x;
    uint32_t rowPad;
    uint32_t off, size;
    unsigned char hdr[54];
    if (!s || !s->px) return RbErr;
    w = s->w;
    h = s->h;
    rowPad = (uint32_t)((w * 3 + 3) & ~3);
    off = 54;
    size = off + rowPad * (uint32_t)h;
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (unsigned char)size; hdr[3] = (unsigned char)(size >> 8);
    hdr[4] = (unsigned char)(size >> 16); hdr[5] = (unsigned char)(size >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (unsigned char)w; hdr[19] = (unsigned char)(w >> 8);
    hdr[20] = (unsigned char)(w >> 16); hdr[21] = (unsigned char)(w >> 24);
    hdr[22] = (unsigned char)h; hdr[23] = (unsigned char)(h >> 8);
    hdr[24] = (unsigned char)(h >> 16); hdr[25] = (unsigned char)(h >> 24);
    hdr[26] = 1;
    hdr[28] = 24;
    f = fopen(path, "wb");
    if (!f) return RbErrIo;
    fwrite(hdr, 1, 54, f);
    for (y = h - 1; y >= 0; y--) {
        unsigned char row[4096 * 3];
        int n = 0;
        for (x = 0; x < w; x++) {
            uint32_t p = s->px[y * w + x];
            row[n++] = (unsigned char)(p & 255);
            row[n++] = (unsigned char)((p >> 8) & 255);
            row[n++] = (unsigned char)((p >> 16) & 255);
        }
        while (n % 4) row[n++] = 0;
        fwrite(row, 1, (size_t)n, f);
    }
    fclose(f);
    return RbOk;
}

static int DumpUi(Session *ss, const char *dir)
{
    char path[MAX_PATH];
    if (SessionLoad(ss) != RbOk) return 1;
    ss->fade = 1.f;
    ss->selAnim = 1.f;
    SplashDraw(ss, 0.42f, 1.f);
    snprintf(path, sizeof(path), "%s\\splash.bmp", dir);
    WriteBmp(path, &ss->fb);
    SplashDraw(ss, 1.f, 1.f);
    snprintf(path, sizeof(path), "%s\\splash-done.bmp", dir);
    WriteBmp(path, &ss->fb);
    ss->selected = 0;
    ss->phase = RbPhasePicker;
    PickerDraw(ss);
    snprintf(path, sizeof(path), "%s\\picker.bmp", dir);
    WriteBmp(path, &ss->fb);
    ss->phase = RbPhaseAuth;
    Strcpy(ss->authBuf, sizeof(ss->authBuf), "secret");
    ss->authLen = 6;
    AuthDraw(ss);
    snprintf(path, sizeof(path), "%s\\auth.bmp", dir);
    WriteBmp(path, &ss->fb);
    ss->phase = RbPhaseConsole;
    ss->consoleLines = 0;
    ss->consoleIn[0] = 0;
    Strcpy(ss->consoleIn, sizeof(ss->consoleIn), "help");
    ConsoleDraw(ss);
    snprintf(path, sizeof(path), "%s\\console.bmp", dir);
    WriteBmp(path, &ss->fb);
    fprintf(stderr, "wrote UI dumps to %s\n", dir);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
            "RoboBoot — RoboticelaOS bootloader (host)\n"
            "  --root DIR       bootloader data root\n"
            "  --menu           show the startup picker\n"
            "  --hold-alt       simulate Alt+M through splash\n"
            "  --fullscreen     cover the display\n"
            "  --width N        window width (default 1280)\n"
            "  --height N       window height (default 800)\n"
            "  --dump DIR       write splash/picker BMP frames and exit\n");
}

int main(int argc, char **argv)
{
    Session ss;
    Platform plat;
    int i, w = 1280, h = 800, fullscreen = 0, holdAlt = 0, forceMenu = 0;
    const char *root = NULL;
    const char *dumpDir = NULL;

    SetProcessDPIAware();
    for (i = 1; i < argc; i++) {
        if (StrEq(argv[i], "--help") || StrEq(argv[i], "-h")) {
            usage();
            return 0;
        } else if (StrEq(argv[i], "--root") && i + 1 < argc) {
            root = argv[++i];
        } else if (StrEq(argv[i], "--menu")) {
            forceMenu = 1;
        } else if (StrEq(argv[i], "--hold-alt")) {
            holdAlt = 1;
            forceMenu = 1;
        } else if (StrEq(argv[i], "--fullscreen")) {
            fullscreen = 1;
        } else if (StrEq(argv[i], "--width") && i + 1 < argc) {
            w = atoi(argv[++i]);
        } else if (StrEq(argv[i], "--height") && i + 1 < argc) {
            h = atoi(argv[++i]);
        } else if (StrEq(argv[i], "--dump") && i + 1 < argc) {
            dumpDir = argv[++i];
        }
    }

    if (!root) {
        static char exe[MAX_PATH];
        char *slash;
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        slash = strrchr(exe, '\\');
        if (slash) {
            *slash = 0;
            slash = strrchr(exe, '\\');
            if (slash) *slash = 0;
        }
        root = exe;
    }

    Strcpy(gHost.root, sizeof(gHost.root), root);
    gHost.running = 1;
    plat = gPlat;
    plat.root = gHost.root;

    if (dumpDir) {
        SessionInit(&ss, &plat);
        ss.fb.w = w;
        ss.fb.h = h;
        ss.fb.px = (uint32_t *)RoboRbCalloc((size_t)ss.fb.w * (size_t)ss.fb.h * 4u);
        if (!ss.fb.px) return 1;
        i = DumpUi(&ss, dumpDir);
        SessionFree(&ss);
        return i;
    }

    if (!MakeWindow(w, h, fullscreen)) {
        fprintf(stderr, "failed to open RoboBoot window\n");
        return 1;
    }

    SessionInit(&ss, &plat);
    ss.forceMenu = forceMenu;
    ss.altLatched = holdAlt;
    ss.fb.w = gHost.w;
    ss.fb.h = gHost.h;
    ss.fb.px = (uint32_t *)RoboRbCalloc((size_t)ss.fb.w * (size_t)ss.fb.h * 4u);
    if (!ss.fb.px) return 1;

    SessionBoot(&ss);
    Logf("session complete, last target=%s", gHost.lastBootId[0] ? gHost.lastBootId : "(none)");
    SessionFree(&ss);
    if (gHost.hdc && gHost.hwnd) ReleaseDC(gHost.hwnd, gHost.hdc);
    if (gHost.hwnd) DestroyWindow(gHost.hwnd);
    return gHost.lastBootCode;
}
