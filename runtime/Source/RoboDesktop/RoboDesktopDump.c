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
 *   **the needed length is always reported.** A short buffer returns `ROBO_NOT_SUPPORTED` with `out_length`
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
typedef struct desktop_writer {
    char *buffer;
    /** Bytes available, including the NUL that is always kept at @ref length. */
    size_t capacity;
    /** Bytes the text needs, whether or not they fit. */
    size_t length;
} desktop_writer_t;

static void desktop_writer_init(desktop_writer_t *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0u;
    if (buffer && capacity > 0u) {
        buffer[0] = '\0';
    }
}

static void desktop_writer_char(desktop_writer_t *text, char c)
{
    if (text->buffer && text->length + 1u < text->capacity) {
        text->buffer[text->length] = c;
        text->buffer[text->length + 1u] = '\0';
    }
    text->length++;
}

static void desktop_writer_string(desktop_writer_t *text, const char *string)
{
    if (!string) {
        return;
    }
    while (*string != '\0') {
        desktop_writer_char(text, *string);
        string++;
    }
}

static void desktop_writer_u64(desktop_writer_t *text, uint64_t value)
{
    char digits[24];
    size_t count = 0u;

    if (value == 0u) {
        desktop_writer_char(text, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value /= 10u;
        count++;
    }
    while (count > 0u) {
        count--;
        desktop_writer_char(text, digits[count]);
    }
}

static void desktop_writer_i32(desktop_writer_t *text, int32_t value)
{
    if (value < 0) {
        desktop_writer_char(text, '-');
        /* Negated through the unsigned range, so the most negative value prints rather than overflowing on
         * the way. */
        desktop_writer_u64(text, (uint64_t)0u - (uint64_t)(int64_t)value);
        return;
    }
    desktop_writer_u64(text, (uint64_t)value);
}

static void desktop_writer_hex64(desktop_writer_t *text, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned shift = 60u;

    for (;;) {
        desktop_writer_char(text, digits[(value >> shift) & 0xFu]);
        if (shift == 0u) {
            break;
        }
        shift -= 4u;
    }
}

/** `x,y,w,h`, in the order every rectangle in this tree is written. */
static void desktop_writer_rect(desktop_writer_t *text, robo_gfx_rect_t rect)
{
    desktop_writer_i32(text, rect.x);
    desktop_writer_char(text, ',');
    desktop_writer_i32(text, rect.y);
    desktop_writer_char(text, ',');
    desktop_writer_i32(text, rect.width);
    desktop_writer_char(text, ',');
    desktop_writer_i32(text, rect.height);
}

/** How many of the shell's windows are on screen: visible, and not asking to close. */
static size_t desktop_visible_count(const robo_desktop_t *desktop)
{
    size_t count = robo_desktop_window_count(desktop);
    size_t index;
    size_t visible = 0u;

    for (index = 0u; index < count; index++) {
        robo_desktop_window_id_t id = ROBO_DESKTOP_WINDOW_ID_NONE;

        if (robo_desktop_window_at(desktop, index, &id) != ROBO_OK) {
            continue;
        }
        if (robo_desktop_window_is_visible(desktop, id)) {
            visible++;
        }
    }
    return visible;
}

robo_result_t robo_desktop_write(const robo_desktop_t *desktop,
                                char *buffer,
                                size_t capacity,
                                size_t *out_length,
                                robo_desktop_diag_t *diag)
{
    desktop_writer_t text;
    robo_desktop_stats_t stats;
    robo_desktop_pointer_t pointer;
    robo_desktop_geometry_t geometry;

    if (!desktop || !buffer || !out_length || capacity == 0u) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_DUMP, ROBO_INVALID_ARGUMENT,
                                 "a desktop, a buffer, a length and a capacity are required");
    }
    if (robo_desktop_stats(desktop, &stats) != ROBO_OK ||
        robo_desktop_pointer(desktop, &pointer) != ROBO_OK) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_DUMP, ROBO_INVALID_ARGUMENT,
                                 "the shell's counters could not be read");
    }
    geometry = robo_desktop_geometry_of(desktop);
    desktop_writer_init(&text, buffer, capacity);

    /* 1. The geometry: the screen, then the two rectangles everything else is derived from. */
    desktop_writer_string(&text, "desktop ");
    desktop_writer_i32(&text, geometry.screen.width);
    desktop_writer_char(&text, 'x');
    desktop_writer_i32(&text, geometry.screen.height);
    desktop_writer_string(&text, " panel=");
    desktop_writer_rect(&text, geometry.panel);
    desktop_writer_string(&text, " work=");
    desktop_writer_rect(&text, geometry.work_area);
    desktop_writer_char(&text, '\n');

    /* 2. The window list: how many exist, how many are on screen, which one holds focus. */
    desktop_writer_string(&text, "  windows=");
    desktop_writer_u64(&text, (uint64_t)robo_desktop_window_count(desktop));
    desktop_writer_string(&text, " visible=");
    desktop_writer_u64(&text, (uint64_t)desktop_visible_count(desktop));
    desktop_writer_string(&text, " focused=");
    desktop_writer_u64(&text, (uint64_t)robo_desktop_window_focused(desktop));
    desktop_writer_char(&text, '\n');

    /* 3. The lifecycle counters. */
    desktop_writer_string(&text, "  frames=");
    desktop_writer_u64(&text, (uint64_t)stats.frames);
    desktop_writer_string(&text, " without_windows=");
    desktop_writer_u64(&text, (uint64_t)stats.frames_without_windows);
    desktop_writer_string(&text, " resizes=");
    desktop_writer_u64(&text, (uint64_t)stats.resizes);
    desktop_writer_char(&text, '\n');

    /* 4. What the frames drew: the shell's own fills and labels, and the drawings the screen cut. */
    desktop_writer_string(&text, "  fills=");
    desktop_writer_u64(&text, (uint64_t)stats.fill_drawings);
    desktop_writer_string(&text, " labels=");
    desktop_writer_u64(&text, (uint64_t)stats.labels);
    desktop_writer_string(&text, " unshaped=");
    desktop_writer_u64(&text, (uint64_t)stats.labels_unshaped);
    desktop_writer_string(&text, " clipped=");
    desktop_writer_u64(&text, (uint64_t)stats.clipped_drawings);
    desktop_writer_char(&text, '\n');

    /* 5. What the windows contributed, and what reached a display. */
    desktop_writer_string(&text, "  content_runs=");
    desktop_writer_u64(&text, (uint64_t)stats.content_runs);
    desktop_writer_string(&text, " content_pixels=");
    desktop_writer_u64(&text, (uint64_t)stats.content_pixels);
    desktop_writer_string(&text, " presentations=");
    desktop_writer_u64(&text, (uint64_t)stats.presentations);
    desktop_writer_char(&text, '\n');

    /* 6. The pointer, and what was under it the last time the shell looked. */
    desktop_writer_string(&text, "  pointer=");
    desktop_writer_i32(&text, pointer.point.x);
    desktop_writer_char(&text, ',');
    desktop_writer_i32(&text, pointer.point.y);
    desktop_writer_string(&text, " inside=");
    desktop_writer_u64(&text, (uint64_t)(pointer.inside ? 1u : 0u));
    desktop_writer_string(&text, " pressed=");
    desktop_writer_u64(&text, (uint64_t)(pointer.pressed ? 1u : 0u));
    desktop_writer_string(&text, " area=");
    desktop_writer_string(&text, robo_desktop_area_name(pointer.area));
    desktop_writer_char(&text, '\n');

    /* 7. The picture, alone on the last line where a diff shows it. */
    desktop_writer_string(&text, "  checksum=");
    desktop_writer_hex64(&text, stats.last_frame_checksum);
    desktop_writer_char(&text, '\n');

    *out_length = text.length;
    if (text.length + 1u > capacity) {
        return ROBO_DESKTOP_DIAG(diag, ROBO_DESKTOP_DOMAIN_DUMP, ROBO_NOT_SUPPORTED,
                                 "the dump does not fit the caller's buffer");
    }
    return ROBO_OK;
}
