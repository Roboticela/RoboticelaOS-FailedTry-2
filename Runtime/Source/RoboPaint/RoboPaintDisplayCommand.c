/*
 * RoboDisplay commands (Phase 11.3) — see RoboPaintDisplayCommand.h.
 *
 * The vocabulary table, the record, the validity rule the list and the builder share, and
 * the one-line writer. The vocabulary is a table for the reason RoboPaint's is: a row per
 * enumerator means the compiler counts the rows against the count, so an enumerator added
 * without a name is a compile-time omission rather than a value that prints "unknown" in a
 * bug report.
 */

#include "RoboPaint/RoboPaintDisplayCommand.h"

#include "RoboPaintInternal.h"

#include <string.h>

/** Spaces the kind name is padded to, so that a two-list diff lines up. */
#define RoboPaintDisplayNameColumn 16u

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/** One command kind: the enumerator and its name. */
typedef struct displayKindRow {
    PaintDisplayCommandKind kind;
    const char *name;
} DisplayKindRow;

static const DisplayKindRow displayKindRows[RoboPaintDisplayCmdKindCount] = {
    { RoboPaintDisplayCmdNone, "none" },
    { RoboPaintDisplayCmdBeginNode, "begin-node" },
    { RoboPaintDisplayCmdDrawBackground, "draw-background" },
    { RoboPaintDisplayCmdDrawBorder, "draw-border" },
    { RoboPaintDisplayCmdContent, "content" },
    { RoboPaintDisplayCmdPushClip, "push-clip" },
    { RoboPaintDisplayCmdDrawText, "draw-text" },
    { RoboPaintDisplayCmdPopClip, "pop-clip" },
    { RoboPaintDisplayCmdEndNode, "end-node" },
    /* 11.9's pair. Appended rather than inserted, for the reason `RoboPaintTypes.h` states at
     * the operation vocabulary: a stored enumerator is a contract, and the *paint* order these
     * two have -- an opacity scope opening before its box draws and closing before its end
     * marker -- is asserted by the structural check rather than encoded in a numbering. */
    { RoboPaintDisplayCmdPushOpacity, "push-opacity" },
    { RoboPaintDisplayCmdPopOpacity, "pop-opacity" },
    /* 11.7's drawing kind, appended for the same reason: a stored enumerator is a contract. Its
     * *paint* order -- between a node's border commands and its `CONTENT` -- is asserted by the
     * structural check, not encoded in a numbering. */
    { RoboPaintDisplayCmdDrawImage, "draw-image" }
};

/** The row for @p kind, or NULL when @p kind names none. */
static const DisplayKindRow *DisplayKindRowOf(PaintDisplayCommandKind kind)
{
    size_t index;

    for (index = 0u; index < (size_t)RoboPaintDisplayCmdKindCount; index++) {
        if (displayKindRows[index].kind == kind) {
            return &displayKindRows[index];
        }
    }
    return NULL;
}

int PaintDisplayCommandKindIsKnown(PaintDisplayCommandKind kind)
{
    return DisplayKindRowOf(kind) != NULL;
}

const char *PaintDisplayCommandKindName(PaintDisplayCommandKind kind)
{
    const DisplayKindRow *row = DisplayKindRowOf(kind);

    return row ? row->name : "unknown";
}

int PaintDisplayCommandKindDraws(PaintDisplayCommandKind kind)
{
    return kind == RoboPaintDisplayCmdDrawBackground ||
           kind == RoboPaintDisplayCmdDrawBorder ||
           kind == RoboPaintDisplayCmdDrawText ||
           kind == RoboPaintDisplayCmdDrawImage;
}

int PaintDisplayCommandKindUsesColor(PaintDisplayCommandKind kind)
{
    /*
     * The kinds whose look **is** a colour, listed rather than written as "draws and is not an
     * image", so that the next payload kind is a kind that answers no here instead of a special
     * case in the validity rule -- the argument the operation vocabulary makes in the same shape.
     */
    return kind == RoboPaintDisplayCmdDrawBackground ||
           kind == RoboPaintDisplayCmdDrawBorder ||
           kind == RoboPaintDisplayCmdDrawText;
}

int PaintDisplayCommandKindIsStructural(PaintDisplayCommandKind kind)
{
    /*
     * The complement of drawing above `NONE`, written as that complement rather than as a
     * second list of three enumerators, for the reason RoboPaint restates it: a list would
     * be a second place to add the next kind, and the day the two disagreed a command
     * would be neither drawing nor structural and the validity rule would have no branch
     * for it. `NONE` is excluded because a cleared record is not a command.
     */
    if (!PaintDisplayCommandKindIsKnown(kind) || kind == RoboPaintDisplayCmdNone) {
        return 0;
    }
    return !PaintDisplayCommandKindDraws(kind);
}

int PaintDisplayCommandKindIsMarker(PaintDisplayCommandKind kind)
{
    return kind == RoboPaintDisplayCmdBeginNode || kind == RoboPaintDisplayCmdEndNode;
}

int PaintDisplayCommandKindIsClip(PaintDisplayCommandKind kind)
{
    return kind == RoboPaintDisplayCmdPushClip ||
           kind == RoboPaintDisplayCmdPopClip;
}

int PaintDisplayCommandKindIsOpacity(PaintDisplayCommandKind kind)
{
    return kind == RoboPaintDisplayCmdPushOpacity ||
           kind == RoboPaintDisplayCmdPopOpacity;
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void PaintDisplayCommandInit(PaintDisplayCommand *command)
{
    if (!command) {
        return;
    }
    memset(command, 0, sizeof(*command));
    command->kind = RoboPaintDisplayCmdNone;
    command->node = RoboLayoutNodeIdNone;
    command->rect = MakeGfxRect(0, 0, 0, 0);
    PaintClipInit(&command->clip);
    /* Nothing in force, which is *fully opaque* -- the one field here a `memset` cannot clear
     * correctly, because zeroing it would be a valid, meaningful, fully transparent record. */
    PaintOpacityInit(&command->opacity);
    command->color = CssColorNone();
    command->style = RoboCssBorderStyleNone;
    /* And no text run, which is what every kind except `draw-text` requires -- so "nobody
     * set it" and "this command draws no characters" are one state, as they are for the
     * style and the colour. */
    PaintTextRunInit(&command->text);
    /* And no image, which is what every kind except `draw-image` requires, for the third time
     * the same rule: a command that names no resource and a command that was never filled in
     * about one are one state rather than two that could disagree. */
    PaintImageInit(&command->image);
}

int PaintDisplayCommandIsValid(const PaintDisplayCommand *command)
{
    if (!command) {
        return 0;
    }
    if (!PaintDisplayCommandKindIsKnown(command->kind)) {
        return 0;
    }
    if (command->kind == RoboPaintDisplayCmdNone) {
        /* A cleared record is a state, not a command. */
        return 0;
    }
    if (command->node == RoboLayoutNodeIdNone) {
        return 0;
    }
    /* A rectangle is a rectangle: an empty one is fine (a zero-height box is a statement
     * about the document), a negative extent is arithmetic that went wrong one level
     * down. */
    if (command->rect.width < 0 || command->rect.height < 0) {
        return 0;
    }
    if (!PaintClipIsValid(&command->clip)) {
        return 0;
    }
    /*
     * The **clip scope** rule (11.8), the record layer's rule one stage along: a command that
     * opens or closes a scope states that a boundary is in force, so a scope whose clip bounds
     * neither axis is a command whose fields disagree about what it is. Refused here rather
     * than emitted and skipped, and refused *before* the colour split so that the reason a
     * reader is given is the clip rather than the colour field a structural command leaves
     * cleared anyway.
     */
    if (PaintDisplayCommandKindIsClip(command->kind) &&
        !PaintClipIsActive(&command->clip)) {
        return 0;
    }
    /*
     * And the **opacity** rules (11.9), which are the clip rule's shape one field along plus the
     * range, and the range applies to **every** kind: an opacity outside `[0, 1]` is not a value a
     * rasteriser can apply, and a command carrying one would be a drawing whose strength its
     * consumer could not honour. A fully **opaque** scope command is refused for the same reason a
     * clip that bounds nothing is -- it states that a strength is in force and then states the
     * strength that changes nothing, which is a command a consumer could only skip.
     */
    if (!PaintOpacityIsValid(&command->opacity)) {
        return 0;
    }
    if (PaintDisplayCommandKindIsOpacity(command->kind) &&
        PaintOpacityIsOpaque(&command->opacity)) {
        return 0;
    }
    /*
     * The colour rule, split on `uses_color` rather than on *drawing* or on *marker*. A command
     * whose look is a colour must carry one a rasteriser can use; every other command -- a
     * structural one, and since 11.7 a `draw-image` -- must carry `none`. The split is on
     * `uses_color` because `draw-image` **is** a drawing and has no colour to offer: a rule stated
     * on `draws` would be asking it for one, which is exactly the shape `RoboPaintTypes.h` argues
     * against. `CONTENT` lands on the no-colour side either way, which is the case the older
     * comment about `is_marker` was there to protect.
     */
    if (!PaintDisplayCommandKindUsesColor(command->kind)) {
        if (command->color.kind != RoboCssColorNone) {
            return 0;
        }
        /*
         * And inside that side, the two kinds part company: a `draw-image` must carry a payload
         * this build may draw and no style or run, and a structural command must carry none of
         * the four. The image half is the text rule's shape for a payload that is not
         * characters, and both halves are refusals for the same reason -- a `draw-image` with no
         * resource is a drawing that draws nothing, and a marker naming one is a command carrying
         * a drawing it does not make.
         */
        if (command->kind == RoboPaintDisplayCmdDrawImage) {
            return PaintImageIsValid(&command->image) &&
                   command->style == RoboCssBorderStyleNone &&
                   !PaintTextRunIsValid(&command->text);
        }
        return command->style == RoboCssBorderStyleNone &&
               !PaintTextRunIsValid(&command->text) &&
               PaintImageIsCleared(&command->image);
    }
    if (!CssColorIsValid(command->color)) {
        return 0;
    }
    /*
     * The style rule, one field along from the colour's and enforced from the same side.
     * It is here for a reason the record layer cannot state for it: this artifact is
     * **render-ready**, which means a rasteriser must be able to produce the drawn shape
     * from the command alone -- so a `draw-border` that did not say *which* of the ten
     * styles it is would be a command its consumer could only guess at. A `draw-border`
     * therefore carries a style that generates a border, and a `draw-background` carries
     * `NONE`, because it has no side to style. `generates_border` rather than "not
     * `none`": a `hidden` border draws nothing, which is why RoboLayout gives such a side
     * a used width of zero and no command is built for it at all.
     */
    if (command->kind == RoboPaintDisplayCmdDrawBorder) {
        /*
         * And no image payload, which is the rule the structural branch states for the other
         * side of the split: a band that came with a resource would be a command carrying a
         * drawing it does not make. There is deliberately **no `draw-image` case above this
         * line** -- `draw-image` is not one of the kinds `uses_color` admits, so it was decided
         * in that branch, and a second check here would be a rule nothing could reach whose
         * disagreement with the first would be invisible.
         */
        return CssBorderStyleGeneratesBorder(command->style) &&
               PaintImageIsCleared(&command->image);
    }
    /*
     * And the text rule, the style rule's shape one field along and enforced from the same
     * side. A `draw-text` carries a **run** -- a non-empty span, a non-negative baseline
     * offset and font inputs that are a record -- because a command that draws characters
     * must say which ones and where the baseline is; every other drawing carries a cleared
     * one. Both halves are refusals: a `draw-text` with no characters is a drawing that
     * draws nothing, and a band that came with font inputs is a command carrying facts
     * about characters it does not have.
     */
    if (command->kind == RoboPaintDisplayCmdDrawText) {
        return command->style == RoboCssBorderStyleNone &&
               PaintTextRunIsValid(&command->text) &&
               PaintImageIsCleared(&command->image);
    }
    return command->style == RoboCssBorderStyleNone &&
           !PaintTextRunIsValid(&command->text) &&
           PaintImageIsCleared(&command->image);
}

int PaintDisplayCommandEqual(const PaintDisplayCommand *a,
                               const PaintDisplayCommand *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->kind == b->kind && a->node == b->node && a->depth == b->depth &&
           GfxRectEqual(a->rect, b->rect) &&
           PaintClipEqual(&a->clip, &b->clip) &&
           PaintOpacityEqual(&a->opacity, &b->opacity) &&
           CssColorEquals(a->color, b->color) && a->style == b->style &&
           PaintTextRunEqual(&a->text, &b->text) &&
           /* The image payload compares by **identity** rather than by address, which is the
            * reason `PaintImageEqual` is a door instead of four more comparisons here:
            * two lists built from one document must compare equal whatever the heap did. */
           PaintImageEqual(&a->image, &b->image);
}

PaintDisplayCommandKind MakePaintDisplayCommandKind(const PaintDisplayCommand *command)
{
    return command ? command->kind : RoboPaintDisplayCmdNone;
}

LayoutNodeId PaintDisplayCommandNode(const PaintDisplayCommand *command)
{
    return command ? command->node : RoboLayoutNodeIdNone;
}

CssBorderStyle PaintDisplayCommandStyle(const PaintDisplayCommand *command)
{
    return command ? command->style : RoboCssBorderStyleNone;
}

PaintTextRun PaintDisplayCommandText(const PaintDisplayCommand *command)
{
    PaintTextRun run;

    if (!command) {
        PaintTextRunInit(&run);
        return run;
    }
    return command->text;
}

PaintImage PaintDisplayCommandImage(const PaintDisplayCommand *command)
{
    PaintImage image;

    PaintImageInit(&image);
    if (!command || command->kind != RoboPaintDisplayCmdDrawImage) {
        /* The record layer's rule one artifact along: a kind that is not the kind that carries a
         * payload answers with a cleared one rather than with the field. */
        return image;
    }
    return command->image;
}

size_t PaintDisplayCommandDepth(const PaintDisplayCommand *command)
{
    return command ? command->depth : 0u;
}

LayoutRect PaintDisplayCommandRect(const PaintDisplayCommand *command)
{
    return command ? command->rect : MakeGfxRect(0, 0, 0, 0);
}

void PaintDisplayCommandClip(const PaintDisplayCommand *command,
                              PaintClip *out)
{
    if (!out) {
        return;
    }
    if (!command) {
        PaintClipInit(out);
        return;
    }
    *out = command->clip;
}

void PaintDisplayCommandOpacity(const PaintDisplayCommand *command,
                                       PaintOpacity *out)
{
    if (!out) {
        return;
    }
    if (!command) {
        /* Nothing in force, which is full strength -- the same answer an empty scope stack
         * gives. */
        PaintOpacityInit(out);
        return;
    }
    *out = command->opacity;
}

CssColor PaintDisplayCommandColor(const PaintDisplayCommand *command)
{
    return command ? command->color : CssColorNone();
}

int PaintDisplayCommandIsStructural(const PaintDisplayCommand *command)
{
    return command != NULL && PaintDisplayCommandKindIsStructural(command->kind);
}

int PaintDisplayCommandIsMarker(const PaintDisplayCommand *command)
{
    return command != NULL && PaintDisplayCommandKindIsMarker(command->kind);
}

int PaintDisplayCommandIsDrawing(const PaintDisplayCommand *command)
{
    return command != NULL && PaintDisplayCommandKindDraws(command->kind);
}

int PaintDisplayCommandIsClip(const PaintDisplayCommand *command)
{
    return command != NULL && PaintDisplayCommandKindIsClip(command->kind);
}

int PaintDisplayCommandIsOpacity(const PaintDisplayCommand *command)
{
    return command != NULL && PaintDisplayCommandKindIsOpacity(command->kind);
}

Result PaintDisplayCommandWrite(const PaintDisplayCommand *command,
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

    if (!command || !buffer || !outLength || capacity == 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "the command, a buffer and an out_length are required");
    }
    if (!PaintDisplayCommandKindIsKnown(command->kind) ||
        command->kind == RoboPaintDisplayCmdNone) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "not a command kind");
    }

    PaintWriterInit(&text, buffer, capacity);
    name = PaintDisplayCommandKindName(command->kind);
    PaintWriterString(&text, name);
    for (index = strlen(name); index < RoboPaintDisplayNameColumn; index++) {
        PaintWriterChar(&text, ' ');
    }

    PaintWriterString(&text, "node=");
    PaintWriterU64(&text, (uint64_t)command->node);
    PaintWriterString(&text, " depth=");
    PaintWriterU64(&text, (uint64_t)command->depth);

    /* The clip, as two modes with the interval appended only on an axis that has one:
     * `clip=hidden[0,100]/visible`. */
    PaintWriterString(&text, " clip=");
    PaintWriterString(&text, CssOverflowName(command->clip.horizontal.mode));
    if (PaintClipAxisIsActive(&command->clip.horizontal)) {
        PaintWriterChar(&text, '[');
        PaintWriterI64(&text, (int64_t)command->clip.horizontal.start);
        PaintWriterChar(&text, ',');
        PaintWriterI64(&text, (int64_t)command->clip.horizontal.end);
        PaintWriterChar(&text, ']');
    }
    PaintWriterChar(&text, '/');
    PaintWriterString(&text, CssOverflowName(command->clip.vertical.mode));
    if (PaintClipAxisIsActive(&command->clip.vertical)) {
        PaintWriterChar(&text, '[');
        PaintWriterI64(&text, (int64_t)command->clip.vertical.start);
        PaintWriterChar(&text, ',');
        PaintWriterI64(&text, (int64_t)command->clip.vertical.end);
        PaintWriterChar(&text, ']');
    }

    /* The **opacity in force** (11.9), beside the clip and for the reason the clip is beside
     * it: a command that produces visible pixels answers `how strongly` as well as `where`,
     * and a dump that omitted it would leave the one number a reader needs to explain a
     * faint rectangle the one number it does not show. */
    PaintWriterString(&text, " opacity=");
    PaintWriterNumber(&text, PaintOpacityValue(&command->opacity), 2u);

    /* The one rectangle the command acts on, printed the same way for every kind: the
     * whole point of a display command is that it carries no *other* rectangle to choose
     * between. */
    PaintWriterString(&text, " rect=");
    PaintWriterRect(&text, command->rect);

    /*
     * The colour, for the kinds that **have** one rather than for the kinds that draw, which
     * 11.7 is the phase that separates: a `draw-image` is a drawing whose look is its resource's
     * pixels, so a line that asked it for a colour would print nothing or a "none" a reader would
     * take for a fill.
     */
    if (PaintDisplayCommandKindUsesColor(command->kind)) {
        if (CssColorWrite(command->color, colorText, sizeof(colorText),
                                 &colorLength, NULL) == RoboOk) {
            PaintWriterString(&text, " color=");
            PaintWriterString(&text, colorText);
        }
    }
    /* A border band says which of the ten styles it is; nothing else has one to say. */
    if (command->kind == RoboPaintDisplayCmdDrawBorder) {
        PaintWriterString(&text, " style=");
        PaintWriterString(&text, CssBorderStyleName(command->style));
    }
    /*
     * An image command prints the **resource's identity**, the source rectangle it samples and
     * the resource's own dimensions -- the same facts the record's writer prints, and no address,
     * for the same reason: two runs of one document must produce the same lines. The pixels are
     * not printed: a command names them, and naming them is what a reader diffs.
     */
    if (command->kind == RoboPaintDisplayCmdDrawImage) {
        PaintWriterString(&text, " resource=");
        PaintWriterU64(&text, (uint64_t)PaintImageIdentity(&command->image));
        PaintWriterString(&text, " src=");
        PaintWriterI64(&text, (int64_t)command->image.source.x);
        PaintWriterChar(&text, ',');
        PaintWriterI64(&text, (int64_t)command->image.source.y);
        PaintWriterChar(&text, ',');
        PaintWriterI64(&text, (int64_t)command->image.source.width);
        PaintWriterChar(&text, 'x');
        PaintWriterI64(&text, (int64_t)command->image.source.height);
        PaintWriterString(&text, " source_size=");
        PaintWriterU64(&text, (uint64_t)command->image.width);
        PaintWriterChar(&text, 'x');
        PaintWriterU64(&text, (uint64_t)command->image.height);
    }
    /* A text command prints the span, the baseline and the font inputs; its characters
     * are printed by the list dump, which has the pool (see RoboPaintDump.h). */
    if (command->kind == RoboPaintDisplayCmdDrawText) {
        PaintWriterString(&text, " code_points=");
        PaintWriterU64(&text, (uint64_t)command->text.span.count);
        PaintWriterString(&text, " baseline=");
        PaintWriterI64(&text, (int64_t)command->text.baselineOffset);
        PaintWriterString(&text, " font=");
        PaintWriterNumber(&text, command->text.font.fontSizePx, 2u);
        PaintWriterChar(&text, '/');
        PaintWriterI64(&text, (int64_t)command->text.font.fontWeight);
        PaintWriterChar(&text, '/');
        PaintWriterString(&text, command->text.font.italic ? "italic" : "upright");
        PaintWriterChar(&text, '/');
        PaintWriterString(&text,
                                 command->text.font.direction ==
                                         RoboLayoutTextDirectionRtl ? "rtl" : "ltr");
    }

    *outLength = text.length;
    if (!PaintWriterFits(&text)) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboNotSupported,
                               "the command text does not fit the buffer");
    }
    return RoboOk;
}
