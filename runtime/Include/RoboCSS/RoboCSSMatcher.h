/*
 * RoboCSS selector matching (Phase 9.4).
 *
 *   selector list (9.4 parser)  +  element host (RoboCSSStyle.h)  →  a verdict
 *
 * The matcher reads a selector and asks a host about elements. It holds no DOM,
 * no cache, no document and no stylesheet: it is a pure function of (selector,
 * host, element), which is what makes it testable without a browser and what
 * keeps RoboCSS a peer of RoboHTML rather than a client of it.
 *
 * Three ideas do all the work.
 *
 * 1. Compound selectors are evaluated against one *view*; complex selectors walk
 *    right to left.
 *
 *    A compound is "all of these hold of the same element", so it costs one
 *    lookup and then a scan. A complex selector is evaluated from its right-most
 *    compound — the one the candidate element must be — and walked leftwards
 *    through the combinators. The walk backtracks, because a greedy one is
 *    wrong: `a b c` must be able to reject an ancestor `b` and try a higher one,
 *    and `.x .y .z` can match through either of two `.y`s. The recursion is
 *    bounded by the number of compounds in the selector, never by the tree, so
 *    the stack cost is a property of the stylesheet rather than of the document.
 *
 * 2. A relation the host cannot answer is *reported*, never guessed.
 *
 *    `div > p` needs a parent and `h1 + p` needs a previous sibling. A host that
 *    supplies only a view can answer type, universal, class, id and attribute
 *    selectors perfectly well and cannot answer those two at all. The matcher's
 *    answer in that case is "not evaluated", and `unsupported` in the result
 *    says so — because "does not match" and "was not decided" lead to different
 *    places, and a cascade that could not tell them apart would apply the wrong
 *    rules silently. The same rule covers a pseudo-class this build does not
 *    implement (`:hover`, `:lang()`, `:nth-of-type()`): the selector is not
 *    evaluated, and `condition` names what stopped it.
 *
 * 3. What is supported is exactly what Phase 9.4 specifies, and no more.
 *
 *    `:root`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`,
 *    `:nth-last-child()`, `:not()` — the structural set needed to match a real
 *    stylesheet's rules, plus the four combinators and the six attribute
 *    operators. `:hover` and friends need input state that does not exist yet,
 *    and `::before` needs generated content that does not exist yet, so those are
 *    reported as unsupported rather than implemented wrongly.
 *
 * Sibling counting is element counting. The host is asked for the previous and
 * next *element* sibling, so a text node or a comment between two elements
 * neither hides a sibling nor counts as one — `:first-child` is about elements,
 * and the nodes the host filters are nodes RoboCSS cannot see.
 *
 * Nothing here mutates anything. The selector is read-only, the host is called
 * through const-qualified function pointers, and an element is an opaque token
 * passed back to the host unchanged.
 */

#ifndef ROBO_CSS_MATCHER_H
#define ROBO_CSS_MATCHER_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSSelector.h"
#include "RoboCSS/RoboCSSStyle.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What a match attempt found.
 *
 * `matched` and `unsupported` are independent, and both can be set: a selector
 * list whose first selector is `:hover` and whose second is `.card` *does* match
 * a card, and one member of it could not be evaluated. A caller that only looks
 * at `matched` gets the answer it needs, and a caller that also looks at
 * `unsupported` learns not to trust a negative.
 */
typedef struct robo_css_selector_match {
    /** 1 when the selector matched the element. */
    int matched;
    /**
     * 1 when some condition could not be evaluated, so `matched == 0` means
     * "not decided" rather than "no". 0 on every fully evaluated attempt.
     */
    int unsupported;
    /**
     * The pseudo-class name that could not be evaluated — borrowed from the
     * selector's own name arena, so it is valid as long as the selector list is.
     * Absent when the reason was something other than a pseudo-class (an
     * unavailable relation callback, the column combinator), and absent when
     * nothing was unevaluatable.
     */
    robo_css_slice_t condition;
    /**
     * Which complex selector of the list matched — the index
     * robo_css_selector_list_at accepts. Only meaningful for a list match, and
     * only when `matched` is 1.
     */
    size_t selector_index;
    /** Extent of that complex selector in the source, when one matched. */
    robo_css_range_t range;
} robo_css_selector_match_t;

/** Clear @p out to "not matched, nothing unevaluatable". Safe on NULL. */
void robo_css_selector_match_init(robo_css_selector_match_t *out);

/**
 * Does @p element satisfy @p simple?
 *
 * @p compound and @p simple_index locate @p simple inside the compound that owns
 * it, which is what a functional pseudo-class's parsed argument is addressed
 * through; they may be omitted (NULL, 0) for a simple selector that has none, in
 * which case a functional pseudo-class is reported as unsupported rather than
 * guessed at.
 *
 * @return 1 matched; 0 not matched (or not decided — check
 *         `out->unsupported`); -1 when @p simple or @p host is NULL or unusable.
 */
int robo_css_simple_selector_matches(const robo_css_simple_selector_t *simple,
                                     const robo_css_compound_selector_t *compound,
                                     size_t simple_index,
                                     const robo_css_element_host_t *host,
                                     robo_css_element_ref_t element,
                                     robo_css_selector_match_t *out,
                                     robo_css_diag_t *diag);

/**
 * Does @p element satisfy every condition in @p compound?
 *
 * The order of simple selectors is the order they are evaluated in, and a
 * compound short-circuits on the first failure — so a cheap type or class test
 * written before an expensive `:nth-child()` is genuinely cheaper.
 *
 * @return 1 / 0 / -1 as above.
 */
int robo_css_compound_selector_matches(const robo_css_compound_selector_t *compound,
                                       const robo_css_element_host_t *host,
                                       robo_css_element_ref_t element,
                                       robo_css_selector_match_t *out,
                                       robo_css_diag_t *diag);

/**
 * Does @p element satisfy @p selector, combinators included?
 *
 * @return 1 / 0 / -1; -1 also for a structurally incoherent selector (see
 *         robo_css_complex_selector_is_well_formed), because a combinator with
 *         no compound on one side is a broken record rather than an answer.
 */
int robo_css_complex_selector_matches(const robo_css_complex_selector_t *selector,
                                      const robo_css_element_host_t *host,
                                      robo_css_element_ref_t element,
                                      robo_css_selector_match_t *out,
                                      robo_css_diag_t *diag);

/**
 * Does @p element satisfy any selector in @p list?
 *
 * Stops at the first match, and in that case `out->selector_index` and
 * `out->range` describe which one. Selectors after the matching one are not
 * evaluated, so an unsupported condition in a later selector is not reported —
 * the answer was already decided.
 *
 * @return 1 / 0 / -1 as above.
 */
int robo_css_selector_list_matches(const robo_css_selector_list_t *list,
                                   const robo_css_element_host_t *host,
                                   robo_css_element_ref_t element,
                                   robo_css_selector_match_t *out,
                                   robo_css_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_MATCHER_H */
