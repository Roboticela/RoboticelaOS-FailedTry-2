/*
 * RoboCSS opacity vocabulary (Phase 11.9) — see RoboCSSOpacity.h.
 *
 * Nothing in this file is a table, and that is the whole difference between this module and
 * its three neighbours. `display`, `border-style` and `overflow` are closed sets whose
 * questions are all "which row is this spelling?", so each of them is one array and one
 * lookup. Opacity's set is **uncountable** -- every real in [0, 1] is a value -- so its
 * questions are arithmetic and its one table-shaped question ("is this inside the range?")
 * is a comparison, not a search.
 *
 * The three rules, and everything below is one of them:
 *
 *   the range     `[0, 1]`, with the initial value `1` and the clamp that makes a value
 *                 outside it meaningful;
 *   the spelling  a percentage means the same number divided by a hundred, which is the
 *                 one place a *percentage of this property* rather than of a box is
 *                 converted;
 *   the text      the pinned fixed-point form every number this subsystem emits has used
 *                 since 9.8.
 *
 * The renderer is deliberately a *second* implementation of `RoboCSSUnit.c`'s
 * `NumericRender` rather than a call into it, and the reason is that it is private there:
 * the *rule* is shared -- `%.0f` for a whole value, `%.6f` otherwise with the trailing
 * zeroes and a bare point stripped, `-0` normalized to `0`, and a non-zero value the
 * fixed-point precision would render as zero falling back to a compact form -- and having
 * two copies of a twenty-line formatter is what the tests are for. Reaching into another
 * translation unit's `static` would be the thing the boundary scan exists to catch.
 */

#include "RoboCSS/RoboCSSOpacity.h"

#include "RoboCSS/RoboCSSUnit.h"

#include "RoboCSSInternal.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The range                                                           */
/* ------------------------------------------------------------------ */

double CssOpacityInitial(void)
{
    return 1.0;
}

int CssOpacityIsTransparent(double opacity)
{
    /* Exactly zero, and *after* the validity question has been asked by whoever cares:
     * a NaN is not "transparent" either, and answering 1 here for one would be a second,
     * looser rule than `CssOpacityIsValid`. */
    return opacity == 0.0;
}

int CssOpacityIsOpaque(double opacity)
{
    return opacity == 1.0;
}

int CssOpacityIsValid(double opacity)
{
    /*
     * A NaN is separated first, and it has to be: every comparison against one is false,
     * so the range test below would answer *invalid* for a NaN by accident rather than by
     * statement. `opacity == opacity` is false for exactly a NaN and true for every other
     * double including both infinities, which is the whole test and the reason this needs
     * no header.
     */
    if (!(opacity == opacity)) {
        return 0;
    }
    /* Both ends inclusive, and an infinity is refused by this rather than by a test of its
     * own: `+inf <= 1.0` is false and `-inf >= 0.0` is false. */
    return opacity >= 0.0 && opacity <= 1.0;
}

double CssOpacityClamp(double opacity)
{
    if (!(opacity == opacity)) {
        /* Not a number: the property's initial value, which is *visible*. See the header
         * comment -- a NaN answered with zero would render a caller's arithmetic bug as a
         * blank page, and one answered with one renders it as a document that is merely
         * unsurprising. */
        return CssOpacityInitial();
    }
    if (opacity > 1.0) {
        return 1.0;
    }
    if (opacity < 0.0) {
        return 0.0;
    }
    /* An infinity reaches the branches above -- `+inf > 1.0` and `-inf < 0.0` -- so it is
     * clamped rather than needing a test of its own. */
    return opacity;
}

double CssOpacityFromPercentage(double percentage)
{
    /*
     * The division is the whole of this door, and it is *not* clamped: the caller applies
     * `CssOpacityClamp` to the result exactly as it does to a number, so both
     * spellings of `<alpha-value>` meet one range rule at one point. Two clamp sites would
     * be two chances for one of them to be `[0, 100]`.
     */
    return percentage / 100.0;
}

/* ------------------------------------------------------------------ */
/* The text                                                            */
/* ------------------------------------------------------------------ */

/** Digits the formatter needs: six decimals, a sign, a point and a NUL. */
#define OPACITY_DIGITS_MAX 32u

/**
 * Render @p value in the pinned fixed-point form, or 0 when it does not fit.
 *
 * The rule `RoboCSSUnit.c` states for every number this subsystem has emitted since 9.8,
 * restated here rather than reached for: whole values have no decimal point, fractional
 * ones carry six decimals with trailing zeroes and then a bare point stripped, negative
 * zero is zero, and a non-zero value that fixed-point would render as zero falls back to a
 * compact form so the text cannot claim a value is zero when it is not.
 */
static int OpacityRender(double value, char *out, size_t capacity)
{
    char digits[OPACITY_DIGITS_MAX];
    int whole = 0;
    int written;
    size_t length;

    /* The in-range test guards the cast, in that order: a cast of a double outside the
     * integer range is undefined behaviour, and this function is reachable with any input
     * a caller's arithmetic produced. */
    if (value <= RoboCssNumericIntegerLimit &&
        value >= -RoboCssNumericIntegerLimit &&
        (double)(int64_t)value == value) {
        whole = 1;
    }
    written = snprintf(digits, sizeof(digits), whole ? "%.0f" : "%.6f", value);
    if (written < 0 || (size_t)written >= sizeof(digits)) {
        return 0;
    }
    length = (size_t)written;
    if (!whole) {
        while (length > 0u && digits[length - 1u] == '0') {
            length--;
        }
        if (length > 0u && digits[length - 1u] == '.') {
            length--;
        }
    }
    if (length == 0u || (length == 2u && digits[0] == '-' && digits[1] == '0')) {
        digits[0] = '0';
        length = 1u;
    }
    if (!whole && length == 1u && digits[0] == '0' && value != 0.0) {
        written = snprintf(digits, sizeof(digits), "%.6g", value);
        if (written < 0 || (size_t)written >= sizeof(digits)) {
            return 0;
        }
        length = (size_t)written;
    }
    if (capacity <= length) {
        return 0;
    }
    memcpy(out, digits, length);
    out[length] = '\0';
    return 1;
}

Result CssOpacityWrite(double opacity,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     CssDiag *diag)
{
    char digits[OPACITY_DIGITS_MAX];
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!CssOpacityIsValid(opacity)) {
        /*
         * Refused rather than printed, and the refusal is the point of the door: a caller
         * receives text for a value this build would draw with or it receives a diagnostic.
         * Printing `2` or a NaN would be a serializer that made an out-of-range number look
         * like a record, and a reader of a dump has no way to tell the difference.
         */
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "not an opacity value");
    }
    if (!OpacityRender(opacity, digits, sizeof(digits))) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotSupported,
                             "the opacity does not fit its canonical form");
    }
    length = strlen(digits);
    if (length >= capacity) {
        /* The length it would have needed, so a caller can size a buffer from the refusal
         * rather than from a constant it has to keep in step. */
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotSupported,
                             "the opacity's text does not fit");
    }
    memcpy(buffer, digits, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}
