/*
 * RoboInput targets (Phase 12.1) — what an event is *about*, carried as inert identity.
 *
 * ## The decision: a target is identity, never a reference
 *
 * A queued input event must stay meaningful while everything around it changes: the document may be
 * mutated, laid out again and re-painted, a window may be resized, and the event is still the same event.
 * So a target holds **values** -- ids and handles -- and never a pointer to a window, an application, a
 * layout object or a DOM node. Two of those values are other modules' types, and both were designed for
 * exactly this use:
 *
 *   `robo_html_node_handle_t` (`RoboHTMLNode.h`) is an "inert token (safe to copy, compare, store)" whose
 *   own documentation says application code holds nothing else, and whose generation field makes a stale
 *   handle resolve as stale rather than alias whatever node now occupies that slot. That is precisely the
 *   guarantee an queued event needs;
 *
 *   `robo_layout_node_id_t` is the id every door in RoboLayout and RoboPaint takes.
 *
 * Carrying the *types* rather than the modules' behaviour is the whole of this module's upward dependency:
 * RoboInput names two identities and calls nothing. It never resolves a handle, never asks which element is
 * at a point, and never runs a hit test -- a target is what a **dispatcher** fills in after it has done
 * those things, which is why nothing here can dispatch.
 *
 * ## The three kinds, and the rule that orders them
 *
 * A target says what it knows: `WINDOW` (a window, and nothing inside it), `APPLICATION` (an application,
 * and no window of it yet), `DOCUMENT` (a window *and* the node under the point). They are deliberately not
 * exclusive fields but a **kind**, so that "which of this record's fields are meaningful" is one question
 * with one answer -- the same shape `robo_paint_hit_test_result_t` uses for its outcome, and for the same
 * reason: a record whose fields can disagree about what it is has no validation that means anything.
 *
 * `DOCUMENT` implies a window, and `robo_input_target_is_valid` enforces it: a node hit with no window to
 * have hit it in is a record that could only be produced by mistake.
 *
 * ## The flags mirror paint's, on purpose
 *
 * `robo_paint_hit_test_result_t` carries three flags about a hit -- the node is a text run, the point is
 * inside the content box, the node is drawn at zero strength -- and this module's target flags are the same
 * three facts with **the same bit positions**, written down here so that a dispatcher may copy the word
 * unchanged. That is a deliberate duplication of *constants*, not of semantics: the two are asserted equal
 * by the suite (`input.target.from_hit_test`), which is the only way a mirrored constant is allowed to exist
 * in this tree. RoboInput cannot include the paint header to reuse them, because a hit-test *result* type
 * drags in a layout tree and this module is below both.
 */
#ifndef ROBO_INPUT_TARGET_H
#define ROBO_INPUT_TARGET_H

#include "RoboBase/RoboError.h"

#include "RoboHTML/RoboHTMLNode.h"
#include "RoboLayout/RoboLayoutTypes.h"

#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTypes.h"

#include "RoboWindow/RoboWindowTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Runtime-assigned application identity (0 = none). A future application registry issues these. */
typedef uint64_t robo_input_application_id_t;

#define ROBO_INPUT_APPLICATION_ID_NONE ((robo_input_application_id_t)0u)

/* The three facts a hit test concluded, mirrored from `ROBO_PAINT_HIT_FLAG_*` with the same bit
 * positions (see this header's comment). */

/** The node the point is over is a **text run** rather than a box. */
#define ROBO_INPUT_TARGET_FLAG_TEXT ((uint32_t)(1u << 0))
/** The point is inside the node's **content box**, not merely its border box. */
#define ROBO_INPUT_TARGET_FLAG_CONTENT ((uint32_t)(1u << 1))
/** The node is drawn at **zero strength** (`opacity: 0`): the answer stands, the picture does not show it. */
#define ROBO_INPUT_TARGET_FLAG_TRANSPARENT ((uint32_t)(1u << 2))

/** Every defined target flag. */
#define ROBO_INPUT_TARGET_FLAG_ALL \
    (ROBO_INPUT_TARGET_FLAG_TEXT | ROBO_INPUT_TARGET_FLAG_CONTENT | ROBO_INPUT_TARGET_FLAG_TRANSPARENT)

/** What a target knows. */
typedef enum robo_input_target_kind {
    /** Nothing: a cleared record's value. */
    ROBO_INPUT_TARGET_NONE = 0,
    /** A window, and nothing inside it. */
    ROBO_INPUT_TARGET_WINDOW = 1,
    /** An application, and no window of it yet. */
    ROBO_INPUT_TARGET_APPLICATION = 2,
    /** A window **and** the document node under the point. */
    ROBO_INPUT_TARGET_DOCUMENT = 3
} robo_input_target_kind_t;

/** Number of defined kinds. Asserted positionally by `input.conformance.vocabulary`. */
#define ROBO_INPUT_TARGET_KIND_COUNT 4u

/** Non-zero if @p kind is a defined kind. */
int robo_input_target_kind_is_known(robo_input_target_kind_t kind);

/** Human-readable kind name; never NULL; `"unknown"` when undefined. */
const char *robo_input_target_kind_name(robo_input_target_kind_t kind);

/**
 * What one event is about, as a value owned by nobody.
 *
 * `space` records which plane the routing was concluded in, and it is *the same value* the event carries
 * when a caller sets a target through `robo_input_event_set_target`: a target that claimed a different
 * space from the event it arrived on would be a record whose fields disagree, which is the shape this
 * module refuses everywhere.
 */
typedef struct robo_input_target {
    /** What this record knows (see the kind rules in this header's comment). */
    robo_input_target_kind_t kind;
    /** The window the event is about, or @ref ROBO_WINDOW_ID_NONE when the kind does not carry one. */
    robo_window_id_t window;
    /** The application the event is about, or @ref ROBO_INPUT_APPLICATION_ID_NONE. */
    robo_input_application_id_t application;
    /** The display the routing happened on, or `ROBO_DISPLAY_ID_NONE`. Informational. */
    robo_display_id_t display;
    /** The DOM node under the point, for `DOCUMENT`; an unformed handle otherwise. */
    robo_html_node_handle_t dom_node;
    /** The layout node under the point, for `DOCUMENT`; `ROBO_LAYOUT_NODE_ID_NONE` otherwise. */
    robo_layout_node_id_t layout_node;
    /** `ROBO_INPUT_TARGET_FLAG_*` bits; meaningful only for `DOCUMENT`. */
    uint32_t flags;
    /** The plane the routing was concluded in. */
    robo_input_space_t space;
} robo_input_target_t;

/** Clear @p target to "about nothing". Safe on NULL. */
void robo_input_target_init(robo_input_target_t *target);

/**
 * Non-zero when @p target is a record this build may carry on an event.
 *
 * The rules, in the order a reader should check them: the kind is defined; the fields the kind carries are
 * present (`WINDOW` and `DOCUMENT` carry a window, `APPLICATION` carries an application, only `DOCUMENT`
 * carries a node and flags); the fields the kind does *not* carry are **empty**, so that a target cannot
 * say two different things at once; the document handle is either well-formed or absent; and `space` is a
 * value an event in it could legitimately have (`APPLICATION` is allowed here -- an application may route
 * in its own plane -- but `UNKNOWN` is not, because a target whose plane is unknown could not be compared
 * against its event's).
 */
int robo_input_target_is_valid(const robo_input_target_t *target);

/** Non-zero when @p target carries a document node (`DOCUMENT`). Safe on NULL (0). */
int robo_input_target_has_document(const robo_input_target_t *target);

/** Non-zero when @p target names @p window, whatever its kind. Safe on NULL (0). */
int robo_input_target_is_window(const robo_input_target_t *target, robo_window_id_t window);

/** Nothing, in @p space. Refused for an unset space. */
robo_result_t robo_input_target_set_none(robo_input_target_t *target,
                                         robo_input_space_t space,
                                         robo_input_diag_t *diag);

/** A window, in @p space. A zero window id is refused: "a window, which is none" is not a target. */
robo_result_t robo_input_target_set_window(robo_input_target_t *target,
                                           robo_window_id_t window,
                                           robo_input_space_t space,
                                           robo_input_diag_t *diag);

/** An application, in @p space. A zero application id is refused. */
robo_result_t robo_input_target_set_application(robo_input_target_t *target,
                                                robo_input_application_id_t application,
                                                robo_input_space_t space,
                                                robo_input_diag_t *diag);

/**
 * A window and the node under the point, in @p space.
 *
 * @param dom_node  the handle a hit test answered with; may be an unformed handle when the consumer has no
 *                  document identity (a document served from a cache, a synthetic test event), in which case
 *                  the layout node id still travels.
 * @param layout_node the layout object id, or `ROBO_LAYOUT_NODE_ID_NONE`.
 * @param flags     `ROBO_INPUT_TARGET_FLAG_*` bits.
 *
 * A `DOCUMENT` target with **no** node at all (both identities absent) is refused: that is a `WINDOW`
 * target wearing a document's kind, and the kind is what a consumer switches on. At least one of the two
 * identities must be present.
 */
robo_result_t robo_input_target_set_document(robo_input_target_t *target,
                                             robo_window_id_t window,
                                             const robo_html_node_handle_t *dom_node,
                                             robo_layout_node_id_t layout_node,
                                             uint32_t flags,
                                             robo_input_space_t space,
                                             robo_input_diag_t *diag);

/** The document identity, by value. Returns 0 when the target carries none; @p out_node may be NULL. */
int robo_input_target_document(const robo_input_target_t *target,
                               robo_html_node_handle_t *out_dom_node,
                               robo_layout_node_id_t *out_node);

/** Non-zero when @p target carries @p flag. Safe on NULL (0). */
int robo_input_target_has_flag(const robo_input_target_t *target, uint32_t flag);

/** Field-by-field equality, the handle compared by value. Safe on NULL; two NULLs are equal. */
int robo_input_target_equal(const robo_input_target_t *a, const robo_input_target_t *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_TARGET_H */
