/*
 * RoboTypography's scale (Phase 12.3) — see RoboTypographyScale.h.
 *
 * The table is the file. Everything else here is a way to read it: a lookup, the HTML element
 * vocabulary the document defaults are emitted for, and the emitter itself.
 *
 * Two decisions are worth reading in the code rather than only in the header.
 *
 * **The sizes cluster on two bands.** The UI roles sit in 11–13 px because the shell's own geometry
 * already fixed the bands they are drawn in (a 30 px panel, a 24 px title bar) and the phase is not
 * allowed to redesign the desktop. The document roles are free to be larger, and heading 1 at 32 px
 * is the one place in this build where type is *display* sized. Binding the UI roles therefore
 * changes which face and which weight a surface draws with -- which is the phase's whole point --
 * without moving anything.
 *
 * **The emitter writes integral lengths as integers.** `16px`, not `16.0px`. Both are valid CSS and
 * the choice is cosmetic, but it is made once here rather than left to whichever formatting a caller
 * reached for, because the generated sheet's bytes are part of a document's frame checksum and a
 * cosmetic difference between two runs of one scale would look like a typography bug.
 */

#include "RoboTypography/RoboTypographyScale.h"

#include "RoboTypography/RoboTypographyDiagnostics.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The default scale                                                   */
/* ------------------------------------------------------------------ */

/**
 * One entry per role, in role order.
 *
 * Kept as one array rather than a switch so that it can be copied, replaced and walked -- and so
 * that the ordering rule below (roles strictly increasing) is a property of the data a test can
 * check rather than of a control flow a reader has to follow.
 */
static const RoboTypographyScaleEntry TypographyDefaultScale[RoboTypographyScaleCount] = {
    /* Desktop: the panel's identity and a window's own heading. */
    {RoboTypographyRoleDesktopTitle, RoboFontSystemRoleUIDisplay, RoboTypographyWeightSemiBold,
     13.0f, 16.0f, RoboTypographyOpticalDisplay},
    {RoboTypographyRoleDesktopSubtitle, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     11.0f, 14.0f, RoboTypographyOpticalSmall},
    {RoboTypographyRoleWindowTitle, RoboFontSystemRoleUIText, RoboTypographyWeightMedium,
     13.0f, 16.0f, RoboTypographyOpticalUI},
    {RoboTypographyRoleWindowSubtitle, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     11.0f, 14.0f, RoboTypographyOpticalSmall},

    /* Chrome: the controls. A button reads *medium* to sit above a label without shouting. */
    {RoboTypographyRoleChromeLabel, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     12.0f, 15.0f, RoboTypographyOpticalUI},
    {RoboTypographyRoleChromeButton, RoboFontSystemRoleUIText, RoboTypographyWeightMedium,
     12.0f, 15.0f, RoboTypographyOpticalUI},
    {RoboTypographyRoleChromeMenu, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     12.0f, 15.0f, RoboTypographyOpticalUI},
    {RoboTypographyRoleChromeMenuItem, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     12.0f, 16.0f, RoboTypographyOpticalUI},
    {RoboTypographyRoleChromeTab, RoboFontSystemRoleUIText, RoboTypographyWeightMedium,
     12.0f, 15.0f, RoboTypographyOpticalUI},
    {RoboTypographyRoleChromeAddress, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     12.0f, 15.0f, RoboTypographyOpticalUI},
    {RoboTypographyRoleChromeStatus, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     11.0f, 14.0f, RoboTypographyOpticalSmall},

    /* Document: the defaults a rendered page starts from. */
    {RoboTypographyRoleDocumentBody, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     16.0f, 24.0f, RoboTypographyOpticalBody},
    {RoboTypographyRoleDocumentHeading1, RoboFontSystemRoleUIDisplay,
     RoboTypographyWeightSemiBold, 32.0f, 40.0f, RoboTypographyOpticalLargeDisplay},
    {RoboTypographyRoleDocumentHeading2, RoboFontSystemRoleUIDisplay,
     RoboTypographyWeightSemiBold, 24.0f, 32.0f, RoboTypographyOpticalDisplay},
    {RoboTypographyRoleDocumentHeading3, RoboFontSystemRoleUIDisplay,
     RoboTypographyWeightMedium, 19.0f, 26.0f, RoboTypographyOpticalDisplay},
    {RoboTypographyRoleDocumentCaption, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     13.0f, 18.0f, RoboTypographyOpticalBody},
    {RoboTypographyRoleDocumentCode, RoboFontSystemRoleMonospace, RoboTypographyWeightRegular,
     14.0f, 20.0f, RoboTypographyOpticalBody},

    /* System: what a caller asks for when it means "whatever the system says". */
    {RoboTypographyRoleSystemLabel, RoboFontSystemRoleUIText, RoboTypographyWeightRegular,
     12.0f, 15.0f, RoboTypographyOpticalUI},
    {RoboTypographyRoleSystemSecondaryLabel, RoboFontSystemRoleUIText,
     RoboTypographyWeightRegular, 11.0f, 14.0f, RoboTypographyOpticalSmall},
    {RoboTypographyRoleSystemEmphasis, RoboFontSystemRoleUIDisplay,
     RoboTypographyWeightSemiBold, 11.0f, 14.0f, RoboTypographyOpticalUI},
    {RoboTypographyRoleSystemMonospace, RoboFontSystemRoleMonospace,
     RoboTypographyWeightRegular, 12.0f, 16.0f, RoboTypographyOpticalBody},
};

int RoboTypographyScaleEntryIsValid(const RoboTypographyScaleEntry *entry)
{
    if (!entry) {
        return 0;
    }
    if (!RoboTypographyRoleIsValid(entry->role)) {
        return 0;
    }
    if (!RoboTypographyOpticalSizeIsValid(entry->opticalSize)) {
        return 0;
    }
    if (!RoboFontSystemRoleIsValid(entry->fontRole)) {
        return 0;
    }
    if (entry->weight != 0u &&
        RoboTypographyWeightName(entry->weight) == NULL) {
        /* Not one of the vocabulary's four: a scale that names a weight the rest of the module
         * cannot round to is a scale whose step-down would be a surprise. */
        return 0;
    }
    if (!(entry->sizePx > 0.0f) || !(entry->lineHeightPx > 0.0f)) {
        return 0;
    }
    /* A leading below the size overlaps ascenders with descenders -- the one thing a scale entry is
     * responsible for getting right, so it is refused here rather than discovered in the pixels. */
    if (entry->lineHeightPx < entry->sizePx) {
        return 0;
    }
    return 1;
}

Result RoboTypographyDefaultScale(RoboTypographyScaleEntry *out,
                                  size_t capacity,
                                  size_t *outCount)
{
    size_t index;

    if (!out || capacity == 0u) {
        return RoboInvalidArgument;
    }
    if (capacity < RoboTypographyScaleCount) {
        return RoboTypographyDiag(NULL, RoboTypographyDomainScale, RoboInvalidArgument,
                                  "the buffer is too small for the whole scale; a partial scale "
                                  "would leave a role unresolvable");
    }
    for (index = 0u; index < RoboTypographyScaleCount; index++) {
        out[index] = TypographyDefaultScale[index];
    }
    if (outCount) {
        *outCount = RoboTypographyScaleCount;
    }
    /* The table's own invariant, checked where it is copied rather than trusted: one entry per role,
     * strictly increasing. A hand edit that duplicated or skipped a role is a scale that would answer
     * wrongly for a role, and it is caught here for the cost of twenty-one comparisons. */
    for (index = 0u; index < RoboTypographyScaleCount; index++) {
        if (!RoboTypographyScaleEntryIsValid(&out[index]) ||
            (size_t)out[index].role != index + 1u) {
            return RoboTypographyDiag(NULL, RoboTypographyDomainScale, RoboError,
                                      "the default scale does not hold one valid entry per role in "
                                      "role order");
        }
    }
    return RoboOk;
}

const RoboTypographyScaleEntry *RoboTypographyScaleFindEntry(
    const RoboTypographyScaleEntry *scale,
    size_t scaleCount,
    RoboTypographyRole role)
{
    size_t index;

    if (!scale || !RoboTypographyRoleIsValid(role)) {
        return NULL;
    }
    for (index = 0u; index < scaleCount; index++) {
        if (scale[index].role == role) {
            return &scale[index];
        }
    }
    return NULL;
}

Result RoboTypographyScaleForRole(RoboTypographyRole role,
                                  RoboTypographyScaleEntry *out)
{
    const RoboTypographyScaleEntry *entry;

    if (!out) {
        return RoboInvalidArgument;
    }
    if (!RoboTypographyRoleIsValid(role)) {
        return RoboTypographyDiag(NULL, RoboTypographyDomainRole, RoboInvalidArgument,
                                  "that is not a defined typography role");
    }
    entry = RoboTypographyScaleFindEntry(TypographyDefaultScale, RoboTypographyScaleCount, role);
    if (!entry) {
        return RoboTypographyDiag(NULL, RoboTypographyDomainScale, RoboNotFound,
                                  "the default scale has no entry for that role");
    }
    *out = *entry;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The HTML element vocabulary                                         */
/* ------------------------------------------------------------------ */

/** Casefold equality between an element name slice and a NUL-terminated name. */
static int ElementNameEquals(const char *name, size_t length, const char *expected)
{
    size_t index = 0u;

    if (!name || !expected) {
        return 0;
    }
    while (index < length) {
        char c = name[index];
        char e = expected[index];

        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (e == '\0' || c != e) {
            return 0;
        }
        index++;
    }
    return expected[index] == '\0';
}

Result RoboTypographyRoleForHtmlElement(const char *name,
                                        size_t nameLen,
                                        RoboTypographyRole *out)
{
    size_t length;

    if (!name || !out) {
        return RoboInvalidArgument;
    }
    length = (nameLen != 0u) ? nameLen : strlen(name);
    if (length == 0u) {
        return RoboNotFound;
    }

    if (ElementNameEquals(name, length, "body")) {
        *out = RoboTypographyRoleDocumentBody;
        return RoboOk;
    }
    if (ElementNameEquals(name, length, "h1")) {
        *out = RoboTypographyRoleDocumentHeading1;
        return RoboOk;
    }
    if (ElementNameEquals(name, length, "h2")) {
        *out = RoboTypographyRoleDocumentHeading2;
        return RoboOk;
    }
    if (ElementNameEquals(name, length, "h3")) {
        *out = RoboTypographyRoleDocumentHeading3;
        return RoboOk;
    }
    /* `h4`-`h6` take heading 3 and that is a decision rather than an omission: this build's
     * vocabulary has three heading roles, and a second, smaller heading size invented here would be
     * a type setting that exists nowhere else in the system. */
    if (ElementNameEquals(name, length, "h4") || ElementNameEquals(name, length, "h5") ||
        ElementNameEquals(name, length, "h6")) {
        *out = RoboTypographyRoleDocumentHeading3;
        return RoboOk;
    }
    if (ElementNameEquals(name, length, "small") || ElementNameEquals(name, length, "caption") ||
        ElementNameEquals(name, length, "figcaption")) {
        *out = RoboTypographyRoleDocumentCaption;
        return RoboOk;
    }
    if (ElementNameEquals(name, length, "code") || ElementNameEquals(name, length, "pre") ||
        ElementNameEquals(name, length, "kbd") || ElementNameEquals(name, length, "samp") ||
        ElementNameEquals(name, length, "tt")) {
        *out = RoboTypographyRoleDocumentCode;
        return RoboOk;
    }
    /* Everything else inherits the body's setting, which is why it has no rule of its own: a `p`,
     * a `li` or a `td` that repeated the body's numbers would be a second copy that could drift. */
    return RoboNotFound;
}

/* ------------------------------------------------------------------ */
/* The generated document sheet                                        */
/* ------------------------------------------------------------------ */

/** One emitted rule: the document role and the selectors it is written for. */
typedef struct DocumentSheetRule {
    RoboTypographyRole role;
    const char *selectors;
} DocumentSheetRule;

static const DocumentSheetRule TypographyDocumentRules[] = {
    {RoboTypographyRoleDocumentBody, "body"},
    {RoboTypographyRoleDocumentHeading1, "h1"},
    {RoboTypographyRoleDocumentHeading2, "h2"},
    {RoboTypographyRoleDocumentHeading3, "h3"},
    /* `h4`-`h6` share heading 3's setting, so they share its rule. */
    {RoboTypographyRoleDocumentHeading3, "h4,h5,h6"},
    {RoboTypographyRoleDocumentCaption, "small,figcaption,caption"},
    {RoboTypographyRoleDocumentCode, "code,pre,kbd,samp,tt"},
};
#define TypographyDocumentRuleCount \
    (sizeof(TypographyDocumentRules) / sizeof(TypographyDocumentRules[0]))

static int SheetAppend(char *buffer,
                       size_t capacity,
                       size_t *cursor,
                       size_t *needed,
                       const char *text)
{
    size_t index = 0u;

    if (!text) {
        return 1;
    }
    while (text[index] != '\0') {
        (*needed)++;
        if (*cursor + 1u < capacity) {
            buffer[*cursor] = text[index];
            (*cursor)++;
            buffer[*cursor] = '\0';
        }
        index++;
    }
    return 1;
}

/** Append a length: an integral value as `16px`, a fractional one as `13.5px`. */
static void SheetAppendPx(char *buffer,
                          size_t capacity,
                          size_t *cursor,
                          size_t *needed,
                          float value)
{
    char digits[16];
    size_t count = 0u;
    uint32_t scaled = (uint32_t)(value * 10.0f + 0.5f);
    uint32_t whole = scaled / 10u;
    uint32_t fraction = scaled % 10u;

    if (whole == 0u) {
        digits[count++] = '0';
    }
    while (whole != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (whole % 10u));
        whole /= 10u;
    }
    while (count > 0u) {
        char one[2];
        one[0] = digits[--count];
        one[1] = '\0';
        SheetAppend(buffer, capacity, cursor, needed, one);
    }
    if (fraction != 0u) {
        char one[2];
        SheetAppend(buffer, capacity, cursor, needed, ".");
        one[0] = (char)('0' + fraction);
        one[1] = '\0';
        SheetAppend(buffer, capacity, cursor, needed, one);
    }
    SheetAppend(buffer, capacity, cursor, needed, "px");
}

/** Append a CSS numeric weight (`400`..`700`). */
static void SheetAppendWeight(char *buffer,
                              size_t capacity,
                              size_t *cursor,
                              size_t *needed,
                              FontWeight weight)
{
    char digits[8];
    size_t count = 0u;
    uint32_t value = (uint32_t)weight;

    if (value == 0u) {
        digits[count++] = '0';
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u) {
        char one[2];
        one[0] = digits[--count];
        one[1] = '\0';
        SheetAppend(buffer, capacity, cursor, needed, one);
    }
}

Result RoboTypographyBuildDocumentStyleSheet(const RoboTypographyScaleEntry *scale,
                                             size_t scaleCount,
                                             char *buffer,
                                             size_t capacity,
                                             size_t *outLength)
{
    RoboTypographyScaleEntry local[RoboTypographyScaleCount];
    size_t cursor = 0u;
    size_t needed = 0u;
    size_t index;

    if (!buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    if (outLength) {
        *outLength = 0u;
    }
    if (!scale || scaleCount == 0u) {
        if (RoboTypographyDefaultScale(local, RoboTypographyScaleCount, NULL) != RoboOk) {
            return RoboTypographyDiag(NULL, RoboTypographyDomainScale, RoboError,
                                      "the default scale could not be read");
        }
        scale = local;
        scaleCount = RoboTypographyScaleCount;
    }

    for (index = 0u; index < TypographyDocumentRuleCount; index++) {
        const DocumentSheetRule *rule = &TypographyDocumentRules[index];
        const RoboTypographyScaleEntry *entry =
            RoboTypographyScaleFindEntry(scale, scaleCount, rule->role);

        if (!entry) {
            /* A caller's scale with a document role missing emits no rule for it rather than a
             * partial one: the element then keeps the layout engine's own initial value, which is a
             * defined state, where half a rule would be a type setting nobody chose. */
            continue;
        }
        SheetAppend(buffer, capacity, &cursor, &needed, rule->selectors);
        SheetAppend(buffer, capacity, &cursor, &needed, "{font-size:");
        SheetAppendPx(buffer, capacity, &cursor, &needed, entry->sizePx);
        SheetAppend(buffer, capacity, &cursor, &needed, ";font-weight:");
        SheetAppendWeight(buffer, capacity, &cursor, &needed, entry->weight);
        SheetAppend(buffer, capacity, &cursor, &needed, ";line-height:");
        SheetAppendPx(buffer, capacity, &cursor, &needed, entry->lineHeightPx);
        SheetAppend(buffer, capacity, &cursor, &needed, ";}");
    }

    if (outLength) {
        *outLength = needed;
    }
    if (needed + 1u > capacity) {
        /* The caller is told the length it needed rather than handed a truncated sheet: a stylesheet
         * cut mid-declaration would parse as something nobody wrote. */
        return RoboTypographyDiag(NULL, RoboTypographyDomainDocument, RoboNotSupported,
                                  "the generated document sheet does not fit the caller's buffer");
    }
    return RoboOk;
}
