/*
 * RoboPaint opacity (Phase 11.9) — the effective opacity in force, and the stack of scopes
 * that put one there.
 *
 * The record and its stack live in **one header**, and that is the one structural difference
 * between this module and the clip's. `RoboPaintClip.h` holds the clip *stack* and points at
 * `RoboPaintGeometry.h` for the record, because a clip record *is* geometry: two axes, an
 * interval each, in paint coordinates, beside `PaintBoxes` and derived from the same
 * rectangles. Opacity is not geometry at all -- it is one real that says how strongly a
 * drawing reaches a surface -- so there is no geometry header for the record to live in and
 * no reason to invent one. A reader looking for "the opacity in force" finds both its type
 * and its stack here, and that is the whole of this module's opacity surface.
 *
 * **The record holds the *effective* opacity, never the declared one.** Those are different
 * facts and confusing them is the one mistake this type exists to prevent. `opacity: 0.5` on
 * an element is a *declaration*: a number read once, from a computed style, at the box that
 * wrote it. The effective opacity is what is **in force** at a point in the traversal, and it
 * is that declaration multiplied by every ancestor's -- so a `0.5` box inside a `0.5` box
 * draws at `0.25`, and a `0.5` box at the top of a document draws at `0.5`. Every record in
 * this module carries the second, because that is the number a rasteriser applies and a
 * consumer that had to re-multiply a path to find it would be re-walking the tree to
 * rediscover what the traversal already concluded.
 *
 * **A path accumulation, not an inheritance.** This is the fact worth reading twice, because
 * CSS's own word for the property (`opacity`) and CSS's own word for how a child sees it
 * (`inherit`) point in the wrong direction. `opacity` does **not** inherit
 * (`RoboCSSPropertyRegistry.c` registers it non-inherited, and argues why): inheritance copies
 * a value *down* to a child that declared nothing, so an inherited opacity would let a child
 * at `opacity: 1` inside a parent at `0.5` become fully opaque again. What actually happens is
 * that the parent's opacity applies to *everything the parent contains*, so the child is drawn
 * at `0.5` -- and at a `0.25` grandchild. That is an accumulation along a **path**, which is
 * why it lives here and in the traversal rather than in the cascade.
 *
 * **One deviation from CSS, stated rather than discovered.** CSS composites an element's
 * *whole subtree* as a group and applies the opacity to the flattened result. This module
 * multiplies the effective value **per drawing**, which is exact for a subtree whose contents
 * do not overlap and differs from group opacity where they do: two overlapping children of a
 * `0.5` parent composite as `0.5*(a over b)` under CSS and as `(0.5*a) over (0.5*b)` here.
 * The two agree about every drawing's *existence* and about the whole document's, and they can
 * differ in the colour of a pixel where one descendant's ink covers another's. Group opacity
 * needs an offscreen layer to composite into, which is a compositor and a later phase's; what
 * this build does instead is carry the multiplication and **say so**, so that the number a
 * surface receives is one this module can defend rather than one it guessed at. The record's
 * shape is what a group implementation will replace -- a flag and a layer id -- and nothing
 * above it has to change.
 *
 * **Why a record and not a bare `double`.** The value is one number, and a struct around one
 * number is normally ceremony. Three things make it worth it here, and all three are the same
 * argument the clip record makes: the range is an *invariant* that has one place to live, so
 * `PaintOpacityIsValid` is a door rather than a comparison repeated at every consumer;
 * "fully opaque" is a *state* rather than a magic literal, so a cleared record can be spelled
 * by a door and a consumer never writes `1.0` itself; and the field on an operation or a
 * command then reads as the peer of the clip field beside it, which is what lets the validity
 * rule, the builder's scope checks and the stack all be the clip's own machinery one field
 * along rather than a second design.
 *
 * An **opaque** record is the state of a document that declares no opacity anywhere: it is
 * what an empty stack answers, and a caller may draw under it without asking whether anything
 * is in force -- the same rule the clip record's cleared state states. A **transparent**
 * record (`0`) is a legal value and a meaningful one, and it is *not* the same as an empty
 * stack: a scope ancestor opacity has reduced to nothing draws nothing, which is a fact about
 * the document, and popping it restores the enclosing strength -- a transparent scope poisons
 * the drawing inside it and never the stack.
 *
 * Everything here is a value owned by nobody except the stack, which is created, used and
 * destroyed by whoever needs it. There is no global opacity state.
 */
#ifndef ROBO_PAINT_OPACITY_H
#define ROBO_PAINT_OPACITY_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintDiagnostics.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The strength in force                                               */
/* ------------------------------------------------------------------ */

/**
 * The opacity in force at a point in the traversal.
 *
 * One real in `[0, 1]`, and its meaning is fixed: **the factor a drawing is composited with**,
 * which is `0` for invisible, `1` for full strength, and the product of every ancestor's
 * declared opacity in between. A cleared record is `1` -- an element nothing said anything
 * about is fully opaque -- and that direction is the whole reason this type has a cleared
 * state rather than a zeroed one (`PaintOpacityInit`).
 */
typedef struct PaintOpacity {
    /** The effective opacity, in `[0, 1]`. */
    double value;
} PaintOpacity;

/**
 * Clear @p opacity to **fully opaque**: the state of a document that declares none.
 *
 * The door rather than a `1.0` at each site, for the reason the clip record has
 * `PaintClipInit`: a record whose cleared state is whatever `memset` leaves would be a
 * record whose default is *invisible* here, and every consumer of a cleared one would draw
 * nothing. Safe on NULL.
 */
void PaintOpacityInit(PaintOpacity *opacity);

/** The fully opaque record, by value: what an empty scope stack answers. */
PaintOpacity PaintOpacityOpaque(void);

/**
 * The record @p value names, clamped into `[0, 1]`.
 *
 * The construction door, and the clamp is the value module's own rule restated rather than a
 * second implementation of it: a declared opacity that arrived out of range was already
 * clamped where it was computed (`RoboCSSOpacity.h`), so what this protects is the arithmetic
 * *between* boxes -- a product, an accumulation a caller assembled itself -- from producing a
 * record no consumer may draw with. A value that is not a number becomes the fully opaque
 * record, for the direction that module argues: a bug in a caller's arithmetic must render the
 * way "nothing was declared" does, which is visible.
 */
PaintOpacity PaintOpacityFromValue(double value);

/** The value, by value. `1.0` (fully opaque) for NULL. */
double PaintOpacityValue(const PaintOpacity *opacity);

/** Non-zero when @p opacity is a record this build may draw with: finite, in `[0, 1]`.
 *  Safe on NULL (0 -- a NULL is not a record). */
int PaintOpacityIsValid(const PaintOpacity *opacity);

/** Non-zero when @p opacity is exactly fully opaque: the state in which the property
 *  changes nothing about what is inside it. Safe on NULL (1 -- nothing in force). */
int PaintOpacityIsOpaque(const PaintOpacity *opacity);

/**
 * Non-zero when @p opacity is exactly **fully transparent**: nothing inside it draws.
 *
 * The predicate a consumer asks before spending work, and the reason it is here rather than a
 * comparison against zero at each site: "a scope that draws nothing" is a fact about a
 * document, and a consumer that answered it itself would be re-deriving the rule. It is *not*
 * the same question as `PaintOpacityStackIsEmpty`, and the two must not be
 * conflated -- an empty stack is the opposite state (nothing is in force, so everything draws
 * at full strength).
 *
 * Safe on NULL (0 -- a NULL is not a transparent scope, it is no scope).
 */
int PaintOpacityIsTransparent(const PaintOpacity *opacity);

/** Field-by-field equality of the one field. Safe on NULL; two NULLs are equal. */
int PaintOpacityEqual(const PaintOpacity *a, const PaintOpacity *b);

/**
 * The opacity **inside** @p inner, entered from @p outer: their product.
 *
 * The accumulation, and the whole of what this module computes. It is a product rather than a
 * replacement because opacity composes: an element drawn at half strength inside an ancestor
 * drawn at half strength is drawn at a quarter, and a build that let the inner value *replace*
 * the outer one would render every nested scope as if its ancestors declared nothing.
 *
 * Both operands are records, so both are in `[0, 1]` and the product is too; the clamp is
 * applied anyway, because this door is also the one a caller uses to combine two values it
 * assembled itself, and "the result is a record" is a property worth holding by construction
 * rather than by argument. A NULL operand is read as opaque (nothing in force), so the call is
 * safe on a destructively cleared pointer.
 */
PaintOpacity PaintOpacityMultiply(const PaintOpacity *outer,
                                                 const PaintOpacity *inner);

/**
 * The byte @p opacity names in a rasteriser's own unit: `0` invisible, `255` full.
 *
 * The single conversion between this module's `[0, 1]` and `RoboGraphicsRaster.h`'s global
 * opacity, and it lives here rather than at the call site because the rounding is a decision:
 * the round is **to nearest**, so `0.5` becomes `128` rather than `127`, and the raster then
 * multiplies a source alpha by `128 / 255`. Neither `127` nor `128` is exactly one half, and
 * the one that is nearer is the one this build uses. `1` is exactly `255` rather than a value
 * that rounds to it, so the fully opaque case is the raster's own default and a document with
 * no opacity is drawn by a raster nothing has changed.
 *
 * A NULL or invalid record answers `0`, which is the *safe* direction for a converter: a value
 * nothing can read must not put ink on a surface, and the alternative -- answering `255` --
 * would turn a caller's arithmetic bug into a fully drawn document.
 */
uint8_t PaintOpacityToByte(const PaintOpacity *opacity);

/* ------------------------------------------------------------------ */
/* The scopes in force                                                 */
/* ------------------------------------------------------------------ */

/** A LIFO of opacity records. Opaque; owned by whoever created it. */
typedef struct PaintOpacityStack PaintOpacityStack;

/**
 * Create an empty stack: **nothing in force**, which is full strength.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p outStack; RoboOutOfMemory. The
 *         stack is left empty on failure and @p outStack receives NULL.
 */
Result PaintOpacityStackCreate(PaintOpacityStack **outStack,
                                              PaintDiag *diag);

/** Destroy @p stack, freeing its storage. Safe on NULL. */
void PaintOpacityStackDestroy(PaintOpacityStack *stack);

/** Empty @p stack, keeping its capacity. What a caller reusing a stack across passes calls
 *  first. Safe on NULL. */
void PaintOpacityStackReset(PaintOpacityStack *stack);

/**
 * Push @p opacity: from here on it is the strength in force.
 *
 * @p opacity is copied, so the caller's record may go out of scope immediately. An opaque
 * record is legal and pushes a scope that changes nothing; the *command* vocabulary refuses
 * such a push, because a scope that establishes nothing is a command a consumer could only
 * skip (`RoboPaintTypes.h`), but the stack is a container of values and has no opinion.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an @p opacity that is not a
 *         record this build may draw with; RoboOutOfMemory. On either failure nothing was
 *         pushed and the stack is exactly as it was.
 */
Result PaintOpacityStackPush(PaintOpacityStack *stack,
                                            const PaintOpacity *opacity,
                                            PaintDiag *diag);

/**
 * Pop the innermost scope.
 *
 * A pop with **nothing to pop is a refusal**, not a silent no-op -- the clip stack's rule, for
 * the clip stack's reason: answering OK would let a consumer that lost track of its own pushes
 * go on drawing at a strength that is not in force. The refusal is safe: the stack is
 * untouched, so a caller may report it and carry on.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p stack; RoboNotFound when the stack is
 *         empty.
 */
Result PaintOpacityStackPop(PaintOpacityStack *stack,
                                           PaintDiag *diag);

/**
 * The strength in force: the innermost scope's record, or the fully opaque one when the stack
 * is empty.
 *
 * @p out always receives a valid record -- an empty stack is not an error here, because
 * "nothing is in force" is the ordinary state of a document that declares no opacity. Safe on
 * NULL: a NULL stack answers the opaque record, so a caller may read a destructively cleared
 * pointer without a branch.
 */
void PaintOpacityStackCurrent(const PaintOpacityStack *stack,
                                      PaintOpacity *out);

/** Scopes currently open: 0 for an empty stack, which is a legal answer (0 for NULL). */
size_t PaintOpacityStackDepth(const PaintOpacityStack *stack);

/** Records the stack can hold before it must grow (0 for NULL). */
size_t PaintOpacityStackCapacity(const PaintOpacityStack *stack);

/** Non-zero when no scope is open. Safe on NULL (1 -- nothing is in force). */
int PaintOpacityStackIsEmpty(const PaintOpacityStack *stack);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_OPACITY_H */
