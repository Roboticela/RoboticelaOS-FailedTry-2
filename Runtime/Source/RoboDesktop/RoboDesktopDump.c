/*
 * RoboDesktop dump (Phase 11.13) — see RoboDesktop.h.
 *
 * Seven lines, one per group of facts: the geometry, the window list, the lifecycle counters, the drawing
 * counts, the content and presentation counts, the pointer, and the checksum. The grouping is the reader's
 * rather than the struct's — a caller comparing two desktops compares *lines*, so a resize shows up in the
 * first line, a pointer move in the sixth, and a repaint in the seventh.
 *
 * Three rules this file inherits or states, and all three are why it is a door rather than a `printf`:
 *
 *   **no address, ever.** Every number here is a count, a coordinate or a checksum, all of them functions
 *   of the input. A pointer in a dump would be the one value that changes between two runs of the same
 *   shell, which would make the deterministic thing about the dump the thing that is not;
 *
 *   **the needed length is always reported.** A short buffer returns `RoboNotSupported` with `outLength`
 *   set to what the text *would* have taken — which is why the writer keeps counting after it stops
 *   storing;
 *
 *   **the visible count is computed, not stored.** The shell keeps a window list; how many of its windows
 *   are on screen is a question the list answers, and a counter kept beside it would be a second authority
 *   on the same fact.
 *
 * Every field is read through a public door, which is why this file includes the module's front door and
 * not the private header its siblings share: a dump that could see the shell's fields would be a second
 * place the shell's state has to stay true to.
 */

#include "RoboDesktop/RoboDesktop.h"

/** The text builder, private to this file (see the header comment). */
typedef struct desktopWriter {
    char *buffer;
    /** Bytes available, including the NUL that is always kept at @ref length. */
    size_t capacity;
    /** Bytes the text needs, whether or not they fit. */
    size_t length;
} DesktopWriter;

static void DesktopWriterInit(DesktopWriter *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

static void DesktopWriterChar(DesktopWriter *text, char c)
{
    if (text->buffer && text->length + 1u < text->capacity) {
        text->buffer[text->length] = c;
        text->buffer[text->length + 1u] = '\0';
    }
    text->length++;
}

static void DesktopWriterString(DesktopWriter *text, const char *string)
{
    if (!string) {
        return;
    }
    while (*string != '\0') {
        DesktopWriterChar(text, *string);
        string++;
    }
}

static void DesktopWriterU64(DesktopWriter *text, uint64_t value)
{
    char digits[24];
    size_t count = 0u;

    if (value == 0u) {
        DesktopWriterChar(text, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value /= 10u;
        count++;
    }
    while (count > 0u) {
        count--;
        DesktopWriterChar(text, digits[count]);
    }
}

static void DesktopWriterI32(DesktopWriter *text, int32_t value)
{
    if (value < 0) {
        DesktopWriterChar(text, '-');
        /* Negated through the unsigned range, so the most negative value prints rather than overflowing on
         * the way. */
        DesktopWriterU64(text, (uint64_t)0u - (uint64_t)(int64_t)value);
        return;
    }
    DesktopWriterU64(text, (uint64_t)value);
}

static void DesktopWriterHex64(DesktopWriter *text, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned shift = 60u;

    for (;;) {
        DesktopWriterChar(text, digits[(value >> shift) & 0xFu]);
        if (shift == 0u) {
            break;
        }
        shift -= 4u;
    }
}

/** `x,y,w,h`, in the order every rectangle in this tree is written. */
static void DesktopWriterRect(DesktopWriter *text, GfxRect rect)
{
    DesktopWriterI32(text, rect.x);
    DesktopWriterChar(text, ',');
    DesktopWriterI32(text, rect.y);
    DesktopWriterChar(text, ',');
    DesktopWriterI32(text, rect.width);
    DesktopWriterChar(text, ',');
    DesktopWriterI32(text, rect.height);
}

/** How many of the shell's windows are on screen: visible, and not asking to close. */
static size_t DesktopVisibleCount(const Desktop *desktop)
{
    size_t count = DesktopWindowCount(desktop);
    size_t index;
    size_t visible = 0u;

    for (index = 0u; index < count; index++) {
        DesktopWindowId id = RoboDesktopWindowIdNone;

        if (DesktopWindowAt(desktop, index, &id) != RoboOk) {
            continue;
        }
        if (DesktopWindowIsVisible(desktop, id)) {
            visible++;
        }
    }
    return visible;
}

Result DesktopWrite(const Desktop *desktop,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                DesktopDiag *diag)
{
    DesktopWriter text;
    DesktopStats stats;
    DesktopPointer pointer;
    DesktopGeometry geometry;

    if (!desktop || !buffer || !outLength || capacity == 0u) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboInvalidArgument,
                                 "a desktop, a buffer, a length and a capacity are required");
    }
    if (GetDesktopStats(desktop, &stats) != RoboOk ||
        GetDesktopPointer(desktop, &pointer) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboInvalidArgument,
                                 "the shell's counters could not be read");
    }
    geometry = DesktopGeometryOf(desktop);
    DesktopWriterInit(&text, buffer, capacity);

    /* 1. The geometry: the screen, then the two rectangles everything else is derived from. */
    DesktopWriterString(&text, "desktop ");
    DesktopWriterI32(&text, geometry.screen.width);
    DesktopWriterChar(&text, 'x');
    DesktopWriterI32(&text, geometry.screen.height);
    DesktopWriterString(&text, " panel=");
    DesktopWriterRect(&text, geometry.panel);
    DesktopWriterString(&text, " work=");
    DesktopWriterRect(&text, geometry.workArea);
    DesktopWriterChar(&text, '\n');

    /* 2. The window list: how many exist, how many are on screen, which one holds focus. */
    DesktopWriterString(&text, "  windows=");
    DesktopWriterU64(&text, (uint64_t)DesktopWindowCount(desktop));
    DesktopWriterString(&text, " visible=");
    DesktopWriterU64(&text, (uint64_t)DesktopVisibleCount(desktop));
    DesktopWriterString(&text, " focused=");
    DesktopWriterU64(&text, (uint64_t)DesktopWindowFocused(desktop));
    DesktopWriterChar(&text, '\n');

    /* 3. The lifecycle counters. */
    DesktopWriterString(&text, "  frames=");
    DesktopWriterU64(&text, (uint64_t)stats.frames);
    DesktopWriterString(&text, " without_windows=");
    DesktopWriterU64(&text, (uint64_t)stats.framesWithoutWindows);
    DesktopWriterString(&text, " resizes=");
    DesktopWriterU64(&text, (uint64_t)stats.resizes);
    DesktopWriterChar(&text, '\n');

    /* 4. What the frames drew: the shell's own fills and labels, and the drawings the screen cut. */
    DesktopWriterString(&text, "  fills=");
    DesktopWriterU64(&text, (uint64_t)stats.fillDrawings);
    DesktopWriterString(&text, " labels=");
    DesktopWriterU64(&text, (uint64_t)stats.labels);
    DesktopWriterString(&text, " unshaped=");
    DesktopWriterU64(&text, (uint64_t)stats.labelsUnshaped);
    DesktopWriterString(&text, " clipped=");
    DesktopWriterU64(&text, (uint64_t)stats.clippedDrawings);
    DesktopWriterChar(&text, '\n');

    /* 5. What the windows contributed, and what reached a display. */
    DesktopWriterString(&text, "  content_runs=");
    DesktopWriterU64(&text, (uint64_t)stats.contentRuns);
    DesktopWriterString(&text, " content_pixels=");
    DesktopWriterU64(&text, (uint64_t)stats.contentPixels);
    DesktopWriterString(&text, " presentations=");
    DesktopWriterU64(&text, (uint64_t)stats.presentations);
    DesktopWriterChar(&text, '\n');

    /* 6. The pointer, and what was under it the last time the shell looked. */
    DesktopWriterString(&text, "  pointer=");
    DesktopWriterI32(&text, pointer.point.x);
    DesktopWriterChar(&text, ',');
    DesktopWriterI32(&text, pointer.point.y);
    DesktopWriterString(&text, " inside=");
    DesktopWriterU64(&text, (uint64_t)(pointer.inside ? 1u : 0u));
    DesktopWriterString(&text, " pressed=");
    DesktopWriterU64(&text, (uint64_t)(pointer.pressed ? 1u : 0u));
    DesktopWriterString(&text, " area=");
    DesktopWriterString(&text, DesktopAreaName(pointer.area));
    DesktopWriterChar(&text, '\n');

    /* 7. The picture, alone on the last line where a diff shows it. */
    DesktopWriterString(&text, "  checksum=");
    DesktopWriterHex64(&text, stats.lastFrameChecksum);
    DesktopWriterChar(&text, '\n');

    *outLength = text.length;
    if (text.length + 1u > capacity) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboNotSupported,
                                 "the dump does not fit the caller's buffer");
    }
    return RoboOk;
}
