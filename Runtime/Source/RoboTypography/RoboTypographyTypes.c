/*
 * RoboTypography's vocabulary and resolved-style helpers (Phase 12.3) — see
 * RoboTypographyTypes.h and RoboTypographyDiagnostics.h.
 *
 * Two things share this file and the pairing is deliberate: the *vocabulary* (a role's name, an
 * optical class, a weight's step) and the *record* those are written into. They are one concern --
 * "what a typography style is" -- and a second file would only be a second place for the name of a
 * role and the role's own enumerator to drift apart. The diagnostics record is here for the same
 * reason every module in this tree keeps its record beside its vocabulary: the failure a caller
 * reads names a *role*, an *optical class* or a *weight*, so the strings it prints are these.
 *
 * The metric door at the bottom is the one function here that touches RoboFont, and it is the seam
 * worth stating: `ascentPx`/`descentPx` are computed exactly as RoboLayout's measurer computes them
 * -- the face's layout metrics through `FontFaceLayoutMetrics`, each term scaled by
 * `FontScaleFontUnits` *separately* rather than summed in font units first. The reason is the one
 * `RoboLayoutTextFont.c` gives for the same choice: a caller placing a baseline needs the halves,
 * and a total would lose them.
 */

#include "RoboTypography/RoboTypographyDiagnostics.h"
#include "RoboTypography/RoboTypographyTypes.h"

#include "RoboFont/RoboFontMetrics.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

int RoboTypographyDomainIsKnown(TypographyDiagnosticDomain domain)
{
    return domain > RoboTypographyDomainNone &&
           domain < (TypographyDiagnosticDomain)RoboTypographyDomainCount;
}

const char *RoboTypographyDomainName(TypographyDiagnosticDomain domain)
{
    switch (domain) {
    case RoboTypographyDomainRole:
        return "role";
    case RoboTypographyDomainScale:
        return "scale";
    case RoboTypographyDomainResolver:
        return "resolver";
    case RoboTypographyDomainDocument:
        return "document";
    default:
        break;
    }
    return "unknown";
}

void RoboTypographyDiagReset(TypographyDiag *diag)
{
    if (!diag) {
        return;
    }
    diag->code = RoboOk;
    diag->domain = RoboTypographyDomainNone;
    diag->message = NULL;
    diag->file = NULL;
    diag->line = 0;
}

Result RoboTypographyDiagSet(TypographyDiag *diag,
                             Result code,
                             TypographyDiagnosticDomain domain,
                             const char *message,
                             const char *file,
                             int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    if (code == RoboOk) {
        RoboTypographyDiagReset(diag);
        return RoboOk;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message;
    diag->file = file;
    diag->line = line;
    return code;
}

Result RoboTypographyDiagRecord(TypographyDiag *diag,
                                Result code,
                                TypographyDiagnosticDomain domain,
                                const char *message,
                                const char *file,
                                int line)
{
    if (diag && code != RoboOk) {
        diag->code = code;
        diag->domain = domain;
        diag->message = message;
        diag->file = file;
        diag->line = line;
    }
    return code;
}

/* ------------------------------------------------------------------ */
/* Roles                                                               */
/* ------------------------------------------------------------------ */

static const char *const TypographyRoleNames[RoboTypographyRoleCount] = {
    "DesktopTitle",
    "DesktopSubtitle",
    "WindowTitle",
    "WindowSubtitle",
    "ChromeLabel",
    "ChromeButton",
    "ChromeMenu",
    "ChromeMenuItem",
    "ChromeTab",
    "ChromeAddress",
    "ChromeStatus",
    "DocumentBody",
    "DocumentHeading1",
    "DocumentHeading2",
    "DocumentHeading3",
    "DocumentCaption",
    "DocumentCode",
    "SystemLabel",
    "SystemSecondaryLabel",
    "SystemEmphasis",
    "SystemMonospace",
};

const char *RoboTypographyRoleName(RoboTypographyRole role)
{
    if (!RoboTypographyRoleIsValid(role)) {
        return NULL;
    }
    return TypographyRoleNames[(size_t)role - 1u];
}

int RoboTypographyRoleIsValid(RoboTypographyRole role)
{
    return role > RoboTypographyRoleNone &&
           role <= (RoboTypographyRole)RoboTypographyRoleCount;
}

/** One folded byte: upper-case ASCII lowered, separators dropped, everything else kept. */
static int RoleFoldChar(char c)
{
    if (c == '-' || c == '_' || c == ' ' || c == '.') {
        return 0;
    }
    if (c >= 'A' && c <= 'Z') {
        return (int)(c - 'A' + 'a');
    }
    return (int)(unsigned char)c;
}

/** Casefold equality between a slice and a NUL-terminated name, ignoring separators. */
static int RoleNameMatches(const char *text, size_t length, const char *name)
{
    size_t t = 0u;
    size_t n = 0u;

    if (!text || !name) {
        return 0;
    }
    for (;;) {
        int tc;
        int nc;

        while (t < length && RoleFoldChar(text[t]) == 0) {
            t++;
        }
        while (name[n] != '\0' && RoleFoldChar(name[n]) == 0) {
            n++;
        }
        tc = (t < length) ? RoleFoldChar(text[t]) : 0;
        nc = (name[n] != '\0') ? RoleFoldChar(name[n]) : 0;
        if (tc == 0 || nc == 0) {
            return tc == nc;
        }
        if (tc != nc) {
            return 0;
        }
        t++;
        n++;
    }
}

Result RoboTypographyRoleFromName(const char *name,
                                  size_t nameLen,
                                  RoboTypographyRole *out)
{
    size_t length;
    size_t index;

    if (!name || !out) {
        return RoboInvalidArgument;
    }
    length = (nameLen != 0u) ? nameLen : strlen(name);
    if (length == 0u) {
        return RoboNotFound;
    }
    for (index = 0u; index < RoboTypographyRoleCount; index++) {
        if (RoleNameMatches(name, length, TypographyRoleNames[index])) {
            *out = (RoboTypographyRole)(index + 1u);
            return RoboOk;
        }
    }
    return RoboNotFound;
}

int RoboTypographyRoleIsDesktop(RoboTypographyRole role)
{
    return role >= RoboTypographyRoleDesktopTitle &&
           role <= RoboTypographyRoleWindowSubtitle;
}

int RoboTypographyRoleIsChrome(RoboTypographyRole role)
{
    return role >= RoboTypographyRoleChromeLabel &&
           role <= RoboTypographyRoleChromeStatus;
}

int RoboTypographyRoleIsDocument(RoboTypographyRole role)
{
    return role >= RoboTypographyRoleDocumentBody &&
           role <= RoboTypographyRoleDocumentCode;
}

int RoboTypographyRoleIsSystem(RoboTypographyRole role)
{
    return role >= RoboTypographyRoleSystemLabel &&
           role <= RoboTypographyRoleSystemMonospace;
}

/* ------------------------------------------------------------------ */
/* Optical size                                                        */
/* ------------------------------------------------------------------ */

static const char *const TypographyOpticalNames[RoboTypographyOpticalSizeCount] = {
    "small",
    "body",
    "ui",
    "display",
    "large-display",
};

int RoboTypographyOpticalSizeIsValid(RoboTypographyOpticalSize size)
{
    return size >= RoboTypographyOpticalSmall &&
           size <= RoboTypographyOpticalLargeDisplay;
}

const char *RoboTypographyOpticalSizeName(RoboTypographyOpticalSize size)
{
    if (!RoboTypographyOpticalSizeIsValid(size)) {
        return "unknown";
    }
    return TypographyOpticalNames[(size_t)size];
}

Result RoboTypographyOpticalSizeFromName(const char *name,
                                         size_t nameLen,
                                         RoboTypographyOpticalSize *out)
{
    size_t length;
    size_t index;

    if (!name || !out) {
        return RoboInvalidArgument;
    }
    length = (nameLen != 0u) ? nameLen : strlen(name);
    if (length == 0u) {
        return RoboNotFound;
    }
    for (index = 0u; index < RoboTypographyOpticalSizeCount; index++) {
        if (RoleNameMatches(name, length, TypographyOpticalNames[index])) {
            *out = (RoboTypographyOpticalSize)index;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

/* ------------------------------------------------------------------ */
/* Weights                                                             */
/* ------------------------------------------------------------------ */

typedef struct TypographyWeightStep {
    const char *name;
    FontWeight weight;
} TypographyWeightStep;

static const TypographyWeightStep TypographyWeights[RoboTypographyWeightCount] = {
    {"regular", RoboTypographyWeightRegular},
    {"medium", RoboTypographyWeightMedium},
    {"semibold", RoboTypographyWeightSemiBold},
    {"bold", RoboTypographyWeightBold},
};

const char *RoboTypographyWeightName(FontWeight weight)
{
    size_t index;

    for (index = 0u; index < RoboTypographyWeightCount; index++) {
        if (TypographyWeights[index].weight == weight) {
            return TypographyWeights[index].name;
        }
    }
    return NULL;
}

Result RoboTypographyWeightFromName(const char *name,
                                    size_t nameLen,
                                    FontWeight *out)
{
    size_t length;
    size_t index;

    if (!name || !out) {
        return RoboInvalidArgument;
    }
    length = (nameLen != 0u) ? nameLen : strlen(name);
    if (length == 0u) {
        return RoboNotFound;
    }
    for (index = 0u; index < RoboTypographyWeightCount; index++) {
        if (RoleNameMatches(name, length, TypographyWeights[index].name)) {
            *out = TypographyWeights[index].weight;
            return RoboOk;
        }
    }
    /* `normal` and `bolder`/`lighter` are not steps of this vocabulary: a caller that means CSS's
     * keyword means the weight CSS resolves it to, and guessing would be inventing a step. */
    return RoboNotFound;
}

FontWeight RoboTypographyNearestWeight(FontWeight requested)
{
    size_t index;
    size_t best = 0u;
    uint32_t bestDistance = 0xFFFFFFFFu;

    if (!FontWeightIsValid(requested)) {
        return RoboTypographyWeightRegular;
    }
    for (index = 0u; index < RoboTypographyWeightCount; index++) {
        FontWeight candidate = TypographyWeights[index].weight;
        uint32_t distance = (candidate > requested) ? (uint32_t)(candidate - requested)
                                                    : (uint32_t)(requested - candidate);

        /* Strictly-less keeps the *first* (lighter) candidate on a tie, which is the documented
         * rule: the vocabulary is in increasing order, so the earlier entry is the lighter one. */
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return TypographyWeights[best].weight;
}

/* ------------------------------------------------------------------ */
/* The resolved style                                                  */
/* ------------------------------------------------------------------ */

void RoboTypographyStyleInit(RoboTypographyStyle *style)
{
    if (!style) {
        return;
    }
    memset(style, 0, sizeof(*style));
    style->role = RoboTypographyRoleNone;
    style->fontRole = RoboFontSystemRoleNone;
    style->opticalSize = RoboTypographyOpticalSmall;
}

int RoboTypographyStyleIsValid(const RoboTypographyStyle *style)
{
    if (!style) {
        return 0;
    }
    if (!RoboTypographyRoleIsValid(style->role)) {
        return 0;
    }
    if (!RoboTypographyOpticalSizeIsValid(style->opticalSize)) {
        return 0;
    }
    if (style->weight != 0u && !FontWeightIsValid(style->weight)) {
        return 0;
    }
    /* A size or a leading of zero is not a type setting: it is an unset field, and a caller that
     * passed one deserves a refusal at the seam rather than an invisible or overlapping line. */
    if (!(style->sizePx > 0.0f) || !(style->lineHeightPx > 0.0f)) {
        return 0;
    }
    if (!(style->ascentPx >= 0.0f) || !(style->descentPx >= 0.0f)) {
        return 0;
    }
    return 1;
}

int RoboTypographyStyleEqual(const RoboTypographyStyle *a,
                             const RoboTypographyStyle *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->role == b->role && a->fontRole == b->fontRole && a->face == b->face &&
           a->weight == b->weight && a->sizePx == b->sizePx &&
           a->lineHeightPx == b->lineHeightPx && a->opticalSize == b->opticalSize &&
           a->ascentPx == b->ascentPx && a->descentPx == b->descentPx;
}

/** Append one byte if it fits; returns 0 when it did not. */
static int StyleAppend(char *buffer,
                       size_t capacity,
                       size_t *cursor,
                       char c)
{
    if (*cursor + 1u >= capacity) {
        return 0;
    }
    buffer[*cursor] = c;
    (*cursor)++;
    buffer[*cursor] = '\0';
    return 1;
}

/** Append a NUL-terminated string; returns 0 when it did not fit whole. */
static int StyleAppendText(char *buffer,
                           size_t capacity,
                           size_t *cursor,
                           const char *text)
{
    size_t index = 0u;

    if (!text) {
        return 1;
    }
    while (text[index] != '\0') {
        if (!StyleAppend(buffer, capacity, cursor, text[index])) {
            return 0;
        }
        index++;
    }
    return 1;
}

/** Append an unsigned decimal. */
static int StyleAppendUint(char *buffer,
                           size_t capacity,
                           size_t *cursor,
                           uint32_t value)
{
    char digits[16];
    size_t count = 0u;

    if (value == 0u) {
        return StyleAppend(buffer, capacity, cursor, '0');
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u) {
        if (!StyleAppend(buffer, capacity, cursor, digits[--count])) {
            return 0;
        }
    }
    return 1;
}

/** Append a non-negative float with one decimal, rounded to nearest. */
static int StyleAppendFixed1(char *buffer,
                             size_t capacity,
                             size_t *cursor,
                             float value)
{
    uint32_t scaled;
    uint32_t whole;
    uint32_t fraction;

    if (!(value >= 0.0f)) {
        return StyleAppend(buffer, capacity, cursor, '?');
    }
    scaled = (uint32_t)(value * 10.0f + 0.5f);
    whole = scaled / 10u;
    fraction = scaled % 10u;
    if (!StyleAppendUint(buffer, capacity, cursor, whole)) {
        return 0;
    }
    if (!StyleAppend(buffer, capacity, cursor, '.')) {
        return 0;
    }
    return StyleAppendUint(buffer, capacity, cursor, fraction);
}

/** Room for the longest line this function can build, with margin. */
#define RoboTypographyStyleLineMax 192u

/*
 * The line is built into a **local scratch first**, and the scratch is why the length a caller is
 * told is exact: a buffer too small is refused with the length the text *needs* rather than with the
 * length that fitted, so "how big should my buffer be?" has one honest answer. Building straight
 * into the caller's buffer could only report the truncated length.
 */
Result RoboTypographyStyleWrite(const RoboTypographyStyle *style,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength)
{
    char scratch[RoboTypographyStyleLineMax];
    size_t cursor = 0u;
    const char *role;
    const char *weight;

    if (!style || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    scratch[0] = '\0';
    role = RoboTypographyRoleName(style->role);
    weight = RoboTypographyWeightName(style->weight);
    if (!StyleAppendText(scratch, sizeof(scratch), &cursor, role ? role : "None") ||
        !StyleAppendText(scratch, sizeof(scratch), &cursor, " weight=") ||
        !StyleAppendText(scratch, sizeof(scratch), &cursor, weight ? weight : "-") ||
        !StyleAppendText(scratch, sizeof(scratch), &cursor, " size=") ||
        !StyleAppendFixed1(scratch, sizeof(scratch), &cursor, style->sizePx) ||
        !StyleAppendText(scratch, sizeof(scratch), &cursor, " line=") ||
        !StyleAppendFixed1(scratch, sizeof(scratch), &cursor, style->lineHeightPx) ||
        !StyleAppendText(scratch, sizeof(scratch), &cursor, " optical=") ||
        !StyleAppendText(scratch, sizeof(scratch), &cursor,
                         RoboTypographyOpticalSizeName(style->opticalSize)) ||
        !StyleAppendText(scratch, sizeof(scratch), &cursor, " ascent=") ||
        !StyleAppendFixed1(scratch, sizeof(scratch), &cursor, style->ascentPx) ||
        !StyleAppendText(scratch, sizeof(scratch), &cursor, " face=") ||
        !StyleAppendText(scratch, sizeof(scratch), &cursor, style->face ? "1" : "0")) {
        /* Unreachable: the scratch is sized for the longest line this function can build. Reported
         * rather than trusted, because a future field appended without widening the scratch would
         * otherwise produce a silently truncated dump. */
        if (outLength) {
            *outLength = 0u;
        }
        return RoboNotSupported;
    }
    if (outLength) {
        *outLength = cursor;
    }
    if (cursor + 1u > capacity) {
        return RoboNotSupported;
    }
    memcpy(buffer, scratch, cursor + 1u);
    return RoboOk;
}

Result RoboTypographyStyleApplyFontMetrics(RoboTypographyStyle *style)
{
    FontMetrics line;
    float ascent;
    float descent;

    if (!style) {
        return RoboInvalidArgument;
    }
    style->ascentPx = 0.0f;
    style->descentPx = 0.0f;
    if (!style->face) {
        /* No face: the size is the honest line extent, exactly as RoboLayout's measurer reads an
         * unknown face -- and never zero, which would collapse a line box to nothing. */
        style->ascentPx = style->sizePx;
        return RoboOk;
    }

    memset(&line, 0, sizeof(line));
    if (FontFaceLayoutMetrics(style->face, &line) != RoboOk ||
        !FontUnitsPerEmIsValid(line.unitsPerEm)) {
        style->ascentPx = style->sizePx;
        return RoboOk;
    }

    ascent = (float)style->sizePx;
    if (FontScaleFontUnits(line.ascent, line.unitsPerEm,
                           (FontPixelSize)style->sizePx, RoboFontRoundNearest,
                           &ascent) != RoboOk) {
        ascent = (float)style->sizePx;
    }
    descent = 0.0f;
    (void)FontScaleFontUnits(line.descent, line.unitsPerEm,
                             (FontPixelSize)style->sizePx, RoboFontRoundNearest,
                             &descent);
    /* A face that authored its descent positive is reported by RoboFont as a magnitude, but a face
     * in the wild may carry either sign. Normalising here keeps the field's stated meaning true, and
     * keeps a caller's baseline arithmetic from depending on which table it read. */
    if (descent < 0.0f) {
        descent = -descent;
    }
    if (ascent < 0.0f) {
        ascent = 0.0f;
    }
    style->ascentPx = ascent;
    style->descentPx = descent;
    return RoboOk;
}
