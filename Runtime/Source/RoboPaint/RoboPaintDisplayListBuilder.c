/*
 * RoboDisplay list builder (Phase 11.3–11.8) — see RoboPaintDisplayListBuilder.h.
 *
 * The conversion layer and the structural check, and the two are deliberately one pass
 * rather than two: a record is validated in the same step it is converted, so a record
 * that is out of place is refused before any command derived from it is appended -- and
 * the output list is cleared on refusal, so a caller never holds a partially converted
 * sequence.
 *
 * The structural check is a small state machine over a stack of the currently open boxes.
 * The reason it is a stack rather than a running counter is the *node identity* rule: a
 * box's content boundary and its closing marker must name the box its opening marker
 * named, and "which box is this depth?" is only answerable by remembering the open boxes.
 * The stack is grown on demand, and it is why the builder is a stateful object rather than
 * a function -- the capacity survives a `reset`, exactly as a paint context's reuse does.
 *
 * **Since 11.8 there is a second stack beside it**, of the clip scopes currently open. It is
 * the one structural fact the box stack cannot carry: a box's contents are bounded by the
 * clip that box put in force, so a command inside an open scope must carry that scope's
 * boundary rather than any boundary of its own -- and "what is in force here?" is answered by
 * the innermost push, which is a fact about the *sequence* rather than about the box. The two
 * stacks move together (a scope opens after a box's content boundary and closes before its end
 * marker) but they are not the same stack, and merging them would mean a box that opens no
 * scope still had to carry one.
 *
 * Nothing here performs layout or touches the tree: the records are read through the paint
 * list's `const` doors and the conversion uses only what a record already carries (plus
 * `PaintOperationSideBand`, the one derivation the record leaves to its consumer).
 */

#include "RoboPaint/RoboPaintDisplayListBuilder.h"

#include "RoboPaint/RoboPaintClip.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboPaint/RoboPaintOperation.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/**
 * One open box: its node id, whether its content boundary has passed, and which of the two
 * scopes it has open.
 *
 * **Two flags rather than one**, because a box can open both and they are two different
 * facts: `scopeOpen` is the clip scope around its *contents* (11.8) and `opacityOpen` is
 * the opacity scope around the box *itself* (11.9). They also live at different points in
 * the box's life -- the clip pair opens after the content boundary and the opacity pair
 * opens before the box's first decoration -- so a single flag would make one of the two
 * checks read the other's state.
 */
typedef struct displayOpenBox {
    LayoutNodeId node;
    unsigned char contentSeen;
    unsigned char scopeOpen;
    unsigned char opacityOpen;
    /** Whether anything the box draws for **itself** has been emitted yet. */
    unsigned char painted;
} DisplayOpenBox;

struct PaintDisplayListBuilder {
    DisplayOpenBox *open;
    size_t openCount;
    size_t openCapacity;
    /** The clip scopes currently open, innermost last. */
    PaintClipStack *clips;
    /** The opacity scopes currently open, innermost last. */
    PaintOpacityStack *opacities;
};

/* ------------------------------------------------------------------ */
/* The open-box stack                                                  */
/* ------------------------------------------------------------------ */

static Result DisplayStackReserve(PaintDisplayListBuilder *builder,
                                           size_t capacity,
                                           PaintDiag *diag)
{
    DisplayOpenBox *grown;

    if (capacity <= builder->openCapacity) {
        return RoboOk;
    }
    grown = (DisplayOpenBox *)Realloc(builder->open,
                                               capacity * sizeof(*grown));
    if (!grown) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not grow the builder's open-box stack");
    }
    builder->open = grown;
    builder->openCapacity = capacity;
    return RoboOk;
}

static Result DisplayStackPush(PaintDisplayListBuilder *builder,
                                        LayoutNodeId node,
                                        PaintDiag *diag)
{
    Result result;

    if (builder->openCount == builder->openCapacity) {
        size_t wanted = builder->openCapacity < 1u ? 8u : builder->openCapacity * 2u;

        result = DisplayStackReserve(builder, wanted, diag);
        if (result != RoboOk) {
            return result;
        }
    }
    builder->open[builder->openCount].node = node;
    builder->open[builder->openCount].contentSeen = 0u;
    builder->open[builder->openCount].scopeOpen = 0u;
    builder->open[builder->openCount].opacityOpen = 0u;
    builder->open[builder->openCount].painted = 0u;
    builder->openCount++;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

Result PaintDisplayListBuilderCreate(PaintDisplayListBuilder **outBuilder,
                                               PaintDiag *diag)
{
    PaintDisplayListBuilder *builder;

    if (!outBuilder) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "out_builder is NULL");
    }
    *outBuilder = NULL;
    builder = (PaintDisplayListBuilder *)RoboCalloc(1u, sizeof(*builder));
    if (!builder) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not allocate the display list builder");
    }
    /* No first block: the stack grows on the first box, and an empty build never allocates
     * one -- which is the honest baseline for a builder that is reused across builds. */
    builder->clips = NULL;
    builder->opacities = NULL;
    if (PaintClipStackCreate(&builder->clips, NULL) != RoboOk) {
        RoboFree(builder);
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not allocate the builder's clip scope stack");
    }
    if (PaintOpacityStackCreate(&builder->opacities, NULL) != RoboOk) {
        PaintClipStackDestroy(builder->clips);
        RoboFree(builder);
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not allocate the builder's opacity scope stack");
    }
    *outBuilder = builder;
    return RoboOk;
}

void PaintDisplayListBuilderDestroy(PaintDisplayListBuilder *builder)
{
    if (!builder) {
        return;
    }
    PaintClipStackDestroy(builder->clips);
    PaintOpacityStackDestroy(builder->opacities);
    RoboFree(builder->open);
    RoboFree(builder);
}

void PaintDisplayListBuilderReset(PaintDisplayListBuilder *builder)
{
    if (!builder) {
        return;
    }
    /* The capacity survives: a builder reused per frame reuses its stack -- and so does a
     * scope that was open when a build was refused, which is why **both** scope stacks are
     * cleared here as well as at the start of every build. A second stack that were left
     * holding a scope would make the next build's first drawing disagree with a strength
     * nothing in that build had established. */
    builder->openCount = 0u;
    PaintClipStackReset(builder->clips);
    PaintOpacityStackReset(builder->opacities);
}

/* ------------------------------------------------------------------ */
/* Conversion                                                          */
/* ------------------------------------------------------------------ */

/** Fill @p out with the display command @p operation resolves to.
 *
 * @param textTarget the output list's text pool. A `TEXT` record's characters are copied
 *                    into it here, because a display command names characters in **its
 *                    own** list's pool -- the record list's pool is the other list's, and a
 *                    span that still pointed there would be a command that stopped meaning
 *                    anything the moment that list was cleared. Passing NULL is legal and
 *                    makes a text record resolve to an error, which only a caller that
 *                    built a list with no pool could do.
 *
 * @return RoboOk; RoboError for an operation this build has no rule for, a border whose
 *         band cannot be derived, or a text run whose characters cannot be copied.
 */
static Result DisplayCommandFor(const PaintOperation *operation,
                                         const PaintTextPool *textSource,
                                         PaintTextPool *textTarget,
                                         PaintDisplayCommand *out,
                                         PaintDiag *diag)
{
    PaintDisplayCommandInit(out);
    out->node = operation->node;
    out->depth = operation->depth;
    /*
     * Both states in force carry across for **every** kind, scope kinds included -- a push
     * command's own strength is the strength it establishes, and the pair's second half carries
     * the same one so that a consumer can check it against the first. See the scope comments in
     * this file for what each of the two is checked against.
     */
    out->clip = operation->clip;
    out->opacity = operation->opacity;

    switch (operation->kind) {
    case RoboPaintOpBeginBox:
        out->kind = RoboPaintDisplayCmdBeginNode;
        out->rect = operation->boxes.border;
        /* Structural: the colour is `none`, which `init` already left it as. */
        return RoboOk;
    case RoboPaintOpBackground:
        out->kind = RoboPaintDisplayCmdDrawBackground;
        out->rect = operation->boxes.border;
        out->color = operation->color;
        return RoboOk;
    case RoboPaintOpBorderTop:
    case RoboPaintOpBorderRight:
    case RoboPaintOpBorderBottom:
    case RoboPaintOpBorderLeft:
        out->kind = RoboPaintDisplayCmdDrawBorder;
        if (PaintOperationSideBand(operation, &out->rect, diag) != RoboOk) {
            return RoboError;
        }
        /* The **side** is consumed into the band rectangle and the four kinds collapse into
         * one -- but the **style** is not consumed by anything: it is the shape of what is
         * inside that rectangle, so it travels with the command (11.5). */
        out->color = operation->color;
        out->style = operation->style;
        return RoboOk;
    case RoboPaintOpText:
        out->kind = RoboPaintDisplayCmdDrawText;
        /* A run has no insets, so the one rectangle it acts on is any of the three the
         * operation carries -- they are equal by construction (`PaintBoxesOf`
         * declines a text run's inset doors and says so with `insetsMeasured`). */
        out->rect = operation->boxes.border;
        out->color = operation->color;
        out->text = operation->text;
        /* The characters are renumbered into this list's pool. The run's other two facts
         * -- the baseline offset and the font inputs -- are values and copy straight
         * across, so "the size it was measured at is the size it will be drawn at" is a
         * copy rather than an agreement to maintain. */
        if (PaintTextPoolCopy(textTarget, textSource, operation->text.span,
                                      &out->text.span, diag) != RoboOk) {
            return RoboError;
        }
        return RoboOk;
    case RoboPaintOpImage:
        /*
         * The image commands (11.7), and the one derivation this build performs for them is the
         * **destination rectangle**: an image record carries the box's three rectangles and this
         * artifact carries one, so the content box is what survives -- because an image *is* its
         * element's content, and the content box is where content goes. That is the same kind of
         * conversion the border band above performs on a side, and it is done once here so that a
         * rasteriser never has to choose between three rectangles.
         *
         * The payload travels as it is: the resource, the source rectangle in image pixels and
         * the resource's facts are all values, and none of them is derived from anything above.
         * The command this produces borrows the resource; the **list** takes the reference that
         * keeps it alive (`PaintDisplayListAppend`), which is what lets the record list be
         * cleared or destroyed while this list keeps drawing.
         */
        out->kind = RoboPaintDisplayCmdDrawImage;
        out->rect = operation->boxes.content;
        out->image = operation->image;
        return RoboOk;
    case RoboPaintOpContent:
        out->kind = RoboPaintDisplayCmdContent;
        out->rect = operation->boxes.content;
        return RoboOk;
    case RoboPaintOpPushClip:
    case RoboPaintOpPopClip:
        out->kind = operation->kind == RoboPaintOpPushClip
                            ? RoboPaintDisplayCmdPushClip
                            : RoboPaintDisplayCmdPopClip;
        /*
         * The rectangle a clip command acts on is the box's **padding box**: the boundary the
         * box's own `overflow` resolves to, and the answer to "where did this scope come from?"
         * when a dump is being read. It is deliberately *not* the effective clip's rectangle,
         * and the reason is that the effective rectangle does not always exist: one bounded
         * axis is a legal document (`overflow-x: hidden` beside `overflow-y: visible`), so
         * `PaintClipRect` refuses exactly the case where a reader would most like a
         * rectangle and a command that answered anyway would be inventing one. The boundary
         * itself travels in the `clip` field, where the axis split survives.
         */
        out->rect = operation->boxes.padding;
        return RoboOk;
    case RoboPaintOpPushOpacity:
    case RoboPaintOpPopOpacity:
        out->kind = operation->kind == RoboPaintOpPushOpacity
                            ? RoboPaintDisplayCmdPushOpacity
                            : RoboPaintDisplayCmdPopOpacity;
        /*
         * The rectangle is the box's **border box**, and it is the same box the clip pair names
         * for the *other* of its two rectangles -- deliberately. A clip command's rectangle is
         * the padding box because that is the boundary the box's overflow resolves to ("where
         * did this scope come from?"); an opacity command's is the border box because the
         * scope belongs to the box *itself*, decorations included, and the border box is the
         * whole of what it tints. A reader comparing the two commands of one box's two scopes
         * sees them name its two different rectangles, and each name is the true answer to its
         * own question.
         */
        out->rect = operation->boxes.border;
        return RoboOk;
    case RoboPaintOpEndBox:
        out->kind = RoboPaintDisplayCmdEndNode;
        out->rect = operation->boxes.border;
        return RoboOk;
    default:
        return RoboError;
    }
}

/* ------------------------------------------------------------------ */
/* The structural check and the build                                  */
/* ------------------------------------------------------------------ */

/** Refuse the build: empty the output list and report why. */
static Result DisplayRefuse(PaintDisplayList *outList,
                                    PaintDiag *diag,
                                    const char *message)
{
    PaintDisplayListClear(outList);
    return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboError, message);
}

/*
 * The **opacity scope** (11.9), checked before the clip's because the two pairs sit at
 * different points in a box's life and each check depends on where its own pair sits.
 *
 * **Why the pair is here and not after the content boundary.** A clip belongs to a box's
 * *contents* -- CSS paints the box's own background under its parent's boundary -- so the clip
 * pair opens when the contents begin. An opacity belongs to the box *itself*: CSS paints the
 * box's own background and borders at its own `opacity`, so the pair opens before the box's
 * first decoration. That is why the push requires `!painted && !contentSeen` while the clip's
 * push requires `contentSeen`, and why the two checks are near mirror images of each other
 * rather than one rule with a flag.
 *
 * Three things are checked on a push, and each is a way a list can lie about the document it
 * came from: the scope names the innermost **open** box and sits at that box's depth (so the
 * box it belongs to is the box it names, not a child that happens to be open); it establishes
 * a strength **different** from the one in force (a scope that establishes nothing is a command
 * a consumer could only skip); and it does not *raise* the strength (opacity composes by
 * multiplication, so a scope an ancestor reduced can only reduce further -- a push that raised
 * it would be a box whose `opacity` cancelled an ancestor's, which CSS has no way to express).
 *
 * The pop is checked against the innermost open scope rather than against a value the pop
 * carries alone, so the pair is provably paired: a pop with nothing open, a pop that closes a
 * scope another box opened, and a pop whose strength is not the one its push carried are three
 * separate refusals rather than one that happens to catch all three.
 */
static Result DisplayOpacityScopes(PaintDisplayListBuilder *builder,
                                            const PaintOperation *operation,
                                            PaintDisplayListStats *stats,
                                            const char **refusal)
{
    DisplayOpenBox *top;
    PaintOpacity inForce;

    *refusal = NULL;
    if (builder->openCount == 0u) {
        *refusal = "an opacity scope belongs to no open box";
        return RoboError;
    }
    top = &builder->open[builder->openCount - 1u];
    if (operation->node != top->node || operation->depth != builder->openCount - 1u) {
        *refusal = "an opacity scope names a box other than the innermost open one";
        return RoboError;
    }
    PaintOpacityStackCurrent(builder->opacities, &inForce);

    if (operation->kind == RoboPaintOpPushOpacity) {
        if (top->opacityOpen) {
            *refusal = "a box opens two opacity scopes";
            return RoboError;
        }
        if (top->painted || top->contentSeen) {
            *refusal = "an opacity scope opens after the box it belongs to has begun drawing";
            return RoboError;
        }
        if (PaintOpacityEqual(&operation->opacity, &inForce)) {
            *refusal = "an opacity scope establishes nothing new";
            return RoboError;
        }
        if (PaintOpacityValue(&operation->opacity) > PaintOpacityValue(&inForce)) {
            *refusal = "an opacity scope is stronger than the strength it is inside";
            return RoboError;
        }
        if (stats != NULL) {
            stats->opacityScopes++;
        }
        return RoboOk;
    }

    if (!top->opacityOpen) {
        *refusal = "a box closes an opacity scope it did not open";
        return RoboError;
    }
    if (!PaintOpacityEqual(&operation->opacity, &inForce)) {
        *refusal = "an opacity scope closes with a strength other than the one it opened";
        return RoboError;
    }
    return RoboOk;
}

/**
 * Non-zero when @p narrow is at least as tight as @p outer on **every axis @p outer bounds**.
 *
 * The one property of a nested scope that can be checked without geometry: a scope is entered
 * to constrain the drawing inside it, so a push whose interval is *wider* than the boundary
 * already in force on an axis is a record the traversal cannot produce -- it would be a box
 * whose `overflow` freed an axis an ancestor had bounded, which CSS has no way to express and
 * this build has no way to reach. An axis @p outer leaves open constrains nothing, so @p narrow
 * is free to bound it or not.
 *
 * The containment is per axis and inclusive at both ends: an interval equal to the enclosing one
 * on that axis narrowing the *other* axis is a real scope (a document that hides `y` inside one
 * that hides `x`), and refusing it would refuse a document the traversal does produce. The
 * equal-on-both-axes case is refused by the caller, because a scope that changes nothing is a
 * command a consumer can only skip.
 *
 * Two cases that read like violations and are not, both of which cost a debug cycle to get right:
 *
 *   **an inset bound**   `outer = [0, 200]` with `narrow = [20, 200]` is the *ordinary* case, a
 *                        child box inset from its parent's edge. Containment is
 *                        `outer.start <= narrow.start && narrow.end <= outer.end`; the spelled
 *                        `narrow.start > outer.start` is the opposite predicate, and `20 > 0` is
 *                        exactly what an inset child produces.
 *
 *   **an empty narrow**  a scope whose intersection came out disjoint is canonicalized to
 *                        `[start, start]` -- see `PaintClipIntersectAxis` -- and a zero-length
 *                        interval at `start` sits *outside* an outer `[0, 200]`'s start, so the
 *                        containment test throws it out. It must not: an empty scope bounds
 *                        nothing, so it cannot bound too much. It is accepted here and its
 *                        emptiness does the suppressing downstream.
 */
static int DisplayClipNarrows(const PaintClip *narrow,
                                const PaintClip *outer)
{
    if (PaintClipIsEmpty(narrow)) {
        return 1;
    }
    if (PaintClipAxisIsActive(&outer->horizontal)) {
        if (!PaintClipAxisIsActive(&narrow->horizontal)) {
            return 0;
        }
        if (narrow->horizontal.start < outer->horizontal.start ||
            narrow->horizontal.end > outer->horizontal.end) {
            return 0;
        }
    }
    if (PaintClipAxisIsActive(&outer->vertical)) {
        if (!PaintClipAxisIsActive(&narrow->vertical)) {
            return 0;
        }
        if (narrow->vertical.start < outer->vertical.start ||
            narrow->vertical.end > outer->vertical.end) {
            return 0;
        }
    }
    return 1;
}

Result PaintDisplayListBuilderBuild(PaintDisplayListBuilder *builder,
                                              const PaintList *records,
                                              PaintDisplayList *outList,
                                              PaintDisplayListStats *outStats,
                                              PaintDiag *diag)
{
    PaintDisplayListStats stats;
    PaintTextPool *textTarget;
    const PaintTextPool *textSource;
    size_t count;
    size_t index;

    if (!builder || !records || !outList) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "the builder, the records and the output list are required");
    }
    /* The two pools a text run crosses between: the record list's, which holds the
     * characters the traversal read, and the output list's, which will hold the copy this
     * artifact owns. Both are borrowed for the duration of the build and neither is
     * retained -- an output list that kept a pointer to the record list's pool would be
     * exactly the dangling reference the pool exists to make impossible. */
    textTarget = PaintDisplayListTextPool(outList);
    textSource = PaintListTextPool(records);

    /*
     * The list is cleared first, so a caller may reuse one display list across builds and a
     * refused build hands back an **empty** list rather than the previous one -- the paint
     * pass's rule at the same boundary. It clears the list's **text pool** with it, which is
     * what makes the copy below the only characters this artifact holds: were the pool kept
     * across a build, the second build's runs would be appended after the first's and a
     * reader would be able to reach code points no command names.
     */
    PaintDisplayListClear(outList);
    builder->openCount = 0u;
    PaintClipStackReset(builder->clips);
    PaintOpacityStackReset(builder->opacities);
    memset(&stats, 0, sizeof(stats));

    count = PaintListCount(records);
    stats.records = count;

    for (index = 0u; index < count; index++) {
        PaintOperation operation;
        PaintDisplayCommand command;

        if (PaintListAt(records, index, &operation) != RoboOk) {
            /* Unreachable: the index came from the list's own count. Reported rather than
             * skipped so a future list that can lose a record says so here. */
            return DisplayRefuse(outList, diag, "the record list changed under the build");
        }

        /*
         * The **opacity scope** (11.9), handled before the clip's -- see `DisplayOpacityScopes`
         * for why the two pairs sit at different points in a box's life -- and before the box
         * chain for the reason a scope is not a box: it opens and closes around one box's own
         * painting and contents, and the box that declared the opacity is the innermost open box
         * in both cases.
         *
         * The push's strength is pushed onto the builder's opacity stack here, at the push, and
         * the pop's is compared against it at the pop -- so `inForce` for a drawing the box
         * makes is the box's own effective opacity rather than an ancestor's, which is the same
         * arrangement the clip stack has one field along.
         */
        if (PaintOpKindIsOpacity(operation.kind)) {
            const char *refusal = NULL;
            Result scoped = DisplayOpacityScopes(builder, &operation, &stats, &refusal);

            if (scoped != RoboOk) {
                return DisplayRefuse(outList, diag, refusal);
            }
            if (operation.kind == RoboPaintOpPushOpacity) {
                if (PaintOpacityStackPush(builder->opacities, &operation.opacity, diag) !=
                    RoboOk) {
                    PaintDisplayListClear(outList);
                    return diag ? diag->code : RoboOutOfMemory;
                }
                builder->open[builder->openCount - 1u].opacityOpen = 1u;
            } else {
                (void)PaintOpacityStackPop(builder->opacities, NULL);
                builder->open[builder->openCount - 1u].opacityOpen = 0u;
            }
            if (DisplayCommandFor(&operation, textSource, textTarget, &command, diag) !=
                RoboOk) {
                return DisplayRefuse(outList, diag,
                                      "an opacity scope resolves to no display command");
            }
            if (PaintDisplayListAppend(outList, &command, diag) != RoboOk) {
                PaintDisplayListClear(outList);
                return diag ? diag->code : RoboError;
            }
            continue;
        }

        /*
         * The **clip scope** (11.8), handled before the box chain because a scope is not a
         * box: it opens and closes *around* one box's contents, and the box that declared the
         * overflow is the innermost open box in both cases.
         *
         * Both halves name that box and carry its own depth, exactly as the box's `CONTENT`
         * does, so a scope can be checked against the box it belongs to rather than trusted:
         * a scope emitted between two of a box's own records, or after its `END_BOX`, or inside
         * a *child* that happens to be open, is a sequence no traversal of this build produces
         * and is refused here.
         *
         * The push is then checked against what is already in force -- a scope that establishes
         * nothing, or one *wider* than the boundary it is inside, is refused (the rule the
         * in-scope check below then relies on) -- and the pop against the innermost open scope,
         * so the pair is provably paired: a pop with no scope, a pop that closes a scope another
         * box opened, and a pop whose boundary is not the one its push carried are three
         * separate refusals rather than one that happens to catch all three.
         */
        if (PaintOpKindIsClip(operation.kind)) {
            DisplayOpenBox *top;
            PaintClip inForce;

            if (builder->openCount == 0u) {
                return DisplayRefuse(outList, diag, "a clip scope belongs to no open box");
            }
            top = &builder->open[builder->openCount - 1u];
            if (operation.node != top->node || operation.depth != builder->openCount - 1u) {
                return DisplayRefuse(outList, diag,
                                      "a clip scope names a box other than the innermost "
                                      "open one");
            }
            if (!top->contentSeen) {
                return DisplayRefuse(outList, diag,
                                      "a clip scope precedes its box's content boundary");
            }
            PaintClipStackCurrent(builder->clips, &inForce);
            if (operation.kind == RoboPaintOpPushClip) {
                if (top->scopeOpen) {
                    return DisplayRefuse(outList, diag, "a box opens two clip scopes");
                }
                if (PaintClipEqual(&operation.clip, &inForce)) {
                    return DisplayRefuse(outList, diag,
                                          "a clip scope establishes nothing new");
                }
                if (!DisplayClipNarrows(&operation.clip, &inForce)) {
                    return DisplayRefuse(outList, diag,
                                          "a clip scope is wider than the boundary it is "
                                          "inside");
                }
                if (PaintClipStackPush(builder->clips, &operation.clip, diag) != RoboOk) {
                    PaintDisplayListClear(outList);
                    return diag ? diag->code : RoboOutOfMemory;
                }
                top->scopeOpen = 1u;
                stats.clipScopes++;
            } else {
                if (!top->scopeOpen) {
                    return DisplayRefuse(outList, diag,
                                          "a box closes a clip scope it did not open");
                }
                if (!PaintClipEqual(&operation.clip, &inForce)) {
                    return DisplayRefuse(outList, diag,
                                          "a clip scope closes with a boundary other than the "
                                          "one it opened");
                }
                (void)PaintClipStackPop(builder->clips, NULL);
                top->scopeOpen = 0u;
            }
            if (DisplayCommandFor(&operation, textSource, textTarget, &command, diag) !=
                RoboOk) {
                return DisplayRefuse(outList, diag,
                                      "a clip scope resolves to no display command");
            }
            if (PaintDisplayListAppend(outList, &command, diag) != RoboOk) {
                PaintDisplayListClear(outList);
                return diag ? diag->code : RoboError;
            }
            continue;
        }

        /*
         * Everything else is inside whatever scope is open, and **must carry its boundary**.
         * This is the rule the clip commands exist to make checkable: without it a push and a
         * pop would be two structural commands a consumer could only skip, and the boundary a
         * drawing is bounded by would still be a field nothing cross-checks.
         *
         * The check is made *only* when a scope is open, and that asymmetry is deliberate: a
         * list the traversal produced always has a scope open whenever a clip is in force, so
         * the check catches every disagreement that can occur -- while a hand-built list with
         * no clip records at all keeps working exactly as it did before this phase, its own
         * commands' boundaries being the only statement of clipping it has. Requiring a scope
         * for every bounded command would have refused a list that is merely *literal* about
         * its clip rather than structured by one.
         */
        if (!PaintClipStackIsEmpty(builder->clips)) {
            PaintClip inForce;

            PaintClipStackCurrent(builder->clips, &inForce);
            if (!PaintClipEqual(&operation.clip, &inForce)) {
                return DisplayRefuse(outList, diag,
                                      "a command inside a clip scope carries a different "
                                      "boundary");
            }
        }
        /*
         * And the same rule for the strength in force (11.9), with the same asymmetry and for
         * the same reason: a list the traversal produced always has an opacity scope open
         * whenever a strength other than full is in force, so the check catches every
         * disagreement that can occur -- while a hand-built list with no opacity records at all
         * keeps working, its own commands' strengths being the only statement of opacity it has.
         */
        if (!PaintOpacityStackIsEmpty(builder->opacities)) {
            PaintOpacity inForce;

            PaintOpacityStackCurrent(builder->opacities, &inForce);
            if (!PaintOpacityEqual(&operation.opacity, &inForce)) {
                return DisplayRefuse(outList, diag,
                                      "a command inside an opacity scope carries a "
                                      "different strength");
            }
        }

        /*
         * The structural check, kind by kind. `openCount` is the number of boxes
         * currently open, so a box that nests directly inside them has depth
         * `openCount`, and a record that belongs to the innermost open box has depth
         * `openCount - 1`.
         *
 * A **text run** is neither, and the difference is the whole of 11.6's shape in
 * this file: it is a **child** of the box whose element owns the text node, so its
 * depth is one past the innermost open box and its node is a *different* object --
 * but it opens no box of its own, because a run is not one (`RoboPaintTypes.h`).
 * So it is checked as a child and the stack is left alone, and the fact that it
 * follows the box's content boundary is required exactly as it is for a nested box:
 * a run emitted before its parent's own painting had finished would be a run no
 * traversal of this build can produce.
 *
 * An **image** (11.7) takes the ordinary path below -- the innermost open box's depth and
 * node, and the rule that it precedes that box's content boundary -- and the check is not
 * special-cased for it on purpose: those are exactly the facts a replaced element's drawing has,
 * so a site that carved out an exception for images would be stating the rule twice and
 * differently. It is the *traversal* that places the record (after the box's decorations, before
 * its `CONTENT`), and this file's job is to refuse a sequence that does not hold. The same is
 * true of the opacity scope's `painted` flag below: an image is a drawing the box made for
 * itself, so a `PUSH_OPACITY` after one is refused there automatically.
 */
        if (operation.kind == RoboPaintOpBeginBox) {
            if (operation.depth != builder->openCount) {
                return DisplayRefuse(outList, diag,
                                      "a begin-box nests where its depth says it does not");
            }
            if (DisplayStackPush(builder, operation.node, diag) != RoboOk) {
                PaintDisplayListClear(outList);
                return diag ? diag->code : RoboOutOfMemory;
            }
        } else {
            DisplayOpenBox *top;

            if (builder->openCount == 0u) {
                return DisplayRefuse(outList, diag, "a record belongs to no open box");
            }
            top = &builder->open[builder->openCount - 1u];
            if (operation.kind == RoboPaintOpText) {
                /* A text child: one deeper than the box that owns it, naming itself rather
                 * than that box, and inside the box's contents. Nothing is pushed, because
                 * a run has nothing to put anything in. Its depth is checked *before* the
                 * generic rule below, because that rule is what every record owned by the
                 * innermost box obeys and a run is owned a level down from it. */
                if (operation.depth != builder->openCount) {
                    return DisplayRefuse(outList, diag,
                                          "a text run's depth is not its parent box's plus "
                                          "one");
                }
                if (operation.node == top->node) {
                    return DisplayRefuse(outList, diag,
                                          "a text run names the box that owns it");
                }
                if (!top->contentSeen) {
                    return DisplayRefuse(outList, diag,
                                          "a text run precedes its parent box's content "
                                          "boundary");
                }
                if (DisplayCommandFor(&operation, textSource, textTarget, &command,
                                        diag) != RoboOk) {
                    return DisplayRefuse(outList, diag,
                                          "a text run's characters could not be copied");
                }
                if (PaintDisplayListAppend(outList, &command, diag) != RoboOk) {
                    PaintDisplayListClear(outList);
                    return diag ? diag->code : RoboError;
                }
                stats.texts++;
                continue;
            }
            if (operation.depth != builder->openCount - 1u) {
                return DisplayRefuse(outList, diag,
                                      "a record's depth is not its innermost box's");
            }
            if (operation.node != top->node) {
                return DisplayRefuse(outList, diag,
                                      "a record names a node other than its innermost box's");
            }

            switch (operation.kind) {
            case RoboPaintOpContent:
                if (top->contentSeen) {
                    return DisplayRefuse(outList, diag,
                                          "a box passes its content boundary twice");
                }
                top->contentSeen = 1u;
                break;
            case RoboPaintOpEndBox:
                if (!top->contentSeen) {
                    return DisplayRefuse(outList, diag,
                                          "a box closes before its content boundary");
                }
                /* A box's opacity scope closes before its marker -- the traversal emits the
                 * pop and then the `END_BOX` -- so a box that closes while one is still open
                 * is a box whose own strength leaked onto whatever follows it. The box stack
                 * alone would not catch it: the frame is about to be discarded, and the scope
                 * it left open is a command in the list with no matching pop until the end of
                 * the build, which is a different and later failure. */
                if (top->opacityOpen) {
                    return DisplayRefuse(outList, diag,
                                          "a box closes with an opacity scope still open");
                }
                builder->openCount--;
                break;
            default:
                /* A drawing record. It belongs to the box's own painting, which is over at
                 * its content boundary. */
                if (top->contentSeen) {
                    return DisplayRefuse(outList, diag,
                                          "a decoration follows its box's content boundary");
                }
                if (!PaintOpKindDraws(operation.kind)) {
                    return DisplayRefuse(outList, diag, "an operation this build cannot "
                                                          "convert");
                }
                /* The box has begun drawing for itself, which is what an opacity push may not
                 * follow -- see `DisplayOpacityScopes`. */
                top->painted = 1u;
                break;
            }
        }

        if (DisplayCommandFor(&operation, textSource, textTarget, &command, diag) !=
            RoboOk) {
            return DisplayRefuse(outList, diag,
                                  "an operation resolves to no display command");
        }
        if (PaintDisplayListAppend(outList, &command, diag) != RoboOk) {
            PaintDisplayListClear(outList);
            return diag ? diag->code : RoboError;
        }

        /* The counters, kept beside the append so the stats describe exactly what the list
         * holds. */
        if (command.kind == RoboPaintDisplayCmdBeginNode) {
            stats.nodes++;
            if (operation.depth > (size_t)stats.maxDepth) {
                stats.maxDepth = (uint32_t)operation.depth;
            }
        } else if (command.kind == RoboPaintDisplayCmdDrawBackground) {
            stats.backgrounds++;
        } else if (command.kind == RoboPaintDisplayCmdDrawBorder) {
            stats.borders++;
        } else if (command.kind == RoboPaintDisplayCmdDrawImage) {
            stats.images++;
        }
    }

    if (builder->openCount != 0u) {
        return DisplayRefuse(outList, diag, "the record list does not close every box it "
                                              "opens");
    }
    /*
     * A scope left open at the end of a build is checked **here** as well as by the box stack,
     * and it is not the same failure: a box stack that came back to zero with a scope still
     * open would be a list whose last command is inside a boundary nothing closes -- a
     * consumer replaying it would end with a clip in force it never popped. The traversal
     * cannot produce it (a scope closes with the box that opened it) and a hand-built list
     * can, so it is reported rather than assumed.
     */
    if (!PaintClipStackIsEmpty(builder->clips)) {
        return DisplayRefuse(outList, diag, "the record list does not close every clip scope "
                                              "it opens");
    }
    /* The same check for the strength (11.9), and here the failure is sharper than the clip's:
     * a strength left in force would make every drawing after it faint for a reason no command
     * in the list states. The per-box check above catches a box that closed with one open; this
     * one catches a list whose box stack balanced while a scope did not. */
    if (!PaintOpacityStackIsEmpty(builder->opacities)) {
        return DisplayRefuse(outList, diag, "the record list does not close every opacity "
                                              "scope it opens");
    }

    stats.commands = PaintDisplayListCount(outList);
    /* Every drawing kind the vocabulary has, added up in the one place the total is stated --
     * images included since 11.7, so `draws` stays the number a reader can check against
     * `commands - skips` whatever kinds the next phase adds. */
    stats.draws = stats.backgrounds + stats.borders + stats.texts + stats.images;
    if (outStats) {
        *outStats = stats;
    }
    return RoboOk;
}
