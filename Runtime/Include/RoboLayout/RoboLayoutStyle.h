/*
 * RoboLayout's CSS seam (Phase 10.1).
 *
 * RoboLayout consumes *computed styles*. It never parses CSS, never holds a
 * stylesheet, never normalises a value and never needs to know a property name
 * beyond the one it reads. The whole of that boundary is this header: a callback
 * that answers "what is the computed style of this element?" and a struct that
 * carries it.
 *
 * The seam is a callback rather than a `CssComputedStyle *` argument for
 * the reason every seam in this runtime is one: **the tree builder must be
 * testable without CSS**. A suite that wants to prove "a `display: none` element
 * generates no box" or "children are linked in document order" has no business
 * standing up a style context, a property table, a registry and a sheet to do it,
 * and a phase that made it do so would be a phase whose structural tests are
 * really integration tests. What a caller does *not* get to do is skip CSS and
 * still claim the boxes are styled: a source that answers nothing produces boxes
 * with no style and the STYLE_MISSING flag, which is a fact a later formatting
 * context checks.
 *
 * **The lifetime rule is the whole contract.** A borrowed style pointer is stored
 * in the tree, so it must stay valid for as long as the tree holds it -- the
 * builder does not copy it, and RoboLayout has no way to extend the life of
 * something it does not own. The real supplier (RoboLayoutDom.h) meets that by
 * owning one computed style per element for as long as the bridge lives; a test
 * meets it by keeping its styles on the stack. A supplier that returned a
 * temporary would be returning a dangling pointer the moment the next element was
 * asked about, and the rule is stated here, once, rather than discovered later.
 *
 * The display the builder dispatches on is read *through* this seam -- the
 * supplier hands over a style and the builder reads `display` out of it with
 * 9.12's own accessor, `CssComputedValueDisplay`. RoboLayout therefore
 * holds no list of property names and no CSS semantics of its own.
 */

#ifndef ROBO_LAYOUT_STYLE_H
#define ROBO_LAYOUT_STYLE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboHTML/RoboHTMLNode.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Answer "what is this element's computed style?".
 *
 * @p element is well formed whenever the builder calls (it only asks about
 * elements it has already resolved).
 * @return a **borrowed** style that must outlive the tree it is stored in, or
 *         NULL when there is nothing to answer with -- an element with no
 *         stylesheet, a document this supplier does not cover, a style that was
 *         never computed. NULL is a legitimate answer and not a failure: the
 *         builder creates the box and marks it STYLE_MISSING, because "this
 *         element generates a box" is a fact about layout and "nothing styled it"
 *         is a fact about CSS.
 */
typedef const CssComputedStyle *(*LayoutStyleFn)(
    void *userdata,
    const HtmlNodeHandle *element);

/** Who answers, and what it knows. A record by value, owned by the caller. */
typedef struct LayoutStyleSource {
    void *userdata;
    /** The callback. NULL means "this source answers nothing". */
    LayoutStyleFn ComputedStyleFor;
} LayoutStyleSource;

/** Clear @p source to "answers nothing". Safe on NULL. */
void LayoutStyleSourceInit(LayoutStyleSource *source);

/** Non-zero when @p source can answer (its callback is set). Says nothing about
 *  whether any particular element has a style. */
int LayoutStyleSourceIsUsable(const LayoutStyleSource *source);

/**
 * Ask @p source about @p element.
 *
 * The call the builder makes, so that "a source with no callback" and "the
 * callback answered NULL" need not be handled twice at the call site. @p source
 * may be NULL.
 *
 * @return a borrowed style, or NULL (see the callback's contract).
 */
const CssComputedStyle *LayoutStyleSourceStyle(
    const LayoutStyleSource *source,
    const HtmlNodeHandle *element);

/**
 * Read a computed style's `display`, which is the one CSS answer the builder
 * itself needs.
 *
 * The style may be NULL, which means "nothing was computed" and is answered with
 * the property's *initial* value (`inline`, per 9.12) rather than with a special
 * case -- so a box built without CSS is a box that is inline, which is what the
 * specification says an element nothing styled is.
 *
 * A style that carries no `display` entry at all is answered the same way and for
 * the same reason: 9.8's container holds the properties an element has an
 * *answer* for, and a non-inherited property nothing declared is answered by the
 * property's initial value, not by an entry.
 */
CssDisplay LayoutStyleDisplay(const CssComputedStyle *style);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_STYLE_H */
