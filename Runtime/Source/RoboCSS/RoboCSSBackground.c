/*
 * RoboCSS background vocabulary (Phase 9.14) — see RoboCSSBackground.h.
 *
 * Three tables and one classifier, and everything below reads them:
 *
 *   the repeat rows       one per value, carrying the two axes so `repeat-x` is
 *                         data rather than a fifth branch
 *   the attachment rows   one per value
 *   the longhand table    the six properties `background` stands for, in the order
 *                         a serialisation writes them
 *
 * The classifier is the piece that makes this a different machine from the box
 * model's positional mapping: a box shorthand's components map to sides *by
 * position*, and a background shorthand's components map to longhands *by kind*.
 * Nothing here weighs a level or compares a specificity — which of a shorthand and
 * a longhand wins is the cascade's answer, asked during resolution, not this
 * module's.
 *
 * The one place a spelling appears is a table row. Every predicate below is a
 * lookup, so a value added later answers the questions that mention it without a
 * function being touched.
 */

#include "RoboCSS/RoboCSSBackground.h"

#include "RoboCSS/RoboCSSColor.h"
#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The repeat vocabulary                                               */
/* ------------------------------------------------------------------ */

/*
 * `horizontal` and `vertical` are the value, not a description of it: `repeat-x`
 * *is* "tiles horizontally, does not tile vertically". Asked as two columns
 * because they are two questions — a caller that wants to know about one axis
 * should not have to know which of four values implies it.
 */
typedef struct backgroundRepeatRow {
    CssBackgroundRepeat value;
    const char *keyword;
    int horizontal;
    int vertical;
} BackgroundRepeatRow;

static const BackgroundRepeatRow backgroundRepeatVocabulary[] = {
    { RoboCssBackgroundRepeat,     "repeat",    1, 1 },
    { RoboCssBackgroundRepeatX,   "repeat-x",  1, 0 },
    { RoboCssBackgroundRepeatY,   "repeat-y",  0, 1 },
    { RoboCssBackgroundNoRepeat,  "no-repeat", 0, 0 }
};

#define BACKGROUND_REPEAT_VOCABULARY_COUNT \
    (sizeof(backgroundRepeatVocabulary) / sizeof(backgroundRepeatVocabulary[0]))

/** The row for @p repeat, or NULL when it is not one of them. */
static const BackgroundRepeatRow *BackgroundRepeatRowOf(
    CssBackgroundRepeat repeat)
{
    size_t index;

    for (index = 0u; index < BACKGROUND_REPEAT_VOCABULARY_COUNT; index++) {
        if (backgroundRepeatVocabulary[index].value == repeat) {
            return &backgroundRepeatVocabulary[index];
        }
    }
    return NULL;
}

int CssBackgroundRepeatIsKnown(CssBackgroundRepeat repeat)
{
    return BackgroundRepeatRowOf(repeat) != NULL;
}

const char *CssBackgroundRepeatName(CssBackgroundRepeat repeat)
{
    const BackgroundRepeatRow *row = BackgroundRepeatRowOf(repeat);

    return row ? row->keyword : "";
}

size_t CssBackgroundRepeatCount(void)
{
    return BACKGROUND_REPEAT_VOCABULARY_COUNT;
}

CssBackgroundRepeat CssBackgroundRepeatFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssBackgroundRepeat;
    }
    for (index = 0u; index < BACKGROUND_REPEAT_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                          backgroundRepeatVocabulary[index].keyword)) {
            return backgroundRepeatVocabulary[index].value;
        }
    }
    /* Not one of them. `repeat` is the answer a caller reads *after* asking
     * CssBackgroundRepeatSliceIsKnown — see the header. */
    return RoboCssBackgroundRepeat;
}

CssBackgroundRepeat CssBackgroundRepeatFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssBackgroundRepeat;
    }
    return CssBackgroundRepeatFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssBackgroundRepeatSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < BACKGROUND_REPEAT_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                          backgroundRepeatVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssBackgroundRepeatWrite(CssBackgroundRepeat repeat,
                                               char *buffer,
                                               size_t capacity,
                                               size_t *outLength,
                                               CssDiag *diag)
{
    const BackgroundRepeatRow *row;
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    row = BackgroundRepeatRowOf(repeat);
    if (!row) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "not a background repeat value");
    }
    length = strlen(row->keyword);
    if (length >= capacity) {
        /* The length it would have needed, so a caller can size a buffer from the
         * refusal rather than from a constant it has to keep in step. */
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                             "the repeat keyword does not fit");
    }
    memcpy(buffer, row->keyword, length + 1u);
    *outLength = length;
    return RoboOk;
}

int CssBackgroundRepeatRepeatsOnAxis(CssBackgroundRepeat repeat,
                                               CssBackgroundAxis axis)
{
    const BackgroundRepeatRow *row = BackgroundRepeatRowOf(repeat);

    if (!row) {
        /* Not the first row's answer: a value nothing defined does not tile
         * anything, and claiming it tiles both axes would be inventing one. */
        return 0;
    }
    if (axis == RoboCssBackgroundAxisHorizontal) {
        return row->horizontal;
    }
    if (axis == RoboCssBackgroundAxisVertical) {
        return row->vertical;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The axis                                                            */
/* ------------------------------------------------------------------ */

int CssBackgroundAxisIsKnown(CssBackgroundAxis axis)
{
    return axis == RoboCssBackgroundAxisHorizontal ||
           axis == RoboCssBackgroundAxisVertical;
}

const char *CssBackgroundAxisName(CssBackgroundAxis axis)
{
    switch (axis) {
    case RoboCssBackgroundAxisHorizontal:
        return "horizontal";
    case RoboCssBackgroundAxisVertical:
        return "vertical";
    default:
        return "";
    }
}

/* ------------------------------------------------------------------ */
/* The attachment vocabulary                                           */
/* ------------------------------------------------------------------ */

typedef struct backgroundAttachmentRow {
    CssBackgroundAttachment value;
    const char *keyword;
} BackgroundAttachmentRow;

static const BackgroundAttachmentRow backgroundAttachmentVocabulary[] = {
    { RoboCssBackgroundAttachmentScroll, "scroll" },
    { RoboCssBackgroundAttachmentFixed,  "fixed" }
};

#define BACKGROUND_ATTACHMENT_VOCABULARY_COUNT \
    (sizeof(backgroundAttachmentVocabulary) / sizeof(backgroundAttachmentVocabulary[0]))

static const BackgroundAttachmentRow *BackgroundAttachmentRowOf(
    CssBackgroundAttachment attachment)
{
    size_t index;

    for (index = 0u; index < BACKGROUND_ATTACHMENT_VOCABULARY_COUNT; index++) {
        if (backgroundAttachmentVocabulary[index].value == attachment) {
            return &backgroundAttachmentVocabulary[index];
        }
    }
    return NULL;
}

int CssBackgroundAttachmentIsKnown(CssBackgroundAttachment attachment)
{
    return BackgroundAttachmentRowOf(attachment) != NULL;
}

const char *CssBackgroundAttachmentName(CssBackgroundAttachment attachment)
{
    const BackgroundAttachmentRow *row = BackgroundAttachmentRowOf(attachment);

    return row ? row->keyword : "";
}

size_t CssBackgroundAttachmentCount(void)
{
    return BACKGROUND_ATTACHMENT_VOCABULARY_COUNT;
}

CssBackgroundAttachment CssBackgroundAttachmentFromSlice(
    CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssBackgroundAttachmentScroll;
    }
    for (index = 0u; index < BACKGROUND_ATTACHMENT_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                          backgroundAttachmentVocabulary[index].keyword)) {
            return backgroundAttachmentVocabulary[index].value;
        }
    }
    return RoboCssBackgroundAttachmentScroll;
}

CssBackgroundAttachment CssBackgroundAttachmentFromText(
    const char *keyword)
{
    if (!keyword) {
        return RoboCssBackgroundAttachmentScroll;
    }
    return CssBackgroundAttachmentFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssBackgroundAttachmentSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < BACKGROUND_ATTACHMENT_VOCABULARY_COUNT; index++) {
        if (CssBytesEqualAsciiCi(keyword.bytes, keyword.length,
                                          backgroundAttachmentVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

Result CssBackgroundAttachmentWrite(CssBackgroundAttachment attachment,
                                                   char *buffer,
                                                   size_t capacity,
                                                   size_t *outLength,
                                                   CssDiag *diag)
{
    const BackgroundAttachmentRow *row;
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    row = BackgroundAttachmentRowOf(attachment);
    if (!row) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "not a background attachment value");
    }
    length = strlen(row->keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                             "the attachment keyword does not fit");
    }
    memcpy(buffer, row->keyword, length + 1u);
    *outLength = length;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The edge                                                            */
/* ------------------------------------------------------------------ */

/*
 * One table, two axes: the axis picks the *word*, so `START` is `left`
 * horizontally and `top` vertically. Spelling is looked up rather than built,
 * because a caller printing `left bottom` must get `bottom` and not a word
 * assembled out of the axis's name.
 */
static const char *const backgroundEdgeNames[RoboCssBackgroundAxisCount]
                                             [RoboCssBackgroundEdgeCount] = {
    /* horizontal */ { "center", "left", "right"  },
    /* vertical   */ { "center", "top",  "bottom" }
};

int CssBackgroundEdgeIsKnown(CssBackgroundEdge edge)
{
    return edge == RoboCssBackgroundEdgeCenter ||
           edge == RoboCssBackgroundEdgeStart ||
           edge == RoboCssBackgroundEdgeEnd;
}

const char *CssBackgroundEdgeName(CssBackgroundEdge edge,
                                          CssBackgroundAxis axis)
{
    if (!CssBackgroundEdgeIsKnown(edge) ||
        !CssBackgroundAxisIsKnown(axis)) {
        return "";
    }
    return backgroundEdgeNames[(size_t)axis][(size_t)edge];
}

CssBackgroundEdge CssBackgroundEdgeFromSlice(CssSlice keyword,
                                                               CssBackgroundAxis axis)
{
    CssBackgroundEdge edge;

    if (!CssBackgroundAxisIsKnown(axis)) {
        return RoboCssBackgroundEdgeCenter;
    }
    for (edge = (CssBackgroundEdge)0;
         (int)edge < (int)RoboCssBackgroundEdgeCount;
         edge = (CssBackgroundEdge)((int)edge + 1)) {
        if (CssBytesEqualAsciiCi(
                keyword.bytes, keyword.length,
                backgroundEdgeNames[(size_t)axis][(size_t)edge])) {
            return edge;
        }
    }
    /* Not an edge on *this* axis — a different fact from "not an edge", and why
     * the caller asks the `_is_known` door first. */
    return RoboCssBackgroundEdgeCenter;
}

int CssBackgroundEdgeSliceIsKnown(CssSlice keyword,
                                            CssBackgroundAxis axis)
{
    CssBackgroundEdge edge;

    if (!CssBackgroundAxisIsKnown(axis) ||
        CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (edge = (CssBackgroundEdge)0;
         (int)edge < (int)RoboCssBackgroundEdgeCount;
         edge = (CssBackgroundEdge)((int)edge + 1)) {
        if (CssBytesEqualAsciiCi(
                keyword.bytes, keyword.length,
                backgroundEdgeNames[(size_t)axis][(size_t)edge])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* A position component                                                */
/* ------------------------------------------------------------------ */

int CssBackgroundComponentKindIsKnown(CssBackgroundComponentKind kind)
{
    return kind == RoboCssBackgroundComponentNone ||
           kind == RoboCssBackgroundComponentEdge ||
           kind == RoboCssBackgroundComponentLength ||
           kind == RoboCssBackgroundComponentPercentage;
}

const char *CssBackgroundComponentKindName(CssBackgroundComponentKind kind)
{
    switch (kind) {
    case RoboCssBackgroundComponentNone:
        return "none";
    case RoboCssBackgroundComponentEdge:
        return "edge";
    case RoboCssBackgroundComponentLength:
        return "length";
    case RoboCssBackgroundComponentPercentage:
        return "percentage";
    default:
        return "unknown";
    }
}

CssBackgroundComponent CssBackgroundComponentNone(void)
{
    CssBackgroundComponent component;

    memset(&component, 0, sizeof(component));
    component.kind = RoboCssBackgroundComponentNone;
    component.edge = RoboCssBackgroundEdgeCenter;
    component.numeric = CssNumericNone();
    return component;
}

int CssBackgroundComponentIsPresent(CssBackgroundComponent component)
{
    return component.kind != RoboCssBackgroundComponentNone;
}

int CssBackgroundComponentEquals(CssBackgroundComponent a,
                                         CssBackgroundComponent b)
{
    /* Two absent components are *not* equal: "no component" is not a value, and a
     * caller that wanted "neither is set" has is_present for that question. */
    if (!CssBackgroundComponentIsPresent(a) ||
        !CssBackgroundComponentIsPresent(b)) {
        return 0;
    }
    if (a.kind != b.kind) {
        /* Kind-aware, so `left` and `0px` are different however equal they look
         * once something resolves them, and a length never equals a percentage
         * even at the same magnitude. */
        return 0;
    }
    if (a.kind == RoboCssBackgroundComponentEdge) {
        return a.edge == b.edge;
    }
    return CssNumericEquals(a.numeric, b.numeric);
}

Result CssBackgroundComponentWrite(CssBackgroundComponent component,
                                                  CssBackgroundAxis axis,
                                                  char *buffer,
                                                  size_t capacity,
                                                  size_t *outLength,
                                                  CssDiag *diag)
{
    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!CssBackgroundComponentIsPresent(component)) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "there is no component to write");
    }
    if (component.kind == RoboCssBackgroundComponentEdge) {
        const char *keyword;
        size_t length;

        if (!CssBackgroundAxisIsKnown(axis)) {
            return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                                 "an edge is spelled by its axis");
        }
        keyword = CssBackgroundEdgeName(component.edge, axis);
        length = strlen(keyword);
        if (length == 0u) {
            return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                                 "not an edge on that axis");
        }
        if (length >= capacity) {
            *outLength = length;
            return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                                 "the edge keyword does not fit");
        }
        memcpy(buffer, keyword, length + 1u);
        *outLength = length;
        return RoboOk;
    }
    /* A length or a percentage: one canonical spelling, shared with every other
     * numeric value in this subsystem. */
    return CssNumericWrite(component.numeric, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* The position pair                                                   */
/* ------------------------------------------------------------------ */

CssBackgroundPosition CssBackgroundPositionNone(void)
{
    CssBackgroundPosition position;

    position.horizontal = CssBackgroundComponentNone();
    position.vertical = CssBackgroundComponentNone();
    return position;
}

int CssBackgroundPositionIsPresent(CssBackgroundPosition position)
{
    /* Both halves, because both halves are what the value *is*: a half-filled
     * position is a state this module holds only while parsing, and a caller that
     * could hold one would have to remember which half was missing. */
    return CssBackgroundComponentIsPresent(position.horizontal) &&
           CssBackgroundComponentIsPresent(position.vertical);
}

int CssBackgroundPositionEquals(CssBackgroundPosition a,
                                        CssBackgroundPosition b)
{
    if (!CssBackgroundPositionIsPresent(a) ||
        !CssBackgroundPositionIsPresent(b)) {
        return 0;
    }
    return CssBackgroundComponentEquals(a.horizontal, b.horizontal) &&
           CssBackgroundComponentEquals(a.vertical, b.vertical);
}

Result CssBackgroundPositionWrite(CssBackgroundPosition position,
                                                 char *buffer,
                                                 size_t capacity,
                                                 size_t *outLength,
                                                 CssDiag *diag)
{
    char horizontal[RoboCssNumericTextMax];
    char vertical[RoboCssNumericTextMax];
    size_t horizontalLength = 0u;
    size_t verticalLength = 0u;
    Result result;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!CssBackgroundPositionIsPresent(position)) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "there is no position to write");
    }
    result = CssBackgroundComponentWrite(
        position.horizontal, RoboCssBackgroundAxisHorizontal, horizontal,
        sizeof(horizontal), &horizontalLength, diag);
    if (result != RoboOk) {
        return result;
    }
    result = CssBackgroundComponentWrite(
        position.vertical, RoboCssBackgroundAxisVertical, vertical,
        sizeof(vertical), &verticalLength, diag);
    if (result != RoboOk) {
        return result;
    }
    /* Both components, always, separated by one space: the computed value's own
     * spelling whatever the declaration looked like — so `center` prints
     * `center center`, and two equal positions cannot print differently. */
    if (horizontalLength + 1u + verticalLength >= capacity) {
        *outLength = horizontalLength + 1u + verticalLength;
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                             "the position does not fit");
    }
    memcpy(buffer, horizontal, horizontalLength);
    buffer[horizontalLength] = ' ';
    memcpy(buffer + horizontalLength + 1u, vertical, verticalLength + 1u);
    *outLength = horizontalLength + 1u + verticalLength;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The size pair                                                       */
/* ------------------------------------------------------------------ */

int CssBackgroundSizeKindIsKnown(CssBackgroundSizeKind kind)
{
    return kind == RoboCssBackgroundSizeAuto ||
           kind == RoboCssBackgroundSizeCover ||
           kind == RoboCssBackgroundSizeContain ||
           kind == RoboCssBackgroundSizePair;
}

const char *CssBackgroundSizeKindName(CssBackgroundSizeKind kind)
{
    switch (kind) {
    case RoboCssBackgroundSizeAuto:
        return "auto";
    case RoboCssBackgroundSizeCover:
        return "cover";
    case RoboCssBackgroundSizeContain:
        return "contain";
    case RoboCssBackgroundSizePair:
        return "pair";
    default:
        return "unknown";
    }
}

CssBackgroundSize CssBackgroundSizeNone(void)
{
    CssBackgroundSize size;

    memset(&size, 0, sizeof(size));
    size.kind = RoboCssBackgroundSizeAuto;
    size.width.isAuto = 0;
    size.width.numeric = CssNumericNone();
    size.height.isAuto = 0;
    size.height.numeric = CssNumericNone();
    return size;
}

int CssBackgroundSizeIsPresent(CssBackgroundSize size)
{
    /* A cleared record is indistinguishable from `auto` — the initial value — so
     * a caller reads this through the record that holds it, exactly as it does
     * for the display and positioning vocabularies. What this asks is that the
     * value is *coherent*: a whole-value keyword, or a pair with a real
     * component in at least one half. */
    if (!CssBackgroundSizeKindIsKnown(size.kind)) {
        return 0;
    }
    if (size.kind != RoboCssBackgroundSizePair) {
        return 1;
    }
    return size.width.isAuto || CssNumericIsPresent(size.width.numeric) ||
           size.height.isAuto || CssNumericIsPresent(size.height.numeric);
}

int CssBackgroundSizeEquals(CssBackgroundSize a,
                                    CssBackgroundSize b)
{
    if (!CssBackgroundSizeIsPresent(a) ||
        !CssBackgroundSizeIsPresent(b)) {
        return 0;
    }
    if (a.kind != b.kind) {
        return 0;
    }
    if (a.kind != RoboCssBackgroundSizePair) {
        return 1;
    }
    /* Component-wise, and `auto` is a flag rather than a numeric kind — so
     * `20px auto` differs from `20px 0px`, which is the distinction the pair
     * exists to keep. */
    if (a.width.isAuto != b.width.isAuto ||
        a.height.isAuto != b.height.isAuto) {
        return 0;
    }
    if (!a.width.isAuto && !CssNumericEquals(a.width.numeric, b.width.numeric)) {
        return 0;
    }
    if (!a.height.isAuto && !CssNumericEquals(a.height.numeric, b.height.numeric)) {
        return 0;
    }
    return 1;
}

Result CssBackgroundSizeWrite(CssBackgroundSize size,
                                             char *buffer,
                                             size_t capacity,
                                             size_t *outLength,
                                             CssDiag *diag)
{
    char width[RoboCssNumericTextMax];
    char height[RoboCssNumericTextMax];
    size_t widthLength = 0u;
    size_t heightLength = 0u;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!CssBackgroundSizeIsPresent(size)) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "there is no size to write");
    }
    if (size.kind != RoboCssBackgroundSizePair) {
        const char *keyword = CssBackgroundSizeKindName(size.kind);
        size_t keywordLength = strlen(keyword);

        if (keywordLength >= capacity) {
            *outLength = keywordLength;
            return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                                 "the size keyword does not fit");
        }
        memcpy(buffer, keyword, keywordLength + 1u);
        *outLength = keywordLength;
        return RoboOk;
    }
    if (size.width.isAuto) {
        memcpy(width, "auto", 5u);
        widthLength = 4u;
    } else {
        Result result = CssNumericWrite(size.width.numeric, width,
                                                      sizeof(width), &widthLength, diag);

        if (result != RoboOk) {
            return result;
        }
    }
    if (size.height.isAuto) {
        memcpy(height, "auto", 5u);
        heightLength = 4u;
    } else {
        Result result = CssNumericWrite(size.height.numeric, height,
                                                      sizeof(height), &heightLength, diag);

        if (result != RoboOk) {
            return result;
        }
    }
    if (widthLength + 1u + heightLength >= capacity) {
        *outLength = widthLength + 1u + heightLength;
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                             "the size does not fit");
    }
    memcpy(buffer, width, widthLength);
    buffer[widthLength] = ' ';
    memcpy(buffer + widthLength + 1u, height, heightLength + 1u);
    *outLength = widthLength + 1u + heightLength;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Reading components out of a value                                   */
/* ------------------------------------------------------------------ */

/** The @p wanted-th significant component of @p value, or NULL. */
static const CssValue *BackgroundComponentAt(const CssValue *value,
                                                       size_t wanted)
{
    size_t seen = 0u;
    size_t index;

    if (!value || value->kind == RoboCssValueKindNone) {
        return NULL;
    }
    if (value->kind != RoboCssValueKindList) {
        return wanted == 0u && !CssValueIsTrivia(value) ? value : NULL;
    }
    for (index = 0u; index < value->children.count; index++) {
        const CssValue *child = &value->children.items[index];

        if (CssValueIsTrivia(child)) {
            continue;
        }
        if (seen == wanted) {
            return child;
        }
        seen++;
    }
    return NULL;
}

/** Number of significant components in @p value. */
static size_t BackgroundComponentCount(const CssValue *value)
{
    size_t count = 0u;
    size_t index;

    if (!value || value->kind == RoboCssValueKindNone) {
        return 0u;
    }
    if (value->kind != RoboCssValueKindList) {
        return CssValueIsTrivia(value) ? 0u : 1u;
    }
    for (index = 0u; index < value->children.count; index++) {
        if (!CssValueIsTrivia(&value->children.items[index])) {
            count++;
        }
    }
    return count;
}

/** The lowercased identifier @p value spells, or 0 when it is not an identifier. */
static int BackgroundIdentifier(const CssValue *value,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength)
{
    CssToken token;

    if (!value || value->kind != RoboCssValueKindToken) {
        return 0;
    }
    token = CssValueToken(value);
    if (token.type != RoboCssTokenIdent) {
        return 0;
    }
    return CssTokenKeywordText(&token, buffer, capacity, outLength);
}

/** Non-zero when @p value is a function whose substitution could change a count. */
static int BackgroundComponentIsSubstitution(const CssValue *value)
{
    return CssValueFunctionNamed(value, "var") ||
           CssValueFunctionNamed(value, "env");
}

/** Non-zero when @p value is the `/` delimiter. */
static int BackgroundComponentIsSlash(const CssValue *value)
{
    CssSlice text;

    if (!value || value->kind != RoboCssValueKindToken) {
        return 0;
    }
    if (CssValueToken(value).type != RoboCssTokenDelim) {
        return 0;
    }
    text = CssValueTokenText(value);
    return text.length == 1u && text.bytes && text.bytes[0] == (uint8_t)'/';
}

/**
 * Whether @p keyword is `auto`, ASCII case-insensitively.
 *
 * Spelled once, because the size grammar asks it in three places (a whole-value
 * size, a component after a `/`, and a component of a pair) and three comparisons
 * are three chances for one of them to be case-sensitive.
 */
static int BackgroundKeywordIs(const char *keyword, size_t length, const char *wanted)
{
    return CssBytesEqualAsciiCi((const uint8_t *)keyword, length, wanted);
}

/**
 * What one component of a position or size value turned out to be.
 *
 * `namedAxis` is the field the position grammar turns on, and it is why this is a
 * record rather than a return code: a component that is an edge keyword *knows
 * which axis it belongs to*, and one that is `center` or an offset does not. That
 * is the whole difference between `left top` (one edge per axis, in either order)
 * and `left 10px` (the edge-relative form, which this build refuses) — and it
 * cannot be recovered from the component alone, because `left` and `top` are both
 * the START edge.
 */
typedef struct backgroundReading {
    CssBackgroundComponent component;
    /** -1 when the component fits either axis; 0 horizontal; 1 vertical. */
    int namedAxis;
    int isAuto;
} BackgroundReading;

/**
 * @p numeric as the one spelling an absolute length has.
 *
 * `1in` and `96px` are the same length, so the pair holds one of them and it is
 * the reference pixel — the same conversion, by the same table, a length-shaped
 * property's computed value goes through (RoboCSSComputedValue.c). It is here
 * rather than at the write, because a value has one spelling and the write is not
 * the only reader: whoever measures a box reads the pair's numeric directly.
 *
 * Whether a unit converts at all is the unit module's answer
 * (`CssUnitIsAbsolute`) rather than a list restated here, so a unit added
 * there converts here the day it is added. A *relative* length keeps its unit and
 * its number — `2em` is not a length until something says what the font size is —
 * and a percentage is not a length at all.
 */
static CssNumeric BackgroundCanonicalLength(CssNumeric numeric)
{
    if (CssNumericIsPresent(numeric) &&
        numeric.kind == RoboCssNumericLength &&
        CssUnitIsAbsolute(numeric.unit)) {
        numeric.number = CssUnitToPx(numeric.number, numeric.unit);
        numeric.unit = RoboCssUnitPx;
    }
    return numeric;
}

/**
 * Read @p value as one position-or-size component.
 *
 * One reader for both grammars, because the four questions it asks — is this an
 * edge, is this a length, is this a percentage, is this `auto` — are the same
 * questions both ask, and a second copy is how `20px` comes to be a length for one
 * property and something else for the other. What differs between the two is which
 * of the answers each *accepts*, and that is the caller's decision.
 *
 * @param allowAuto whether `auto` is a component here. A position has no `auto`;
 *                   a size's components do.
 * @return one of @ref CssBackgroundParse, and the reading in @p out.
 */
static CssBackgroundParse BackgroundReadComponent(
    const CssValue *value,
    int allowAuto,
    BackgroundReading *out)
{
    CssNumeric numeric = CssNumericNone();
    CssNumericParse numericOutcome;
    char keyword[64];
    size_t keywordLength = 0u;
    CssSlice text;

    memset(out, 0, sizeof(*out));
    out->component = CssBackgroundComponentNone();
    out->namedAxis = -1;

    if (BackgroundIdentifier(value, keyword, sizeof(keyword), &keywordLength)) {
        text = CssSliceFromBytes((const uint8_t *)keyword, keywordLength);
        if (allowAuto && BackgroundKeywordIs(keyword, keywordLength, "auto")) {
            out->isAuto = 1;
            return RoboCssBackgroundParseSize;
        }
        if (CssBackgroundEdgeSliceIsKnown(
                text, RoboCssBackgroundAxisHorizontal)) {
            out->component.kind = RoboCssBackgroundComponentEdge;
            out->component.edge = CssBackgroundEdgeFromSlice(
                text, RoboCssBackgroundAxisHorizontal);
            /* `center` is the one edge that fits both axes, and it is the whole
             * reason this field exists. */
            out->namedAxis = out->component.edge == RoboCssBackgroundEdgeCenter
                                  ? -1 : 0;
            return RoboCssBackgroundParsePosition;
        }
        if (CssBackgroundEdgeSliceIsKnown(
                text, RoboCssBackgroundAxisVertical)) {
            out->component.kind = RoboCssBackgroundComponentEdge;
            out->component.edge = CssBackgroundEdgeFromSlice(
                text, RoboCssBackgroundAxisVertical);
            out->namedAxis = out->component.edge == RoboCssBackgroundEdgeCenter
                                  ? -1 : 1;
            return RoboCssBackgroundParsePosition;
        }
        /* An identifier, and neither an edge nor `auto`: `banana`, `lefty`. */
        return RoboCssBackgroundParseInvalid;
    }

    numericOutcome = CssNumericFromValue(value, &numeric);
    if (numericOutcome == RoboCssNumericParseDeferred) {
        /* `calc()`, `var()`, `min()`…: legal CSS this build does not evaluate. */
        return RoboCssBackgroundParseDeferred;
    }
    if (numericOutcome != RoboCssNumericParseResolved) {
        return RoboCssBackgroundParseInvalid;
    }
    if (numeric.kind == RoboCssNumericLength) {
        out->component.kind = RoboCssBackgroundComponentLength;
        out->component.edge = RoboCssBackgroundEdgeCenter;
        out->component.numeric = BackgroundCanonicalLength(numeric);
        return RoboCssBackgroundParsePosition;
    }
    if (numeric.kind == RoboCssNumericPercentage) {
        out->component.kind = RoboCssBackgroundComponentPercentage;
        out->component.edge = RoboCssBackgroundEdgeCenter;
        out->component.numeric = numeric;
        return RoboCssBackgroundParsePosition;
    }
    /* A bare number. It is a number, and it is not a position: CSS's
     * `<length-percentage>` has no unitless branch, so `background-position: 10`
     * is a mistake rather than ten pixels. */
    return RoboCssBackgroundParseInvalid;
}

/** The CENTER edge on @p axis, as a component. */
static CssBackgroundComponent BackgroundCenterComponent(void)
{
    CssBackgroundComponent component = CssBackgroundComponentNone();

    component.kind = RoboCssBackgroundComponentEdge;
    component.edge = RoboCssBackgroundEdgeCenter;
    return component;
}

/* ------------------------------------------------------------------ */
/* The parse outcomes                                                  */
/* ------------------------------------------------------------------ */

/*
 * Four rows, and the third is the one that keeps this phase honest: DEFERRED is
 * "legal CSS this build has no representation for" and INVALID is "this means
 * nothing here at all". A diagnostic is addressed to an author, and an author who
 * wrote `background-position: var(--p)` needs a different sentence from one who
 * wrote `background-position: banana`.
 */
static const char *const backgroundParseNames[RoboCssBackgroundParseCount] = {
    "invalid", "position", "size", "deferred"
};

int CssBackgroundParseIsKnown(CssBackgroundParse outcome)
{
    return outcome >= RoboCssBackgroundParseInvalid &&
           (size_t)outcome < (size_t)RoboCssBackgroundParseCount;
}

const char *CssBackgroundParseName(CssBackgroundParse outcome)
{
    return CssBackgroundParseIsKnown(outcome)
               ? backgroundParseNames[(size_t)outcome]
               : "unknown";
}

/* ------------------------------------------------------------------ */
/* Parsing a position                                                  */
/* ------------------------------------------------------------------ */

CssBackgroundParse CssBackgroundPositionFromValue(
    const CssValue *specified,
    CssBackgroundPosition *out)
{
    BackgroundReading firstReading;
    BackgroundReading secondReading;
    CssBackgroundParse outcome;
    const CssValue *first;
    const CssValue *second;
    size_t count;

    if (!out) {
        return RoboCssBackgroundParseInvalid;
    }
    *out = CssBackgroundPositionNone();
    count = BackgroundComponentCount(specified);
    if (count == 0u || count > 2u) {
        /* Three and four components are the edge-relative grammar, whose *count*
         * decides the meaning; this build does not represent it. */
        return RoboCssBackgroundParseInvalid;
    }
    first = BackgroundComponentAt(specified, 0u);
    second = count == 2u ? BackgroundComponentAt(specified, 1u) : NULL;

    outcome = BackgroundReadComponent(first, 0, &firstReading);
    if (outcome == RoboCssBackgroundParseDeferred) {
        return RoboCssBackgroundParseDeferred;
    }
    if (outcome != RoboCssBackgroundParsePosition) {
        return RoboCssBackgroundParseInvalid;
    }
    if (count == 1u) {
        /* The single-component form, folded: a keyword that names the vertical
         * axis (`top`) leaves the horizontal one at centre, and everything else —
         * an offset, `left`, `right`, `center` — is the horizontal one with the
         * vertical left at centre. That asymmetry is CSS's, and it is why this is
         * a branch on `namedAxis` rather than the same assignment twice. */
        if (firstReading.namedAxis == 1) {
            out->horizontal = BackgroundCenterComponent();
            out->vertical = firstReading.component;
        } else {
            out->horizontal = firstReading.component;
            out->vertical = BackgroundCenterComponent();
        }
        return RoboCssBackgroundParsePosition;
    }

    outcome = BackgroundReadComponent(second, 0, &secondReading);
    if (outcome == RoboCssBackgroundParseDeferred) {
        return RoboCssBackgroundParseDeferred;
    }
    if (outcome != RoboCssBackgroundParsePosition) {
        return RoboCssBackgroundParseInvalid;
    }

    if (firstReading.component.kind == RoboCssBackgroundComponentEdge &&
        secondReading.component.kind == RoboCssBackgroundComponentEdge) {
        int firstHome = firstReading.namedAxis;
        int secondHome = secondReading.namedAxis;

        if (firstHome == 0 && secondHome == 0) {
            /* `left right`: two horizontal edges and no vertical one. */
            return RoboCssBackgroundParseInvalid;
        }
        if (firstHome == 1 && secondHome == 1) {
            return RoboCssBackgroundParseInvalid;
        }
        if (firstHome == 1 || secondHome == 0) {
            /* Either the first names the vertical axis (`top left`) or the second
             * names the horizontal one (`center left`), and in both readings the
             * second component is the horizontal one. */
            out->horizontal = secondReading.component;
            out->vertical = firstReading.component;
        } else {
            /* `left top`, `left center`, `center center`, `center top`. */
            out->horizontal = firstReading.component;
            out->vertical = secondReading.component;
        }
        return RoboCssBackgroundParsePosition;
    }

    if (firstReading.component.kind != RoboCssBackgroundComponentEdge &&
        secondReading.component.kind != RoboCssBackgroundComponentEdge) {
        /* Two offsets: horizontal first, vertical second, in the order written. */
        out->horizontal = firstReading.component;
        out->vertical = secondReading.component;
        return RoboCssBackgroundParsePosition;
    }

    /* One keyword and one offset. `center 10px` is a position — horizontal centre,
     * ten pixels down — and `left 10px` is not: that is the edge-relative form,
     * where the offset hangs off the named edge, and there the *order* carries the
     * meaning. Reading it as `left` + `10px` would be a plausible wrong answer, so
     * it is refused instead. */
    {
        BackgroundReading edgeReading =
            firstReading.component.kind == RoboCssBackgroundComponentEdge
                ? firstReading : secondReading;

        if (edgeReading.component.edge != RoboCssBackgroundEdgeCenter) {
            return RoboCssBackgroundParseInvalid;
        }
    }
    out->horizontal = firstReading.component;
    out->vertical = secondReading.component;
    return RoboCssBackgroundParsePosition;
}

/* ------------------------------------------------------------------ */
/* Parsing a size                                                      */
/* ------------------------------------------------------------------ */

CssBackgroundParse CssBackgroundSizeFromValue(
    const CssValue *specified,
    CssBackgroundSize *out)
{
    CssBackgroundSize size = CssBackgroundSizeNone();
    BackgroundReading firstReading;
    BackgroundReading secondReading;
    CssBackgroundParse outcome;
    const CssValue *first;
    const CssValue *second;
    size_t count;

    if (!out) {
        return RoboCssBackgroundParseInvalid;
    }
    *out = CssBackgroundSizeNone();
    count = BackgroundComponentCount(specified);
    if (count == 0u || count > 2u) {
        return RoboCssBackgroundParseInvalid;
    }
    first = BackgroundComponentAt(specified, 0u);
    second = count == 2u ? BackgroundComponentAt(specified, 1u) : NULL;

    if (count == 1u) {
        char keyword[64];
        size_t keywordLength = 0u;

        if (BackgroundIdentifier(first, keyword, sizeof(keyword), &keywordLength)) {
            /* `cover` and `contain` are whole values, not components: neither is a
             * width, and a pair of them would be a value the property does not
             * have. */
            if (BackgroundKeywordIs(keyword, keywordLength, "cover")) {
                size.kind = RoboCssBackgroundSizeCover;
                *out = size;
                return RoboCssBackgroundParseSize;
            }
            if (BackgroundKeywordIs(keyword, keywordLength, "contain")) {
                size.kind = RoboCssBackgroundSizeContain;
                *out = size;
                return RoboCssBackgroundParseSize;
            }
        }
    }

    outcome = BackgroundReadComponent(first, 1, &firstReading);
    if (outcome == RoboCssBackgroundParseDeferred) {
        return RoboCssBackgroundParseDeferred;
    }
    if (outcome != RoboCssBackgroundParseSize &&
        outcome != RoboCssBackgroundParsePosition) {
        return RoboCssBackgroundParseInvalid;
    }
    if (firstReading.isAuto) {
        size.width.isAuto = 1;
    } else if (firstReading.component.kind == RoboCssBackgroundComponentEdge) {
        /* An edge is not a size: `background-size: left` is a mistake. */
        return RoboCssBackgroundParseInvalid;
    } else {
        size.width.numeric = firstReading.component.numeric;
    }

    if (count == 1u) {
        /* CSS: a single width leaves the height `auto`. */
        size.height.isAuto = 1;
    } else {
        outcome = BackgroundReadComponent(second, 1, &secondReading);
        if (outcome == RoboCssBackgroundParseDeferred) {
            return RoboCssBackgroundParseDeferred;
        }
        if (outcome != RoboCssBackgroundParseSize &&
            outcome != RoboCssBackgroundParsePosition) {
            return RoboCssBackgroundParseInvalid;
        }
        if (secondReading.isAuto) {
            size.height.isAuto = 1;
        } else if (secondReading.component.kind == RoboCssBackgroundComponentEdge) {
            return RoboCssBackgroundParseInvalid;
        } else {
            size.height.numeric = secondReading.component.numeric;
        }
    }

    if (size.width.isAuto && size.height.isAuto) {
        /* `auto auto` and `auto` are one computed value with one spelling, so they
         * are one *value* here rather than two that print differently. */
        size.kind = RoboCssBackgroundSizeAuto;
        size.width.isAuto = 0;
        size.height.isAuto = 0;
        *out = size;
        return RoboCssBackgroundParseSize;
    }
    size.kind = RoboCssBackgroundSizePair;
    *out = size;
    return RoboCssBackgroundParseSize;
}

/* ------------------------------------------------------------------ */
/* The shorthand                                                       */
/* ------------------------------------------------------------------ */

/*
 * The six longhands, in the order CSS serialises `background`: image, position,
 * size, repeat, attachment, colour. The order is load-bearing for the *output
 * array* — the caller indexes parts by the same order — so it is written once.
 */
static const char *const backgroundLonghands[RoboCssBackgroundLonghandCount] = {
    "background-image",
    "background-position",
    "background-size",
    "background-repeat",
    "background-attachment",
    "background-color"
};

/** Index of a longhand in @ref backgroundLonghands. */
#define BACKGROUND_PART_IMAGE 0u
#define BACKGROUND_PART_POSITION 1u
#define BACKGROUND_PART_SIZE 2u
#define BACKGROUND_PART_REPEAT 3u
#define BACKGROUND_PART_ATTACHMENT 4u
#define BACKGROUND_PART_COLOR 5u

/** How many components each longhand can take from one layer. */
static const size_t backgroundPartCapacity[RoboCssBackgroundLonghandCount] = {
    1u, /* image      */
    2u, /* position   */
    2u, /* size       */
    1u, /* repeat     */
    1u, /* attachment */
    1u  /* colour     */
};

size_t CssBackgroundLonghandCount(void)
{
    return RoboCssBackgroundLonghandCount;
}

const char *CssBackgroundLonghandName(size_t index)
{
    return index < RoboCssBackgroundLonghandCount ? backgroundLonghands[index] : NULL;
}

int CssBackgroundIsLonghand(CssSlice name)
{
    size_t index;

    if (CssSliceIsAbsent(name) || name.length == 0u) {
        return 0;
    }
    for (index = 0u; index < RoboCssBackgroundLonghandCount; index++) {
        if (CssBytesEqualAsciiCi(name.bytes, name.length,
                                          backgroundLonghands[index])) {
            return 1;
        }
    }
    return 0;
}

const char *CssBackgroundShorthandName(void)
{
    return "background";
}

/**
 * Which longhand @p component belongs to — by *kind*, which is what makes this a
 * different machine from the box model's positional mapping.
 *
 * @param afterSlash non-zero when the component follows a `/`, where the size
 *                    grammar is the only one that applies.
 * @param outPart    receives the longhand index.
 * @return 0 when the component belongs to no longhand, in which case @p outReason
 *         names what was wrong with it.
 */
static int BackgroundClassify(const CssValue *component,
                               int afterSlash,
                               size_t *outPart,
                               const char **outReason)
{
    CssToken token;
    char keyword[64];
    size_t keywordLength = 0u;
    CssSlice text;

    *outReason = "not a background value";

    if (afterSlash) {
        /* After a `/`, exactly the size grammar: `auto`, `cover`, `contain`, a
         * length or a percentage — and nothing else, because everything else has
         * a longhand of its own that the `/` does not reach. */
        if (BackgroundIdentifier(component, keyword, sizeof(keyword), &keywordLength) &&
            (BackgroundKeywordIs(keyword, keywordLength, "auto") ||
             BackgroundKeywordIs(keyword, keywordLength, "cover") ||
             BackgroundKeywordIs(keyword, keywordLength, "contain"))) {
            *outPart = BACKGROUND_PART_SIZE;
            return 1;
        }
        if (component && component->kind == RoboCssValueKindToken) {
            token = CssValueToken(component);
            if (token.type == RoboCssTokenPercentage ||
                token.type == RoboCssTokenDimension) {
                *outPart = BACKGROUND_PART_SIZE;
                return 1;
            }
        }
        *outReason = "not a background size";
        return 0;
    }

    if (component && component->kind == RoboCssValueKindFunction) {
        CssColor color = CssColorNone();

        if (BackgroundComponentIsSubstitution(component)) {
            /* A reference whose substitution can change how many components there
             * are: `--layer: url(a.png) no-repeat` makes one component three, so
             * distributing now would assign the whole reference to one longhand
             * and be wrong on the first real declaration. */
            *outReason = "a reference whose substitution can change the number of components";
            return 0;
        }
        /* A colour function *is* the colour — `rgb(10, 20, 30)`, `hsl(0 100% 50%)`
         * — and every other function is an image reference this build carries
         * rather than resolves. */
        if (CssColorFromFunction(component, &color) ==
            RoboCssColorParseResolved) {
            *outPart = BACKGROUND_PART_COLOR;
            return 1;
        }
        *outPart = BACKGROUND_PART_IMAGE;
        return 1;
    }

    if (!component || component->kind != RoboCssValueKindToken) {
        return 0;
    }
    token = CssValueToken(component);
    switch (token.type) {
    case RoboCssTokenIdent:
        if (!CssTokenKeywordText(&token, keyword, sizeof(keyword),
                                         &keywordLength)) {
            return 0;
        }
        text = CssSliceFromBytes((const uint8_t *)keyword, keywordLength);
        if (CssBackgroundRepeatSliceIsKnown(text)) {
            *outPart = BACKGROUND_PART_REPEAT;
            return 1;
        }
        if (CssBackgroundAttachmentSliceIsKnown(text)) {
            *outPart = BACKGROUND_PART_ATTACHMENT;
            return 1;
        }
        if (BackgroundKeywordIs(keyword, keywordLength, "none")) {
            *outPart = BACKGROUND_PART_IMAGE;
            return 1;
        }
        if (BackgroundKeywordIs(keyword, keywordLength, "auto") ||
            BackgroundKeywordIs(keyword, keywordLength, "cover") ||
            BackgroundKeywordIs(keyword, keywordLength, "contain")) {
            /* CSS's grammar puts these in `<bg-size>`, and `<bg-size>` appears
             * only after a `/`. So `background: cover` is not a shorthand missing
             * its other parts — it is a declaration with a size where a size
             * cannot go. */
            *outReason = "a size keyword needs a `/`";
            return 0;
        }
        if (BackgroundKeywordIs(keyword, keywordLength, "transparent") ||
            BackgroundKeywordIs(keyword, keywordLength, "currentcolor")) {
            *outPart = BACKGROUND_PART_COLOR;
            return 1;
        }
        {
            CssColor named = CssColorNone();

            if (CssColorFromName(text, &named)) {
                *outPart = BACKGROUND_PART_COLOR;
                return 1;
            }
        }
        if (CssBackgroundEdgeSliceIsKnown(
                text, RoboCssBackgroundAxisHorizontal) ||
            CssBackgroundEdgeSliceIsKnown(
                text, RoboCssBackgroundAxisVertical)) {
            *outPart = BACKGROUND_PART_POSITION;
            return 1;
        }
        *outReason = "not a background value";
        return 0;
    case RoboCssTokenHash:
        /* The fourth way to spell a colour, and it takes the same road as the
         * other three: `#fff` is a colour the 9.9 module already parses, so
         * `background: #fff` belongs to `background-color` for the same reason
         * `background: white` does. Refusing it would make one spelling of a
         * colour a mistake and another one correct, which is the sort of
         * difference an author has no way to see. */
        {
            CssColor hex = CssColorNone();

            if (CssColorFromValue(component, &hex) ==
                RoboCssColorParseResolved) {
                *outPart = BACKGROUND_PART_COLOR;
                return 1;
            }
        }
        *outReason = "not a background value";
        return 0;
    case RoboCssTokenPercentage:
    case RoboCssTokenDimension:
        *outPart = BACKGROUND_PART_POSITION;
        return 1;
    case RoboCssTokenNumber:
        /* Unitless, and therefore neither a position nor a size. */
        *outReason = "a bare number is not a background value";
        return 0;
    case RoboCssTokenUrl:
        /* `url(x.png)` unquoted arrives as a URL token rather than as a function,
         * and it is an image reference like any other. */
        *outPart = BACKGROUND_PART_IMAGE;
        return 1;
    case RoboCssTokenComma:
        *outReason = "multiple background layers are not represented";
        return 0;
    default:
        return 0;
    }
}

/** Fold the assignments into one list per longhand, or leave KIND_NONE. */
static Result BackgroundBuildParts(
    const CssValue *assigned[RoboCssBackgroundLonghandCount][2],
    const size_t assignedCount[RoboCssBackgroundLonghandCount],
    CssValue outParts[RoboCssBackgroundLonghandCount],
    CssDiag *diag)
{
    size_t partIndex;

    for (partIndex = 0u; partIndex < RoboCssBackgroundLonghandCount; partIndex++) {
        size_t index;
        CssValue list = RoboCssCleared;

        if (assignedCount[partIndex] == 0u) {
            /* KIND_NONE: the shorthand did not mention this longhand, so its
             * initial value applies. Deliberately not supplied here — the initial
             * values are the registry's (see the header). */
            continue;
        }
        CssValueInit(&list);
        if (CssValueMakeList(&list, diag) != RoboOk) {
            CssValueDispose(&list);
            return RoboOutOfMemory;
        }
        for (index = 0u; index < assignedCount[partIndex]; index++) {
            CssValue copy = RoboCssCleared;
            Result result;

            CssValueInit(&copy);
            result = CssValueCopy(&copy, assigned[partIndex][index], diag);
            if (result == RoboOk) {
                /* Built as a list rather than as the bare component, so the
                 * result has the shape a *declared* longhand's value has — which
                 * is what lets it go through the same normalisers with no special
                 * case. `append` moves, so `copy` owns nothing afterwards. */
                result = CssValueAppend(&list, &copy, diag);
            }
            if (result != RoboOk) {
                CssValueDispose(&copy);
                CssValueDispose(&list);
                return result;
            }
        }
        outParts[partIndex] = list;
    }
    return RoboOk;
}

Result CssBackgroundExpand(
    const CssValue *value,
    CssValue outParts[RoboCssBackgroundLonghandCount],
    CssDiag *diag)
{
    const CssValue *assigned[RoboCssBackgroundLonghandCount][2];
    size_t assignedCount[RoboCssBackgroundLonghandCount];
    size_t count;
    size_t index;
    size_t partIndex;
    size_t slash = 0u;
    int hasSlash = 0;
    size_t sizeStart = 0u;
    size_t sizeEnd = 0u;
    Result result;

    if (!outParts) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "the output array is NULL");
    }
    for (partIndex = 0u; partIndex < RoboCssBackgroundLonghandCount; partIndex++) {
        CssValueInit(&outParts[partIndex]);
        assignedCount[partIndex] = 0u;
        assigned[partIndex][0] = NULL;
        assigned[partIndex][1] = NULL;
    }
    if (!value) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboInvalidArgument,
                             "the shorthand value is NULL");
    }
    count = BackgroundComponentCount(value);
    if (count == 0u) {
        return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                             "a background shorthand has no components");
    }

    /* First pass: find the `/` and the size group it binds. */
    for (index = 0u; index < count; index++) {
        if (BackgroundComponentIsSlash(BackgroundComponentAt(value, index))) {
            if (hasSlash) {
                return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                                     "more than one `/` in a background shorthand");
            }
            hasSlash = 1;
            slash = index;
        }
    }
    if (hasSlash) {
        const char *reason = NULL;
        size_t part = 0u;

        if (slash == 0u || slash + 1u >= count) {
            return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                                 "a `/` needs a position before it and a size after");
        }
        /* CSS's grammar is `<bg-position> [ / <bg-size> ]?` and not the reverse,
         * so the component immediately before the `/` has to be a position. */
        if (!BackgroundClassify(BackgroundComponentAt(value, slash - 1u), 0, &part,
                                 &reason)) {
            return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                                 "a `/` must follow a position");
        }
        /* The size is the components immediately after the `/`: one, or two when
         * the second is size-shaped as well. */
        sizeStart = slash + 1u;
        sizeEnd = sizeStart;
        if (sizeStart + 1u < count &&
            BackgroundClassify(BackgroundComponentAt(value, sizeStart + 1u), 1, &part,
                                &reason)) {
            sizeEnd = sizeStart + 1u;
        }
    }

    /* Second pass: classify and assign. */
    for (index = 0u; index < count; index++) {
        const CssValue *component = BackgroundComponentAt(value, index);
        const char *reason = NULL;
        size_t part = 0u;

        if (BackgroundComponentIsSlash(component)) {
            continue;
        }
        if (!BackgroundClassify(component, hasSlash && index >= sizeStart &&
                                            index <= sizeEnd, &part, &reason)) {
            return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                                 reason);
        }
        if (assignedCount[part] >= backgroundPartCapacity[part]) {
            return RoboCssDiag(diag, RoboCssDomainBackground, RoboNotSupported,
                                 "two components the same longhand cannot take");
        }
        assigned[part][assignedCount[part]] = component;
        assignedCount[part]++;
    }

    result = BackgroundBuildParts(assigned, assignedCount, outParts, diag);
    if (result != RoboOk) {
        /* All or nothing: a caller that ignored the result code must not be handed
         * four of a shorthand's six longhands. */
        for (partIndex = 0u; partIndex < RoboCssBackgroundLonghandCount;
             partIndex++) {
            CssValueDispose(&outParts[partIndex]);
            CssValueInit(&outParts[partIndex]);
        }
        return result;
    }
    return RoboOk;
}
