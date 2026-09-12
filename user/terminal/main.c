#include <robosys/syscall.h>
#include <robosys/display.h>
#include <robosys/input.h>
#include <robosys/syscall_numbers.h>

/*
 * Roboticela terminal — fullscreen framebuffer console + keyboard.
 * Text goes to the display (not only serial).
 */

#define FONT_W 8
#define FONT_H 8
#define COL_BG 0xFF1D1D1Fu
#define COL_FG 0xFFFFFFFFu
#define COL_DIM 0xFFA1A1A6u
#define MAX_COLS 120
#define MAX_ROWS 40
#define LINE_MAX 256

/* Minimal 8x8 glyphs for ASCII 32..126 (bit0 = left). */
static const uint8_t g_font[95][8] = {
    {0,0,0,0,0,0,0,0}, /* space */
    {0x10,0x10,0x10,0x10,0x10,0,0x10,0},
    {0x28,0x28,0x28,0,0,0,0,0},
    {0x28,0x28,0x7C,0x28,0x7C,0x28,0x28,0},
    {0x10,0x3C,0x50,0x38,0x14,0x78,0x10,0},
    {0x60,0x64,0x08,0x10,0x20,0x4C,0x0C,0},
    {0x20,0x50,0x50,0x20,0x54,0x48,0x34,0},
    {0x10,0x10,0x20,0,0,0,0,0},
    {0x08,0x10,0x20,0x20,0x20,0x10,0x08,0},
    {0x20,0x10,0x08,0x08,0x08,0x10,0x20,0},
    {0,0x10,0x54,0x38,0x54,0x10,0,0},
    {0,0x10,0x10,0x7C,0x10,0x10,0,0},
    {0,0,0,0,0,0x10,0x10,0x20},
    {0,0,0,0x7C,0,0,0,0},
    {0,0,0,0,0,0,0x10,0},
    {0x04,0x08,0x10,0x20,0x40,0,0,0},
    {0x38,0x44,0x4C,0x54,0x64,0x44,0x38,0}, /* 0 */
    {0x10,0x30,0x10,0x10,0x10,0x10,0x38,0},
    {0x38,0x44,0x04,0x18,0x20,0x40,0x7C,0},
    {0x38,0x44,0x04,0x18,0x04,0x44,0x38,0},
    {0x08,0x18,0x28,0x48,0x7C,0x08,0x08,0},
    {0x7C,0x40,0x78,0x04,0x04,0x44,0x38,0},
    {0x18,0x20,0x40,0x78,0x44,0x44,0x38,0},
    {0x7C,0x04,0x08,0x10,0x20,0x20,0x20,0},
    {0x38,0x44,0x44,0x38,0x44,0x44,0x38,0},
    {0x38,0x44,0x44,0x3C,0x04,0x08,0x30,0},
    {0,0x10,0,0,0x10,0,0,0},
    {0,0x10,0,0,0x10,0x10,0x20,0},
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08,0},
    {0,0,0x7C,0,0x7C,0,0,0},
    {0x40,0x20,0x10,0x08,0x10,0x20,0x40,0},
    {0x38,0x44,0x04,0x08,0x10,0,0x10,0},
    {0x38,0x44,0x5C,0x54,0x5C,0x40,0x38,0},
    {0x38,0x44,0x44,0x7C,0x44,0x44,0x44,0}, /* A */
    {0x78,0x44,0x44,0x78,0x44,0x44,0x78,0},
    {0x38,0x44,0x40,0x40,0x40,0x44,0x38,0},
    {0x78,0x44,0x44,0x44,0x44,0x44,0x78,0},
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x7C,0},
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0},
    {0x38,0x44,0x40,0x4C,0x44,0x44,0x38,0},
    {0x44,0x44,0x44,0x7C,0x44,0x44,0x44,0},
    {0x38,0x10,0x10,0x10,0x10,0x10,0x38,0},
    {0x04,0x04,0x04,0x04,0x04,0x44,0x38,0},
    {0x44,0x48,0x50,0x60,0x50,0x48,0x44,0},
    {0x40,0x40,0x40,0x40,0x40,0x40,0x7C,0},
    {0x44,0x6C,0x54,0x54,0x44,0x44,0x44,0},
    {0x44,0x64,0x54,0x4C,0x44,0x44,0x44,0},
    {0x38,0x44,0x44,0x44,0x44,0x44,0x38,0},
    {0x78,0x44,0x44,0x78,0x40,0x40,0x40,0},
    {0x38,0x44,0x44,0x44,0x54,0x48,0x34,0},
    {0x78,0x44,0x44,0x78,0x50,0x48,0x44,0},
    {0x38,0x44,0x40,0x38,0x04,0x44,0x38,0},
    {0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0},
    {0x44,0x44,0x44,0x44,0x44,0x44,0x38,0},
    {0x44,0x44,0x44,0x44,0x44,0x28,0x10,0},
    {0x44,0x44,0x44,0x54,0x54,0x6C,0x44,0},
    {0x44,0x44,0x28,0x10,0x28,0x44,0x44,0},
    {0x44,0x44,0x28,0x10,0x10,0x10,0x10,0},
    {0x7C,0x04,0x08,0x10,0x20,0x40,0x7C,0},
    {0x38,0x20,0x20,0x20,0x20,0x20,0x38,0},
    {0x40,0x20,0x10,0x08,0x04,0,0,0},
    {0x38,0x08,0x08,0x08,0x08,0x08,0x38,0},
    {0x10,0x28,0x44,0,0,0,0,0},
    {0,0,0,0,0,0,0,0x7C},
    {0x20,0x10,0x08,0,0,0,0,0},
    {0,0,0x38,0x04,0x3C,0x44,0x3C,0}, /* a */
    {0x40,0x40,0x78,0x44,0x44,0x44,0x78,0},
    {0,0,0x38,0x44,0x40,0x44,0x38,0},
    {0x04,0x04,0x3C,0x44,0x44,0x44,0x3C,0},
    {0,0,0x38,0x44,0x7C,0x40,0x38,0},
    {0x18,0x20,0x20,0x78,0x20,0x20,0x20,0},
    {0,0,0x3C,0x44,0x3C,0x04,0x38,0},
    {0x40,0x40,0x78,0x44,0x44,0x44,0x44,0},
    {0x10,0,0x30,0x10,0x10,0x10,0x38,0},
    {0x08,0,0x18,0x08,0x08,0x48,0x30,0},
    {0x40,0x40,0x48,0x50,0x60,0x50,0x48,0},
    {0x30,0x10,0x10,0x10,0x10,0x10,0x38,0},
    {0,0,0x68,0x54,0x54,0x54,0x54,0},
    {0,0,0x78,0x44,0x44,0x44,0x44,0},
    {0,0,0x38,0x44,0x44,0x44,0x38,0},
    {0,0,0x78,0x44,0x78,0x40,0x40,0},
    {0,0,0x3C,0x44,0x3C,0x04,0x04,0},
    {0,0,0x58,0x64,0x40,0x40,0x40,0},
    {0,0,0x3C,0x40,0x38,0x04,0x78,0},
    {0x20,0x20,0x78,0x20,0x20,0x20,0x18,0},
    {0,0,0x44,0x44,0x44,0x44,0x3C,0},
    {0,0,0x44,0x44,0x44,0x28,0x10,0},
    {0,0,0x44,0x54,0x54,0x54,0x28,0},
    {0,0,0x44,0x28,0x10,0x28,0x44,0},
    {0,0,0x44,0x44,0x3C,0x04,0x38,0},
    {0,0,0x7C,0x08,0x10,0x20,0x7C,0},
    {0x08,0x10,0x10,0x20,0x10,0x10,0x08,0},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0},
    {0x20,0x10,0x10,0x08,0x10,0x10,0x20,0},
    {0,0x20,0x54,0x08,0,0,0,0},
};

static uint32_t *g_fb;
static uint32_t g_w, g_h, g_stride;
static uint32_t g_pixfmt;
static int g_fb_ok;
static char g_rows[MAX_ROWS][MAX_COLS];
static int g_cols, g_nrows;
static int g_cx, g_cy;
static char g_line[LINE_MAX];
static unsigned g_linelen;

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
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

static int starts(const char *s, const char *p)
{
    unsigned i = 0;
    while (p && p[i]) {
        if (!s || s[i] != p[i])
            return 0;
        i++;
    }
    return 1;
}

static void serial_write(const char *s, unsigned n)
{
    if (n)
        robo_write(1, s, n);
}

static void glyph_draw(int col, int row, char ch, uint32_t fg)
{
    const uint8_t *g;
    int x0, y0, y, x;
    unsigned idx;
    if (!g_fb_ok || col < 0 || row < 0 || col >= g_cols || row >= g_nrows)
        return;
    if (ch < 32 || ch > 126)
        ch = '?';
    idx = (unsigned)(ch - 32);
    g = g_font[idx];
    x0 = col * FONT_W + 8;
    y0 = row * FONT_H + 8;
    for (y = 0; y < FONT_H; y++) {
        uint8_t bits = g[y];
        for (x = 0; x < FONT_W; x++) {
            uint32_t c = (bits & (0x80u >> x)) ? fg : COL_BG;
            int px = x0 + x;
            int py = y0 + y;
            if (px >= 0 && py >= 0 && (uint32_t)px < g_w && (uint32_t)py < g_h)
                g_fb[(uint32_t)py * g_stride + (uint32_t)px] = c;
        }
    }
}

static void present(void)
{
    RoboDisplayPresent req;
    if (!g_fb_ok)
        return;
    req.pixels = (uint64_t)(uintptr_t)g_fb;
    req.width = g_w;
    req.height = g_h;
    req.stride = g_stride;
    req.pixfmt = g_pixfmt;
    req.dest_x = 0;
    req.dest_y = 0;
    robo_display((int)ROBO_DISP_PRESENT, 0, &req, sizeof(req), 0);
}

static void redraw(void)
{
    uint32_t i, n;
    int r, c;
    if (!g_fb_ok)
        return;
    n = g_stride * g_h;
    for (i = 0; i < n; i++)
        g_fb[i] = COL_BG;
    for (r = 0; r < g_nrows; r++) {
        for (c = 0; c < g_cols; c++) {
            char ch = g_rows[r][c];
            if (ch)
                glyph_draw(c, r, ch, COL_FG);
        }
    }
    glyph_draw(g_cx, g_cy, '_', COL_DIM);
    present();
}

static void scroll_up(void)
{
    int r, c;
    for (r = 0; r < g_nrows - 1; r++) {
        for (c = 0; c < g_cols; c++)
            g_rows[r][c] = g_rows[r + 1][c];
    }
    for (c = 0; c < g_cols; c++)
        g_rows[g_nrows - 1][c] = 0;
    if (g_cy > 0)
        g_cy--;
}

static void put_char(char ch)
{
    if (ch == '\n') {
        g_cx = 0;
        g_cy++;
        if (g_cy >= g_nrows) {
            scroll_up();
            g_cy = g_nrows - 1;
        }
        return;
    }
    if (ch == '\r')
        return;
    if (ch == '\b') {
        if (g_cx > 0) {
            g_cx--;
            g_rows[g_cy][g_cx] = 0;
        }
        return;
    }
    if (g_cx >= g_cols) {
        put_char('\n');
    }
    g_rows[g_cy][g_cx] = ch;
    g_cx++;
}

static void put(const char *s)
{
    unsigned i = 0;
    serial_write(s, slen(s));
    while (s && s[i]) {
        put_char(s[i]);
        i++;
    }
}

static void putch(char c)
{
    char b[2];
    b[0] = c;
    b[1] = 0;
    serial_write(b, 1);
    put_char(c);
}

static void line(const char *s)
{
    put(s);
    put("\n");
}

static char key_to_ascii(uint32_t code, uint32_t mods)
{
    int shift = (mods & ROBO_MOD_SHIFT) != 0;
    if (code >= ROBO_KEY_A && code <= ROBO_KEY_Z) {
        char c = (char)('a' + (code - ROBO_KEY_A));
        if (shift)
            c = (char)(c - 'a' + 'A');
        return c;
    }
    switch (code) {
    case ROBO_KEY_1: return shift ? '!' : '1';
    case ROBO_KEY_2: return shift ? '@' : '2';
    case ROBO_KEY_3: return shift ? '#' : '3';
    case ROBO_KEY_4: return shift ? '$' : '4';
    case ROBO_KEY_5: return shift ? '%' : '5';
    case ROBO_KEY_6: return shift ? '^' : '6';
    case ROBO_KEY_7: return shift ? '&' : '7';
    case ROBO_KEY_8: return shift ? '*' : '8';
    case ROBO_KEY_9: return shift ? '(' : '9';
    case ROBO_KEY_0: return shift ? ')' : '0';
    case ROBO_KEY_SPACE: return ' ';
    case ROBO_KEY_MINUS: return shift ? '_' : '-';
    case ROBO_KEY_EQUAL: return shift ? '+' : '=';
    case ROBO_KEY_LBRACKET: return shift ? '{' : '[';
    case ROBO_KEY_RBRACKET: return shift ? '}' : ']';
    case ROBO_KEY_BACKSLASH: return shift ? '|' : '\\';
    case ROBO_KEY_SEMICOLON: return shift ? ':' : ';';
    case ROBO_KEY_QUOTE: return shift ? '"' : '\'';
    case ROBO_KEY_GRAVE: return shift ? '~' : '`';
    case ROBO_KEY_COMMA: return shift ? '<' : ',';
    case ROBO_KEY_DOT: return shift ? '>' : '.';
    case ROBO_KEY_SLASH: return shift ? '?' : '/';
    case ROBO_KEY_TAB: return ' ';
    default: return 0;
    }
}

static int resolve_cmd(const char *cmd, char *path, unsigned path_max)
{
    static const char *prefs[] = { "/bin/", "/System/bin/", 0 };
    unsigned p;
    if (!cmd || !cmd[0] || !path || !path_max)
        return -1;
    if (cmd[0] == '/') {
        unsigned i = 0;
        while (cmd[i] && i + 1 < path_max) {
            path[i] = cmd[i];
            i++;
        }
        path[i] = 0;
        return 0;
    }
    for (p = 0; prefs[p]; p++) {
        RoboStat st;
        unsigned i = 0, j = 0;
        while (prefs[p][i] && j + 1 < path_max)
            path[j++] = prefs[p][i++];
        i = 0;
        while (cmd[i] && j + 1 < path_max)
            path[j++] = cmd[i++];
        path[j] = 0;
        if (robo_stat(path, &st) == 0 && st.size > 0)
            return 0;
    }
    return -1;
}

static int run_line(char *cmdline)
{
    char *argv[16];
    unsigned argc = 0;
    unsigned i = 0;
    char path[192];
    robo_long pid;
    int status = 0;

    while (cmdline[i] == ' ' || cmdline[i] == '\t')
        i++;
    if (!cmdline[i])
        return 0;
    if (seq(cmdline + i, "exit") || seq(cmdline + i, "quit"))
        return 1;
    if (starts(cmdline + i, "cd ")) {
        line("cd: not supported yet");
        return 0;
    }
    if (seq(cmdline + i, "help") || seq(cmdline + i, "clear")) {
        if (seq(cmdline + i, "clear")) {
            int r, c;
            for (r = 0; r < g_nrows; r++)
                for (c = 0; c < g_cols; c++)
                    g_rows[r][c] = 0;
            g_cx = g_cy = 0;
            redraw();
            return 0;
        }
        line("Roboticela terminal");
        line("  robo install|list|use|uninstall ...");
        line("  clang / clang++");
        line("  clear   exit");
        return 0;
    }
    while (cmdline[i] && argc < 15u) {
        argv[argc++] = &cmdline[i];
        while (cmdline[i] && cmdline[i] != ' ' && cmdline[i] != '\t')
            i++;
        if (cmdline[i]) {
            cmdline[i++] = 0;
            while (cmdline[i] == ' ' || cmdline[i] == '\t')
                i++;
        }
    }
    argv[argc] = 0;
    if (resolve_cmd(argv[0], path, sizeof(path)) != 0) {
        put("command not found: ");
        line(argv[0]);
        return 0;
    }
    pid = robo_process_spawn(path, argv, (int)argc);
    if ((uint64_t)pid >= (uint64_t)-4095ll || pid <= 0) {
        put("spawn failed: ");
        line(path);
        return 0;
    }
    if (robo_wait((robo_pid)pid, 0, &status) < 0)
        line("wait failed");
    return 0;
}

static int init_display(void)
{
    RoboUserDisplay info;
    robo_long r;
    uint64_t bytes;
    void *mem;

    r = robo_display((int)ROBO_DISP_CLAIM, 0, 0, 0,
                     (int)(ROBO_DISP_ROLE_OUTPUT | ROBO_DISP_ROLE_INPUT));
    if ((uint64_t)r >= (uint64_t)-4095ll) {
        serial_write("terminal: display claim failed\n", 31);
        return 0;
    }
    if ((uint64_t)robo_display((int)ROBO_DISP_INFO, 0, &info, sizeof(info), 0) >=
        (uint64_t)-4095ll)
        return 0;
    if (!info.ready || !info.width || !info.height)
        return 0;
    g_w = info.width;
    g_h = info.height;
    g_stride = info.width;
    g_pixfmt = info.pixfmt;
    if (g_w > 1920u)
        g_w = 1920u;
    if (g_h > 1080u)
        g_h = 1080u;
    bytes = (uint64_t)g_stride * (uint64_t)g_h * 4ull;
    mem = (void *)(uintptr_t)robo_mmap(0, (robo_ulong)bytes,
                                       (int)(ROBO_PROT_READ | ROBO_PROT_WRITE),
                                       (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON),
                                       -1, 0);
    if ((uint64_t)(uintptr_t)mem >= (uint64_t)-4095ll || !mem)
        return 0;
    g_fb = (uint32_t *)mem;
    g_cols = (int)((g_w - 16u) / FONT_W);
    g_nrows = (int)((g_h - 16u) / FONT_H);
    if (g_cols > MAX_COLS)
        g_cols = MAX_COLS;
    if (g_nrows > MAX_ROWS)
        g_nrows = MAX_ROWS;
    if (g_cols < 40)
        g_cols = 40;
    if (g_nrows < 12)
        g_nrows = 12;
    g_fb_ok = 1;
    return 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!init_display()) {
        serial_write("Roboticela terminal (serial only)\n", 34);
    }

    line("RoboticelaOS terminal");
    line("Type help. Package manager: robo");
    put("$ ");
    redraw();

    g_linelen = 0;
    for (;;) {
        RoboInputEvent ev;
        robo_long r = robo_display((int)ROBO_DISP_INPUT, 0, &ev, sizeof(ev), 0);
        if ((uint64_t)r >= (uint64_t)-4095ll) {
            robo_sleep(5);
            continue;
        }
        if (ev.type != ROBO_KEY_DOWN)
            continue;
        if (ev.code == ROBO_KEY_ENTER) {
            putch('\n');
            g_line[g_linelen] = 0;
            redraw();
            if (run_line(g_line))
                break;
            g_linelen = 0;
            put("$ ");
            redraw();
            continue;
        }
        if (ev.code == ROBO_KEY_BACKSPACE) {
            if (g_linelen) {
                g_linelen--;
                putch('\b');
                redraw();
            }
            continue;
        }
        if (ev.code == ROBO_KEY_ESC) {
            g_linelen = 0;
            put("^\n$ ");
            redraw();
            continue;
        }
        {
            char c = key_to_ascii(ev.code, ev.modifiers);
            if (c && g_linelen + 1 < sizeof(g_line)) {
                g_line[g_linelen++] = c;
                putch(c);
                redraw();
            }
        }
    }
    return 0;
}
