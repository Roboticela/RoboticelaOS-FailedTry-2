/*
 * RoboPaint operations (Phase 11.1–11.2) — see RoboPaintOperation.h.
 *
 * The record, the validity rule that decides whether it may enter a list, the two
 * coordinate spaces in one call, and the band a border side occupies. The validity rule
 * is the one piece of policy here and it lives in one function so that
 * `PaintListAppend` and every read door share it rather than each deciding what
 * "a record" means.
 */

#include "RoboPaint/RoboPaintOperation.h"

#include "RoboPaintInternal.h"

#include <string.h>

/** Spaces the kind name is padded to, so that a two-list diff lines up. */
#define RoboPaintOpNameColumn 14u

void PaintOperationInit(PaintOperation *operation)
{
    if (!operation) {
        return;
    }
    memset(operation, 0, sizeof(*operation));
    operation->kind = RoboPaintOpNone;
    operation->node = RoboLayoutNodeIdNone;
    PaintBoxesInit(&operation->boxes);
    operation->origin = MakeGfxPoint(0, 0);
    PaintClipInit(&operation->clip);
    /* And nothing in force, which is *fully opaque* rather than zero -- the one field here
     * whose cleared state a `memset` does not produce, and the reason it is written by a
     * door. A zeroed opacity record is a valid, meaningful, fully transparent one, so a
     * record nothing set would be a drawing at zero strength rather than a drawing at full
     * strength (RoboPaintOpacity.h). */
    PaintOpacityInit(&operation->opacity);
    operation->color = CssColorNone();
    /* A cleared record carries no border style, which is what a non-border kind requires
     * of every record -- so "nobody set it" and "this is not a border" are one state. */
    operation->style = RoboCssBorderStyleNone;
    /* And no text run, which is what a non-text kind requires: an empty span, no baseline
     * and the CSS initial font inputs, so "nobody set it" and "this is not text" are one
     * state the same way. The memset above already zeroed the storage; this is here so
     * that "a cleared run" is `RoboPaintText.h`'s definition rather than a zero pattern
     * this file happens to agree with. */
    PaintTextRunInit(&operation->text);
    /* And no image payload, which is what a non-image kind requires of every record, for the
     * same reason: "nobody set it" and "this drawing has no resource" are one state. */
    PaintImageInit(&operation->image);
}

int PaintOperationIsValid(const PaintOperation *operation)
{
    if (!operation) {
        return 0;
    }
    if (!PaintOpKindIsKnown(operation->kind)) {
        return 0;
    }
    if (operation->kind == RoboPaintOpNone) {
        /* A cleared record is a state, not an operation. Refusing it here is what keeps
         * "the list is empty" and "the list has one meaningless entry" apart. */
        return 0;
    }
    if (operation->node == RoboLayoutNodeIdNone) {
        return 0;
    }
    if (!PaintBoxesIsValid(&operation->boxes)) {
        return 0;
    }
    if (!PaintClipIsValid(&operation->clip)) {
        return 0;
    }
    /*
     * The **clip scope** rule (11.8), and it is the "this record's fields agree about what it
     * is" rule for a third time. A `PUSH_CLIP`/`POP_CLIP` record states that a boundary is in
     * force; a scope whose clip bounds neither axis states the opposite, and a consumer that
     * honoured it would push and pop a record that bounds nothing -- so it is refused here
     * rather than emitted and skipped by every reader.
     *
     * It is checked *before* the colour split rather than inside the structural branch,
     * because the fact it is about is the clip and not the colour: a clip record with a colour
     * is refused by the structural branch below, and this rule would still be the right answer
     * for one that reached here.
     */
    if (PaintOpKindIsClip(operation->kind) &&
        !PaintClipIsActive(&operation->clip)) {
        return 0;
    }
    /*
     * The **opacity scope** rule (11.9), the clip rule's sibling one field along, and it is the
     * same shape for the same reason: a `PUSH_OPACITY`/`POP_OPACITY` record states that a
     * strength is in force, so a scope that is fully **opaque** states the opposite -- a
     * consumer that honoured it would push and pop a record that changes nothing about what is
     * inside it, which is a record a consumer could only skip.
     *
     * The range is checked for **every** kind rather than only for the two scopes, and that is
     * the one place this rule is wider than the clip's: the clip is a well-formed value on every
     * record whatever it bounds, while an opacity outside `[0, 1]` is not a value at all -- so a
     * drawing carrying `2` would be a drawing whose strength a rasteriser cannot apply. A
     * structural record is held to the same rule, because "what strength was in force here" is
     * a question every record answers.
     */
    if (!PaintOpacityIsValid(&operation->opacity)) {
        return 0;
    }
    if (PaintOpKindIsOpacity(operation->kind) &&
        PaintOpacityIsOpaque(&operation->opacity)) {
        return 0;
    }
    /*
     * The colour rule, and it is two rules because the two halves of the vocabulary have
     * different answers. A kind whose appearance **is** a colour -- the background, the four
     * border sides and a text run -- must carry a colour a rasteriser can use: resolved, or
     * `currentColor` that this layer could not resolve (`CssColorIsValid` is exactly
     * that pair). `none` on such a kind is refused because a fill or a border with no colour
     * draws nothing, and an operation that draws nothing is not a statement about the
     * document. Every other kind must carry the colour that says nothing was declared --
     * `CssColorNone()`, which is RoboCSS's own "no colour", not a black a consumer
     * might draw.
     *
     * The split is `PaintOpKindUsesColor` rather than `draws`, and 11.7 is why the
     * two arrived a phase apart: `draws` is true of an `IMAGE` record, whose appearance is its
     * resource's pixels, so a rule stated on `draws` would be asking an image for a colour it
     * cannot have. Stated positively -- "these kinds have a colour" -- the next payload kind
     * says so in one place instead of being added to an exception list here. It is not
     * `is_marker` either, so that `CONTENT`, a structural record that is not a box marker, lands
     * on the "carries no colour" side rather than being asked for one.
     */
    if (PaintOpKindUsesColor(operation->kind)) {
        if (!CssColorIsValid(operation->color)) {
            return 0;
        }
        /*
         * The style rule, and it is the colour rule's shape one field along. A border
         * must carry a style that **generates** a border, and the reason it is
         * `generates_border` rather than "not `none`" is 10.11's own rule: `none` and
         * `hidden` both draw nothing, which is why such a side's *used* width is zero and
         * no record is emitted for it at all. So a border record whose style draws
         * nothing is a record whose fields disagree about what it is, and it is refused
         * here rather than drawn as nothing by a consumer that trusted the kind.
         *
         * A **background** must carry `NONE`: it has no side style, and a background with
         * one would be carrying a fact about a border it is not.
         *
         * And the **text rule** (11.6), the same shape a third time: a `TEXT` record must
         * carry a run -- a non-empty span, a non-negative baseline offset and font inputs
         * that are a record -- and a kind that is not text must carry a **cleared** one.
         * Both halves are refusals: a text record with no characters is a drawing that
         * draws nothing, and a background that came with font inputs is a record carrying
         * facts about characters it does not have. Nothing here checks the span against a
         * pool, because that is a question about a container and this door takes a
         * record (`RoboPaintList.h` asks it where the pool is).
         */
        if (operation->kind == RoboPaintOpText) {
            return operation->style == RoboCssBorderStyleNone &&
                   PaintTextRunIsValid(&operation->text) &&
                   PaintImageIsCleared(&operation->image);
        }
        if (PaintOpKindIsBorder(operation->kind)) {
            return CssBorderStyleGeneratesBorder(operation->style);
        }
        return operation->style == RoboCssBorderStyleNone &&
               !PaintTextRunIsValid(&operation->text) &&
               PaintImageIsCleared(&operation->image);
    }
    /*
     * Every kind whose appearance is **not** a colour this module resolved -- a structural
     * record and, since 11.7, an `IMAGE` record -- and the first half of the rule is the same
     * for both: the colour field must be `none`, because a colour on a record that has no fill
     * is a fact about a drawing that does not exist. The colour rule above is stated on
     * `uses_color` rather than on `draws` for exactly this reason, and this is the branch that
     * makes the split worth having.
     *
     * A **structural** record draws nothing, so it carries the absence of a colour, of a style
     * and of a text run -- and of an image payload too: a `BEGIN_BOX` naming a resource would
     * be a marker carrying a drawing. The text half is stated as "the run is not a run" rather
     * than as a field-by-field comparison against a cleared record, because a cleared run is
     * exactly a run `PaintTextRunIsValid` refuses and a second spelling of it would be
     * a second place for the two to drift.
     *
     * And the **image rule** (11.7), the first of these whose subject is not a colour: an
     * `IMAGE` record's appearance **is** its resource's pixels, so it must carry a payload this
     * build may draw -- a resource with a surface, positive intrinsic dimensions and a source
     * rectangle inside it -- and, carrying no colour, it must carry no **style** either. Its
     * text run must be cleared for the reason every non-`TEXT` kind's must: font inputs on a
     * record that draws no glyphs are facts about characters it does not have. The two halves
     * are the same "its fields agree about what it is" rule the colour split states, applied to
     * a payload instead of to a fill.
     */
    if (operation->color.kind != RoboCssColorNone) {
        return 0;
    }
    if (operation->kind == RoboPaintOpImage) {
        return PaintImageIsValid(&operation->image) &&
               operation->style == RoboCssBorderStyleNone &&
               !PaintTextRunIsValid(&operation->text);
    }
    return operation->style == RoboCssBorderStyleNone &&
           !PaintTextRunIsValid(&operation->text) &&
           PaintImageIsCleared(&operation->image);
}

int PaintOperationEqual(const PaintOperation *a,
                               const PaintOperation *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->kind == b->kind && a->node == b->node && a->depth == b->depth &&
           PaintBoxesEqual(&a->boxes, &b->boxes) &&
           GfxPointEqual(a->origin, b->origin) &&
           PaintClipEqual(&a->clip, &b->clip) &&
           PaintOpacityEqual(&a->opacity, &b->opacity) &&
           CssColorEquals(a->color, b->color) && a->style == b->style &&
           PaintTextRunEqual(&a->text, &b->text) &&
           /* An image payload compares by **identity**, not by address, which is why
            * `PaintImageEqual` exists as a door rather than this being four more
            * comparisons here (RoboPaintImage.h). */
           PaintImageEqual(&a->image, &b->image);
}

PaintOpKind PaintOperationKind(const PaintOperation *operation)
{
    return operation ? operation->kind : RoboPaintOpNone;
}

LayoutNodeId PaintOperationNode(const PaintOperation *operation)
{
    return operation ? operation->node : RoboLayoutNodeIdNone;
}

size_t PaintOperationDepth(const PaintOperation *operation)
{
    return operation ? operation->depth : 0u;
}

void PaintOperationBoxes(const PaintOperation *operation,
                                PaintBoxes *out)
{
    if (!out) {
        return;
    }
    if (!operation) {
        PaintBoxesInit(out);
        return;
    }
    *out = operation->boxes;
}

void PaintOperationOrigin(const PaintOperation *operation,
                                 LayoutPoint *out)
{
    if (!out) {
        return;
    }
    *out = operation ? operation->origin : MakeGfxPoint(0, 0);
}

void PaintOperationClip(const PaintOperation *operation,
                               PaintClip *out)
{
    if (!out) {
        return;
    }
    if (!operation) {
        PaintClipInit(out);
        return;
    }
    *out = operation->clip;
}

void PaintOperationOpacity(const PaintOperation *operation,
                                 PaintOpacity *out)
{
    if (!out) {
        return;
    }
    if (!operation) {
        /* Nothing in force, which is full strength -- the same answer an empty scope stack
         * gives, so the two states a caller can be in read alike. */
        PaintOpacityInit(out);
        return;
    }
    *out = operation->opacity;
}

CssColor PaintOperationColor(const PaintOperation *operation)
{
    return operation ? operation->color : CssColorNone();
}

CssBorderStyle PaintOperationStyle(const PaintOperation *operation)
{
    return operation ? operation->style : RoboCssBorderStyleNone;
}

PaintTextRun PaintOperationText(const PaintOperation *operation)
{
    PaintTextRun run;

    if (!operation) {
        PaintTextRunInit(&run);
        return run;
    }
    return operation->text;
}

PaintImage PaintOperationImage(const PaintOperation *operation)
{
    PaintImage image;

    PaintImageInit(&image);
    if (!operation || operation->kind != RoboPaintOpImage) {
        /* A kind that is not an image answers with a cleared payload rather than with the
         * field, which is the rule the text accessor states one field along: a caller that
         * did not ask the kind gets "no resource", not a leftover. */
        return image;
    }
    return operation->image;
}

int PaintOperationIsStructural(const PaintOperation *operation)
{
    return operation != NULL && PaintOpKindIsStructural(operation->kind);
}

int PaintOperationIsMarker(const PaintOperation *operation)
{
    return operation != NULL && PaintOpKindIsMarker(operation->kind);
}

int PaintOperationIsDrawing(const PaintOperation *operation)
{
    return operation != NULL && PaintOpKindDraws(operation->kind);
}

Result PaintOperationSide(const PaintOperation *operation,
                                        CssBoxSide *out)
{
    if (!operation || !out) {
        return RoboInvalidArgument;
    }
    return PaintOpKindSide(operation->kind, out);
}

Result PaintOperationLocalBoxes(const PaintOperation *operation,
                                               PaintBoxes *out,
                                               PaintDiag *diag)
{
    if (!operation || !out) {
        return RoboPaintDiag(diag, RoboPaintDomainOperation, RoboInvalidArgument,
                               "the operation and an output are required");
    }
    if (!PaintBoxesIsValid(&operation->boxes)) {
        return RoboPaintDiag(diag, RoboPaintDomainOperation, RoboError,
                               "the operation's boxes are not a record");
    }
    return PaintBoxesTranslate(&operation->boxes, -operation->origin.x,
                                      -operation->origin.y, out, diag);
}

Result PaintOperationSideBand(const PaintOperation *operation,
                                             LayoutRect *out,
                                             PaintDiag *diag)
{
    CssBoxSide side;
    const LayoutRect *border;
    const LayoutRect *padding;
    int32_t borderRight;
    int32_t borderBottom;
    int32_t paddingRight;
    int32_t paddingBottom;

    if (!operation || !out) {
        return RoboPaintDiag(diag, RoboPaintDomainOperation, RoboInvalidArgument,
                               "the operation and an output are required");
    }
    if (PaintOpKindSide(operation->kind, &side) != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainOperation, RoboInvalidArgument,
                               "only a border operation occupies a band");
    }
    border = &operation->boxes.border;
    padding = &operation->boxes.padding;
    borderRight = PaintAddSaturated(border->x, border->width);
    borderBottom = PaintAddSaturated(border->y, border->height);
    paddingRight = PaintAddSaturated(padding->x, padding->width);
    paddingBottom = PaintAddSaturated(padding->y, padding->height);

    /*
     * The band on one side is the region of the border box outside the padding box on
     * that edge, and the two axes are answered differently on purpose:
     *
     *   a **top** or **bottom** band spans the border box's full width and stops where
     *   the padding box begins or ends vertically;
     *   a **left** or **right** band spans the border box's full height and stops where
     *   the padding box begins or ends horizontally.
     *
     * Both faces of an axis contribute to the *other* axis's extent, which is why the
     * full width (or height) is used rather than the difference on both axes: a box with
     * a left border and a right border has two bands, each as tall as the box.
     *
     * Every extent is floored at zero, so a box whose insets were saturated or declined
     * produces an empty band rather than a negative one -- and an empty band is a
     * correct answer for a border that occupies no room.
     */
    switch (side) {
    case RoboCssBoxSideTop:
        out->x = border->x;
        out->y = border->y;
        out->width = border->width;
        out->height = PaintExtent((int64_t)padding->y - (int64_t)border->y);
        break;
    case RoboCssBoxSideRight:
        out->x = paddingRight;
        out->y = border->y;
        out->width = PaintExtent((int64_t)borderRight - (int64_t)paddingRight);
        out->height = border->height;
        break;
    case RoboCssBoxSideBottom:
        out->x = border->x;
        out->y = paddingBottom;
        out->width = border->width;
        out->height = PaintExtent((int64_t)borderBottom - (int64_t)paddingBottom);
        break;
    case RoboCssBoxSideLeft:
        out->x = border->x;
        out->y = border->y;
        out->width = PaintExtent((int64_t)padding->x - (int64_t)border->x);
        out->height = border->height;
        break;
    default:
        return RoboPaintDiag(diag, RoboPaintDomainOperation, RoboInvalidArgument,
                               "not a border side");
    }
    return RoboOk;
}

Result PaintOperationWrite(const PaintOperation *operation,
                                         char *buffer,
                                         size_t capacity,
                                         size_t *outLength,
                                         PaintDiag *diag)
{
    PaintWriter text;
    const char *name;
    char colorText[RoboCssColorTextMax + 1u];
    size_t colorLength = 0u;
    size_t index;
    LayoutRect rect;
    int haveRect = 0;

    if (!operation || !buffer || !outLength || capacity == 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainOperation, RoboInvalidArgument,
                               "the operation, a buffer and an out_length are required");
    }
    if (!PaintOpKindIsKnown(operation->kind) ||
        operation->kind == RoboPaintOpNone) {
        return RoboPaintDiag(diag, RoboPaintDomainOperation, RoboInvalidArgument,
                               "not an operation kind");
    }

    PaintWriterInit(&text, buffer, capacity);
    name = PaintOpKindName(operation->kind);
    PaintWriterString(&text, name);
    for (index = strlen(name); index < RoboPaintOpNameColumn; index++) {
        PaintWriterChar(&text, ' ');
    }

    PaintWriterString(&text, "node=");
    PaintWriterU64(&text, (uint64_t)operation->node);
    PaintWriterString(&text, " depth=");
    PaintWriterU64(&text, (uint64_t)operation->depth);

    /* The clip, as two modes with the interval appended only on an axis that has one:
     * `clip=hidden[0,100]/visible`. */
    PaintWriterString(&text, " clip=");
    PaintWriterString(&text, CssOverflowName(operation->clip.horizontal.mode));
    if (PaintClipAxisIsActive(&operation->clip.horizontal)) {
        PaintWriterChar(&text, '[');
        PaintWriterI64(&text, (int64_t)operation->clip.horizontal.start);
        PaintWriterChar(&text, ',');
        PaintWriterI64(&text, (int64_t)operation->clip.horizontal.end);
        PaintWriterChar(&text, ']');
    }
    PaintWriterChar(&text, '/');
    PaintWriterString(&text, CssOverflowName(operation->clip.vertical.mode));
    if (PaintClipAxisIsActive(&operation->clip.vertical)) {
        PaintWriterChar(&text, '[');
        PaintWriterI64(&text, (int64_t)operation->clip.vertical.start);
        PaintWriterChar(&text, ',');
        PaintWriterI64(&text, (int64_t)operation->clip.vertical.end);
        PaintWriterChar(&text, ']');
    }

    /*
     * The **opacity in force** (11.9), printed for every kind exactly as the clip is, and the
     * reason is the same one: what is in force is a question every record answers, so a field
     * that appeared only on a scope would make a drawing's strength unreadable from the drawing
     * a dump is being read for. Two decimals, because a record's value is in `[0, 1]` and two
     * are enough to tell every value a document is likely to declare from its neighbours --
     * `opacity=0.50` beside `opacity=1.00`. The writer is this module's own, so the spelling is
     * deterministic and locale-independent rather than `printf`'s.
     */
    PaintWriterString(&text, " opacity=");
    PaintWriterNumber(&text, PaintOpacityValue(&operation->opacity), 2u);

    /* The rectangle this kind acts on: a marker and a background act on the border box,
     * a border acts on its band. Which one is printed is the kind's business, so a reader
     * of a dump does not have to remember that `background` means "the border box". */
    if (PaintOpKindIsBorder(operation->kind)) {
        if (PaintOperationSideBand(operation, &rect, NULL) == RoboOk) {
            haveRect = 1;
        }
        PaintWriterString(&text, " band=");
    } else if (operation->kind == RoboPaintOpBackground) {
        rect = operation->boxes.border;
        haveRect = 1;
        PaintWriterString(&text, " rect=");
    } else if (operation->kind == RoboPaintOpContent) {
        /* The content boundary acts on the inner rectangle: the region a box's own
         * painting ends at and its contents begin in. Printed explicitly so a reader of
         * a dump sees which of the three rectangles the record is about, rather than
         * having to remember that `content` means "the content box". */
        rect = operation->boxes.content;
        haveRect = 1;
        PaintWriterString(&text, " content=");
    } else if (PaintOpKindIsClip(operation->kind)) {
        /*
         * A **clip scope** acts on the padding box -- the boundary the box's own `overflow`
         * resolved to -- which is the same rectangle the display command carries, and the
         * reason this is a branch at all is that the generic case below would print the
         * *border* box and tell a reader the scope came from a rectangle it did not come
         * from. The boundary itself is in the `clip` field above; this is the box it
         * belongs to.
         */
        rect = operation->boxes.padding;
        haveRect = 1;
        PaintWriterString(&text, " padding=");
    } else {
        rect = operation->boxes.border;
        haveRect = 1;
        PaintWriterString(&text, " border=");
    }
    if (haveRect) {
        PaintWriterRect(&text, rect);
    }

    /*
     * The colour, printed for the kinds that **have** one (`PaintOpKindUsesColor`)
     * rather than for the kinds that draw. 11.7 is why the two are not the same question: an
     * `IMAGE` record draws and carries `none`, so a line that asked it for a colour would print
     * either nothing or a "none" that a reader would take for a fill.
     */
    if (PaintOpKindUsesColor(operation->kind)) {
        if (CssColorWrite(operation->color, colorText, sizeof(colorText),
                                 &colorLength, NULL) == RoboOk) {
            PaintWriterString(&text, " color=");
            PaintWriterString(&text, colorText);
        }
    }
    /* Printed exactly where it means something, which is how the record states it too: a
     * border says which of the ten styles it draws, and nothing else has one to print. */
    if (PaintOpKindIsBorder(operation->kind)) {
        PaintWriterString(&text, " style=");
        PaintWriterString(&text, CssBorderStyleName(operation->style));
    }
    /*
     * A text record prints the **span** and not the characters, and the difference is a
     * lifetime rather than a preference: this door is handed one record, and a record
     * carries two numbers that name characters in a pool it does not have. The characters
     * are printed by the two list dumps (`RoboPaintDump.h`), which do have the pool --
     * which is also where a reader wants them, because that is the view of a whole
     * document rather than of one line of it.
     */
    /*
     * An image record prints the **resource's identity**, the source rectangle and the
     * resource's own dimensions -- everything the record carries, and **no address**: an
     * identity is a fact about the drawing, where a pointer is a fact about this process's
     * heap, and a dump whose lines changed between two runs of one document would be a dump
     * nobody could diff (`RoboPaintImage.h`). The pixels are not printed here for the same
     * reason a run's characters are not: a record names them, and this door has no pool of
     * them to read.
     */
    if (operation->kind == RoboPaintOpImage) {
        PaintWriterString(&text, " resource=");
        PaintWriterU64(&text, (uint64_t)PaintImageIdentity(&operation->image));
        /*
         * The source rectangle is a **RoboGraphics** rectangle rather than a layout one -- it
         * is in image pixels, which is the only space an image's own pixels have -- so it is
         * written field by field here instead of through the layout-rectangle helper. The
         * order is that helper's so the two read alike in a dump: `x,y,wxh`.
         */
        PaintWriterString(&text, " src=");
        PaintWriterI64(&text, (int64_t)operation->image.source.x);
        PaintWriterChar(&text, ',');
        PaintWriterI64(&text, (int64_t)operation->image.source.y);
        PaintWriterChar(&text, ',');
        PaintWriterI64(&text, (int64_t)operation->image.source.width);
        PaintWriterChar(&text, 'x');
        PaintWriterI64(&text, (int64_t)operation->image.source.height);
        PaintWriterString(&text, " source_size=");
        PaintWriterU64(&text, (uint64_t)operation->image.width);
        PaintWriterChar(&text, 'x');
        PaintWriterU64(&text, (uint64_t)operation->image.height);
    }
    if (operation->kind == RoboPaintOpText) {
        PaintWriterString(&text, " code_points=");
        PaintWriterU64(&text, (uint64_t)operation->text.span.count);
        PaintWriterString(&text, " baseline=");
        PaintWriterI64(&text, (int64_t)operation->text.baselineOffset);
        PaintWriterString(&text, " font=");
        PaintWriterNumber(&text, operation->text.font.fontSizePx, 2u);
        PaintWriterChar(&text, '/');
        PaintWriterI64(&text, (int64_t)operation->text.font.fontWeight);
        PaintWriterChar(&text, '/');
        PaintWriterString(&text, operation->text.font.italic ? "italic" : "upright");
        PaintWriterChar(&text, '/');
        PaintWriterString(&text,
                                 operation->text.font.direction ==
                                         RoboLayoutTextDirectionRtl ? "rtl" : "ltr");
    }

    *outLength = text.length;
    if (!PaintWriterFits(&text)) {
        return RoboPaintDiag(diag, RoboPaintDomainOperation, RoboNotSupported,
                               "the operation text does not fit the buffer");
    }
    return RoboOk;
}
