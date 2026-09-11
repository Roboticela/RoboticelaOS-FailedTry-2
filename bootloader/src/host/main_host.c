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
    rb_input pending;
    char root[RB_MAX_PATH];
    int last_boot_code;
    char last_boot_id[RB_MAX_ID];
} host_state;

static host_state g_host;

static void to_win_path(char *dst, size_t cap, const char *src)
{
    size_t i;
    rb_strcpy(dst, cap, src ? src : "");
    for (i = 0; dst[i]; i++)
        if (dst[i] == '/') dst[i] = '\\';
}

static uint64_t host_now_ms(void)
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

static void host_sleep_ms(uint32_t ms)
{
    Sleep(ms);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_host.running = 0;
            g_host.pending.quit = 1;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static int map_vk(WPARAM vk)
{
    switch (vk) {
    case VK_LEFT: return RB_KEY_LEFT;
    case VK_RIGHT: return RB_KEY_RIGHT;
    case VK_UP: return RB_KEY_UP;
    case VK_DOWN: return RB_KEY_DOWN;
    case VK_RETURN: return RB_KEY_ENTER;
    case VK_ESCAPE: return RB_KEY_ESC;
    case VK_BACK: return RB_KEY_BACKSPACE;
    case VK_TAB: return RB_KEY_TAB;
    case VK_F10: return RB_KEY_F10;
    case VK_OEM_3: return RB_KEY_CONSOLE; /* ` ~ */
    default: return RB_KEY_NONE;
    }
}

static void push_key(int key)
{
    if (key == RB_KEY_NONE) return;
    if (g_host.pending.nkeys < 24)
        g_host.pending.keys[g_host.pending.nkeys++] = key;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CLOSE:
        g_host.running = 0;
        g_host.pending.quit = 1;
        DestroyWindow(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CHAR:
        if (wparam >= 32 && wparam < 127 && g_host.pending.nchars < 8)
            g_host.pending.chars[g_host.pending.nchars++] = (int)wparam;
        return 0;
    case WM_SYSCHAR:
        /* Alt+letter must not activate a window mnemonic. */
        if ((wparam == 'm' || wparam == 'M') && g_host.pending.nchars < 8)
            g_host.pending.chars[g_host.pending.nchars++] = 'm';
        return 0;
    case WM_SYSKEYDOWN:
        if (wparam == 'M' || wparam == 'm') {
            if (g_host.pending.nchars < 8)
                g_host.pending.chars[g_host.pending.nchars++] = 'm';
            return 0;
        }
        push_key(map_vk(wparam));
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_OEM_3) push_key(RB_KEY_CONSOLE);
        else if ((wparam == 'M' || wparam == 'm') && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
            if (g_host.pending.nchars < 8)
                g_host.pending.chars[g_host.pending.nchars++] = 'm';
        } else {
            push_key(map_vk(wparam));
        }
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
}

static void host_poll(rb_input *out)
{
    pump();
    *out = g_host.pending;
    rb_zero(&g_host.pending, sizeof(g_host.pending));
    if (!g_host.running) out->quit = 1;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) out->mods |= RB_MOD_ALT;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) out->mods |= RB_MOD_SHIFT;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) out->mods |= RB_MOD_CTRL;
    if ((out->mods & RB_MOD_ALT) && (GetAsyncKeyState('M') & 0x8000)) {
        int i, have = 0;
        for (i = 0; i < out->nchars; i++)
            if (out->chars[i] == 'm' || out->chars[i] == 'M') have = 1;
        if (!have && out->nchars < 8)
            out->chars[out->nchars++] = 'm';
    }
}

static int host_present(rb_surface *s)
{
    if (!g_host.hwnd || !s || !s->px) return 0;
    pump();
    StretchDIBits(g_host.hdc, 0, 0, g_host.w, g_host.h,
                  0, 0, s->w, s->h, s->px, &g_host.bmi, DIB_RGB_COLORS, SRCCOPY);
    return 1;
}

static rb_status host_read(const char *path, void **data, size_t *len)
{
    char win[MAX_PATH];
    HANDLE h;
    DWORD size, got;
    void *buf;
    to_win_path(win, sizeof(win), path);
    h = CreateFileA(win, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return RB_ERR_IO;
    size = GetFileSize(h, NULL);
    buf = rb_alloc(size ? size : 1);
    if (!buf) {
        CloseHandle(h);
        return RB_ERR_NOMEM;
    }
    if (!ReadFile(h, buf, size, &got, NULL)) {
        rb_free(buf);
        CloseHandle(h);
        return RB_ERR_IO;
    }
    CloseHandle(h);
    *data = buf;
    *len = got;
    return RB_OK;
}

static rb_status host_write(const char *path, const void *data, size_t len)
{
    char win[MAX_PATH];
    HANDLE h;
    DWORD got;
    to_win_path(win, sizeof(win), path);
    h = CreateFileA(win, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return RB_ERR_IO;
    if (!WriteFile(h, data, (DWORD)len, &got, NULL)) {
        CloseHandle(h);
        return RB_ERR_IO;
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    return got == (DWORD)len ? RB_OK : RB_ERR_IO;
}

static rb_status host_list(const char *path, void (*cb)(const char *name, int is_dir, void *user), void *user)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    to_win_path(pattern, sizeof(pattern), path);
    {
        size_t n = strlen(pattern);
        if (n && pattern[n - 1] != '\\') rb_strcpy(pattern + n, sizeof(pattern) - n, "\\*");
        else rb_strcpy(pattern + n, sizeof(pattern) - n, "*");
    }
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return RB_OK;
    do {
        if (cb) cb(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, user);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return RB_OK;
}

static void host_log(const char *msg)
{
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", msg);
}

static rb_status host_boot(const rb_target *t, const rb_handoff *h)
{
    rb_logf("RoboBoot handoff: id=%s kind=%s kernel=%zu ramdisk=%zu entry=0x%llx args=%s",
            t->id, rb_kind_name(t->kind),
            h && h->kernel.valid ? h->kernel.size : 0,
            h && h->ramdisk.valid ? h->ramdisk.size : 0,
            (unsigned long long)(h ? h->kernel.entry : 0),
            t->args);
    rb_strcpy(g_host.last_boot_id, sizeof(g_host.last_boot_id), t->id);
    g_host.last_boot_code = 0;
    return RB_OK;
}

static rb_status host_fw(void)
{
    rb_logf("firmware setup requested (host simulation)");
    g_host.running = 0;
    return RB_OK;
}

static rb_status host_reboot(void)
{
    rb_logf("reboot requested (host simulation)");
    g_host.running = 0;
    return RB_OK;
}

static rb_status host_halt(void)
{
    rb_logf("halt requested (host simulation)");
    g_host.running = 0;
    return RB_OK;
}

static const rb_platform g_plat = {
    host_now_ms,
    host_sleep_ms,
    host_poll,
    host_present,
    host_read,
    host_write,
    host_list,
    host_log,
    host_boot,
    host_fw,
    host_reboot,
    host_halt,
    NULL
};

static int make_window(int w, int h, int fullscreen)
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
        g_host.hwnd = CreateWindowExA(WS_EX_TOPMOST, wc.lpszClassName, "RoboBoot",
                                      style, 0, 0, w, h, NULL, NULL, wc.hInstance, NULL);
    } else {
        r.left = 0; r.top = 0; r.right = w; r.bottom = h;
        style = WS_POPUP | WS_VISIBLE;
        g_host.hwnd = CreateWindowExA(0, wc.lpszClassName, "RoboBoot",
                                      style, (sx - w) / 2, (sy - h) / 2, w, h,
                                      NULL, NULL, wc.hInstance, NULL);
    }
    if (!g_host.hwnd) return 0;
    g_host.hdc = GetDC(g_host.hwnd);
    g_host.w = w;
    g_host.h = h;
    g_host.fullscreen = fullscreen;
    memset(&g_host.bmi, 0, sizeof(g_host.bmi));
    g_host.bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_host.bmi.bmiHeader.biWidth = w;
    g_host.bmi.bmiHeader.biHeight = -h;
    g_host.bmi.bmiHeader.biPlanes = 1;
    g_host.bmi.bmiHeader.biBitCount = 32;
    g_host.bmi.bmiHeader.biCompression = BI_RGB;
    ShowWindow(g_host.hwnd, SW_SHOW);
    {
        RECT rc;
        GetClientRect(g_host.hwnd, &rc);
        FillRect(g_host.hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    UpdateWindow(g_host.hwnd);
    return 1;
}

static rb_status write_bmp(const char *path, rb_surface *s)
{
    FILE *f;
    int w, h, y, x;
    uint32_t row_pad;
    uint32_t off, size;
    unsigned char hdr[54];
    if (!s || !s->px) return RB_ERR;
    w = s->w;
    h = s->h;
    row_pad = (uint32_t)((w * 3 + 3) & ~3);
    off = 54;
    size = off + row_pad * (uint32_t)h;
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
    if (!f) return RB_ERR_IO;
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
    return RB_OK;
}

static int dump_ui(rb_session *ss, const char *dir)
{
    char path[MAX_PATH];
    if (rb_session_load(ss) != RB_OK) return 1;
    ss->fade = 1.f;
    ss->sel_anim = 1.f;
    rb_splash_draw(ss, 0.42f, 1.f);
    snprintf(path, sizeof(path), "%s\\splash.bmp", dir);
    write_bmp(path, &ss->fb);
    rb_splash_draw(ss, 1.f, 1.f);
    snprintf(path, sizeof(path), "%s\\splash-done.bmp", dir);
    write_bmp(path, &ss->fb);
    ss->selected = 0;
    ss->phase = RB_PHASE_PICKER;
    rb_picker_draw(ss);
    snprintf(path, sizeof(path), "%s\\picker.bmp", dir);
    write_bmp(path, &ss->fb);
    ss->phase = RB_PHASE_AUTH;
    rb_strcpy(ss->auth_buf, sizeof(ss->auth_buf), "secret");
    ss->auth_len = 6;
    rb_auth_draw(ss);
    snprintf(path, sizeof(path), "%s\\auth.bmp", dir);
    write_bmp(path, &ss->fb);
    ss->phase = RB_PHASE_CONSOLE;
    ss->console_lines = 0;
    ss->console_in[0] = 0;
    rb_strcpy(ss->console_in, sizeof(ss->console_in), "help");
    rb_console_draw(ss);
    snprintf(path, sizeof(path), "%s\\console.bmp", dir);
    write_bmp(path, &ss->fb);
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
    rb_session ss;
    rb_platform plat;
    int i, w = 1280, h = 800, fullscreen = 0, hold_alt = 0, force_menu = 0;
    const char *root = NULL;
    const char *dump_dir = NULL;

    SetProcessDPIAware();
    for (i = 1; i < argc; i++) {
        if (rb_str_eq(argv[i], "--help") || rb_str_eq(argv[i], "-h")) {
            usage();
            return 0;
        } else if (rb_str_eq(argv[i], "--root") && i + 1 < argc) {
            root = argv[++i];
        } else if (rb_str_eq(argv[i], "--menu")) {
            force_menu = 1;
        } else if (rb_str_eq(argv[i], "--hold-alt")) {
            hold_alt = 1;
            force_menu = 1;
        } else if (rb_str_eq(argv[i], "--fullscreen")) {
            fullscreen = 1;
        } else if (rb_str_eq(argv[i], "--width") && i + 1 < argc) {
            w = atoi(argv[++i]);
        } else if (rb_str_eq(argv[i], "--height") && i + 1 < argc) {
            h = atoi(argv[++i]);
        } else if (rb_str_eq(argv[i], "--dump") && i + 1 < argc) {
            dump_dir = argv[++i];
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

    rb_strcpy(g_host.root, sizeof(g_host.root), root);
    g_host.running = 1;
    plat = g_plat;
    plat.root = g_host.root;

    if (dump_dir) {
        rb_session_init(&ss, &plat);
        ss.fb.w = w;
        ss.fb.h = h;
        ss.fb.px = (uint32_t *)rb_calloc((size_t)ss.fb.w * (size_t)ss.fb.h * 4u);
        if (!ss.fb.px) return 1;
        i = dump_ui(&ss, dump_dir);
        rb_session_free(&ss);
        return i;
    }

    if (!make_window(w, h, fullscreen)) {
        fprintf(stderr, "failed to open RoboBoot window\n");
        return 1;
    }

    rb_session_init(&ss, &plat);
    ss.force_menu = force_menu;
    ss.alt_latched = hold_alt;
    ss.fb.w = g_host.w;
    ss.fb.h = g_host.h;
    ss.fb.px = (uint32_t *)rb_calloc((size_t)ss.fb.w * (size_t)ss.fb.h * 4u);
    if (!ss.fb.px) return 1;

    rb_session_boot(&ss);
    rb_logf("session complete, last target=%s", g_host.last_boot_id[0] ? g_host.last_boot_id : "(none)");
    rb_session_free(&ss);
    if (g_host.hdc && g_host.hwnd) ReleaseDC(g_host.hwnd, g_host.hdc);
    if (g_host.hwnd) DestroyWindow(g_host.hwnd);
    return g_host.last_boot_code;
}
