#include <robosys/syscall.h>
#include <robosys/display.h>
#include <robosys/input.h>
#include <robosys/syscallNumbers.h>

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
static const uint8_t gFont[95][8] = {
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

static uint32_t *gFb;
static uint32_t gW, gH, gStride;
static uint32_t gPixfmt;
static int gFbOk;
static char gRows[MAX_ROWS][MAX_COLS];
static int gCols, gNrows;
static int gCx, gCy;
static char gLine[LINE_MAX];
static unsigned gLinelen;

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

static void SerialWrite(const char *s, unsigned n)
{
    if (n)
        Write(1, s, n);
}

static void GlyphDraw(int col, int row, char ch, uint32_t fg)
{
    const uint8_t *g;
    int x0, y0, y, x;
    unsigned idx;
    if (!gFbOk || col < 0 || row < 0 || col >= gCols || row >= gNrows)
        return;
    if (ch < 32 || ch > 126)
        ch = '?';
    idx = (unsigned)(ch - 32);
    g = gFont[idx];
    x0 = col * FONT_W + 8;
    y0 = row * FONT_H + 8;
    for (y = 0; y < FONT_H; y++) {
        uint8_t bits = g[y];
        for (x = 0; x < FONT_W; x++) {
            uint32_t c = (bits & (0x80u >> x)) ? fg : COL_BG;
            int px = x0 + x;
            int py = y0 + y;
            if (px >= 0 && py >= 0 && (uint32_t)px < gW && (uint32_t)py < gH)
                gFb[(uint32_t)py * gStride + (uint32_t)px] = c;
        }
    }
}

static void present(void)
{
    RoboDisplayPresent req;
    if (!gFbOk)
        return;
    req.pixels = (uint64_t)(uintptr_t)gFb;
    req.width = gW;
    req.height = gH;
    req.stride = gStride;
    req.pixfmt = gPixfmt;
    req.destX = 0;
    req.destY = 0;
    Display((int)RoboDispPresent, 0, &req, sizeof(req), 0);
}

static void redraw(void)
{
    uint32_t i, n;
    int r, c;
    if (!gFbOk)
        return;
    n = gStride * gH;
    for (i = 0; i < n; i++)
        gFb[i] = COL_BG;
    for (r = 0; r < gNrows; r++) {
        for (c = 0; c < gCols; c++) {
            char ch = gRows[r][c];
            if (ch)
                GlyphDraw(c, r, ch, COL_FG);
        }
    }
    GlyphDraw(gCx, gCy, '_', COL_DIM);
    present();
}

static void ScrollUp(void)
{
    int r, c;
    for (r = 0; r < gNrows - 1; r++) {
        for (c = 0; c < gCols; c++)
            gRows[r][c] = gRows[r + 1][c];
    }
    for (c = 0; c < gCols; c++)
        gRows[gNrows - 1][c] = 0;
    if (gCy > 0)
        gCy--;
}

static void PutChar(char ch)
{
    if (ch == '\n') {
        gCx = 0;
        gCy++;
        if (gCy >= gNrows) {
            ScrollUp();
            gCy = gNrows - 1;
        }
        return;
    }
    if (ch == '\r')
        return;
    if (ch == '\b') {
        if (gCx > 0) {
            gCx--;
            gRows[gCy][gCx] = 0;
        }
        return;
    }
    if (gCx >= gCols) {
        PutChar('\n');
    }
    gRows[gCy][gCx] = ch;
    gCx++;
}

static void put(const char *s)
{
    unsigned i = 0;
    SerialWrite(s, slen(s));
    while (s && s[i]) {
        PutChar(s[i]);
        i++;
    }
}

static void putch(char c)
{
    char b[2];
    b[0] = c;
    b[1] = 0;
    SerialWrite(b, 1);
    PutChar(c);
}

static void line(const char *s)
{
    put(s);
    put("\n");
}

static char KeyToAscii(uint32_t code, uint32_t mods)
{
    int shift = (mods & RoboModShift) != 0;
    if (code >= RoboKeyA && code <= RoboKeyZ) {
        char c = (char)('a' + (code - RoboKeyA));
        if (shift)
            c = (char)(c - 'a' + 'A');
        return c;
    }
    switch (code) {
    case RoboKey1: return shift ? '!' : '1';
    case RoboKey2: return shift ? '@' : '2';
    case RoboKey3: return shift ? '#' : '3';
    case RoboKey4: return shift ? '$' : '4';
    case RoboKey5: return shift ? '%' : '5';
    case RoboKey6: return shift ? '^' : '6';
    case RoboKey7: return shift ? '&' : '7';
    case RoboKey8: return shift ? '*' : '8';
    case RoboKey9: return shift ? '(' : '9';
    case RoboKey0: return shift ? ')' : '0';
    case RoboKeySpace: return ' ';
    case RoboKeyMinus: return shift ? '_' : '-';
    case RoboKeyEqual: return shift ? '+' : '=';
    case RoboKeyLbracket: return shift ? '{' : '[';
    case RoboKeyRbracket: return shift ? '}' : ']';
    case RoboKeyBackslash: return shift ? '|' : '\\';
    case RoboKeySemicolon: return shift ? ':' : ';';
    case RoboKeyQuote: return shift ? '"' : '\'';
    case RoboKeyGrave: return shift ? '~' : '`';
    case RoboKeyComma: return shift ? '<' : ',';
    case RoboKeyDot: return shift ? '>' : '.';
    case RoboKeySlash: return shift ? '?' : '/';
    case RoboKeyTab: return ' ';
    default: return 0;
    }
}

static int ResolveCmd(const char *cmd, char *path, unsigned pathMax)
{
    static const char *prefs[] = { "/bin/", "/System/bin/", 0 };
    unsigned p;
    if (!cmd || !cmd[0] || !path || !pathMax)
        return -1;
    if (cmd[0] == '/') {
        unsigned i = 0;
        while (cmd[i] && i + 1 < pathMax) {
            path[i] = cmd[i];
            i++;
        }
        path[i] = 0;
        return 0;
    }
    for (p = 0; prefs[p]; p++) {
        RoboStat st;
        unsigned i = 0, j = 0;
        while (prefs[p][i] && j + 1 < pathMax)
            path[j++] = prefs[p][i++];
        i = 0;
        while (cmd[i] && j + 1 < pathMax)
            path[j++] = cmd[i++];
        path[j] = 0;
        if (Stat(path, &st) == 0 && st.size > 0)
            return 0;
    }
    return -1;
}

static int RunLine(char *cmdline)
{
    char *argv[16];
    unsigned argc = 0;
    unsigned i = 0;
    char path[192];
    Long pid;
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
            for (r = 0; r < gNrows; r++)
                for (c = 0; c < gCols; c++)
                    gRows[r][c] = 0;
            gCx = gCy = 0;
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
    if (ResolveCmd(argv[0], path, sizeof(path)) != 0) {
        put("command not found: ");
        line(argv[0]);
        return 0;
    }
    pid = ProcessSpawn(path, argv, (int)argc);
    if ((uint64_t)pid >= (uint64_t)-4095ll || pid <= 0) {
        put("spawn failed: ");
        line(path);
        return 0;
    }
    if (Wait((Pid)pid, 0, &status) < 0)
        line("wait failed");
    return 0;
}

static int InitDisplay(void)
{
    RoboUserDisplay info;
    Long r;
    uint64_t bytes;
    void *mem;

    r = Display((int)RoboDispClaim, 0, 0, 0,
                     (int)(RoboDispRoleOutput | RoboDispRoleInput));
    if ((uint64_t)r >= (uint64_t)-4095ll) {
        SerialWrite("terminal: display claim failed\n", 31);
        return 0;
    }
    if ((uint64_t)Display((int)RoboDispInfo, 0, &info, sizeof(info), 0) >=
        (uint64_t)-4095ll)
        return 0;
    if (!info.ready || !info.width || !info.height)
        return 0;
    gW = info.width;
    gH = info.height;
    gStride = info.width;
    gPixfmt = info.pixfmt;
    if (gW > 1920u)
        gW = 1920u;
    if (gH > 1080u)
        gH = 1080u;
    bytes = (uint64_t)gStride * (uint64_t)gH * 4ull;
    mem = (void *)(uintptr_t)Mmap(0, (Ulong)bytes,
                                       (int)(RoboProtRead | RoboProtWrite),
                                       (int)(RoboMapPrivate | RoboMapAnon),
                                       -1, 0);
    if ((uint64_t)(uintptr_t)mem >= (uint64_t)-4095ll || !mem)
        return 0;
    gFb = (uint32_t *)mem;
    gCols = (int)((gW - 16u) / FONT_W);
    gNrows = (int)((gH - 16u) / FONT_H);
    if (gCols > MAX_COLS)
        gCols = MAX_COLS;
    if (gNrows > MAX_ROWS)
        gNrows = MAX_ROWS;
    if (gCols < 40)
        gCols = 40;
    if (gNrows < 12)
        gNrows = 12;
    gFbOk = 1;
    return 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!InitDisplay()) {
        SerialWrite("Roboticela terminal (serial only)\n", 34);
    }

    line("RoboticelaOS terminal");
    line("Type help. Package manager: robo");
    put("$ ");
    redraw();

    gLinelen = 0;
    for (;;) {
        RoboInputEvent ev;
        Long r = Display((int)RoboDispInput, 0, &ev, sizeof(ev), 0);
        if ((uint64_t)r >= (uint64_t)-4095ll) {
            RoboSleep(5);
            continue;
        }
        if (ev.type != RoboKeyDown)
            continue;
        if (ev.code == RoboKeyEnter) {
            putch('\n');
            gLine[gLinelen] = 0;
            redraw();
            if (RunLine(gLine))
                break;
            gLinelen = 0;
            put("$ ");
            redraw();
            continue;
        }
        if (ev.code == RoboKeyBackspace) {
            if (gLinelen) {
                gLinelen--;
                putch('\b');
                redraw();
            }
            continue;
        }
        if (ev.code == RoboKeyEsc) {
            gLinelen = 0;
            put("^\n$ ");
            redraw();
            continue;
        }
        {
            char c = KeyToAscii(ev.code, ev.modifiers);
            if (c && gLinelen + 1 < sizeof(gLine)) {
                gLine[gLinelen++] = c;
                putch(c);
                redraw();
            }
        }
    }
    return 0;
}
