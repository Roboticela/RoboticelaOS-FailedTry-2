/*
 * RoboLayout objects (Phase 10.1).
 *
 * One box: the thing a layout pass positions. It is *distinct from the DOM* and
 * does not copy it:
 *
 *   the DOM owns elements      a tree owns boxes
 *   a DOM node has children    a box has children *that generate boxes*
 *   an element has a style     a box *references* one
 *
 * Two references and no copies:
 *
 *   `dom_node`  the inert RoboHTML handle of the element this box came from.
 *               A handle is a token (document id, node id, generation), so
 *               asking "which element is this box?" is a comparison and asking
 *               "which box is this element?" is a lookup in the tree.
 *   `style`     a **borrowed** computed style (RoboCSS's 9.8 container), owned
 *               by whoever supplied it -- for the DOM bridge in RoboLayoutDom.h,
 *               the bridge itself. The tree never disposes a style it does not
 *               own, and RoboLayout never parses CSS, never normalises a value
 *               and never resolves one.
 *
 * One CSS value *is* copied, and only one: `display`. The reason is that the box
 * would not exist without it -- the builder creates a box exactly when the
 * computed `display` generates one -- and that the very first question a
 * formatting context asks is what kind of box it has, before it has any reason
 * to go looking through a style it may not have. Everything else about the
 * element (`position`, the box edges, `background`, the borders, the insets) is
 * read from the borrowed style when a later goal needs it, so there is exactly
 * one copy of each fact and no second place for it to go stale.
 *
 * The links are **ids, not pointers**, for the same reason RoboHTML's node
 * registry uses ids: tree storage moves when it grows, and an id that names a
 * dead box is indistinguishable from an id that names nothing -- both are
 * refused by the lookup. A caller that must keep a box keeps its id.
 *
 * The object is written once, by the builder, and is read-only to everybody
 * else. Nothing in this header takes the tree by non-const pointer except
 * robo_layout_tree_add_object (RoboLayoutTree.h), which is why the links cannot
 * be edited behind a pass's back.
 */

#ifndef ROBO_LAYOUT_OBJECT_H
#define ROBO_LAYOUT_OBJECT_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSDisplay.h"
#include "RoboHTML/RoboHTMLNode.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutBorder.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutLineBox.h"
#include "RoboLayout/RoboLayoutMargin.h"
#include "RoboLayout/RoboLayoutPadding.h"
#include "RoboLayout/RoboLayoutResult.h"
#include "RoboLayout/RoboLayoutText.h"
#include "RoboLayout/RoboLayoutTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Forward declaration: a box is always reached through the tree that owns it. */
typedef struct robo_layout_tree robo_layout_tree_t;

/*
 * Object flags. One bit, and it is the one a consumer must check before reading
 * a property off a box's style:
 *
 *   STYLE_MISSING  the builder had no computed style for this element (no style
 *                  source was supplied, or the source answered nothing), so the
 *                  box exists with the CSS *initial* values only. A formatting
 *                  context that read `width` off such a box would be reading a
 *                  value nobody computed, which is why the fact is a flag rather
 *                  than an inference from `style == NULL`: the absence is a
 *                  property of the build, not an accident.
 */
#define ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING (1u << 0)
/** Every defined object flag. */
#define ROBO_LAYOUT_OBJECT_FLAG_ALL (ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING)

/**
 * One box.
 *
 * Stored in the tree's arena and reached by id. The struct is public so that a
 * reader needs no accessor for every field, and the accessors below exist for the
 * two cases where a plain field read is not enough: an id that must be validated,
 * and a link that must be distinguished from "absent".
 *
 * A borrowed pointer to one of these is valid **until the next mutation of the
 * tree** -- adding an object may move the arena. A caller that keeps a box across
 * an add keeps its id.
 */
typedef struct robo_layout_object {
    /** Tree-scoped identity; never NONE for a live box. */
    robo_layout_node_id_t id;
    /**
     * What this object is (RoboLayoutTypes.h): a box, or a text run.
     *
     * One kind since 10.5, and the field is here rather than inferred from
     * @ref dom_node's node kind because a later goal's anonymous box will have no
     * node at all -- so "what is this?" has to be answerable from the object.
     */
    robo_layout_object_kind_t kind;
    /**
     * The DOM node this object came from. Always well formed for a live object.
     *
     * An element for a box and a **text node** for a text object, which is why the
     * field is not named `element`: a node handle is a node handle
     * (RoboLayoutTypes.h), and the accessor that reads it is the same door for
     * both kinds.
     */
    robo_html_node_handle_t dom_node;
    /**
     * Borrowed computed style, or NULL when none was supplied (see the flag).
     *
     * For a text object this is the **parent element's** style, borrowed -- not a
     * copy and not a style of its own. Every property a run needs (font family,
     * size, weight, style, `direction`, `color`) is an inherited property, and
     * RoboCSS already distributes inherited values to descendants, so a text
     * node's computed style *is* its parent's for exactly those properties.
     * RoboLayoutText.h argues it at length.
     */
    const robo_css_computed_style_t *style;
    /**
     * The `display` this object was created under.
     *
     * For a box, the computed value, copied because the box would not exist
     * without it. For a **text object** it is `inline`, and that is a derivation
     * rather than a declaration: a text node's content is inline-level by
     * construction, and there is no property an author could write that would make
     * it otherwise. The passes dispatch on this field, so a text object being
     * inline-level is what makes it a participant of a line without either pass
     * special-casing it (RoboLayoutInline.h).
     */
    robo_css_display_t display;
    /**
     * The measured run, for a **text object** (10.5).
     *
     * Written by the inline pass where the run is placed, and zero for a box --
     * which is unambiguous because a box is not measured: `source` stays an
     * ill-formed handle, exactly as it is in a run nothing measured. The record is
     * kept on the object rather than discarded after placement because it is the one
     * place the *consequences* of a measurement live: a caller can ask how wide the
     * run was, how many code points it covered, whether the font had glyphs for them
     * and which direction it was declared in -- and 10.6's line box and 10.7's
     * baseline read ascent and descent from here instead of re-measuring.
     *
     * It is a copy of a value, not a borrowed reference: a run is arithmetic about a
     * range, so unlike @ref style and @ref dom_node it has nothing to outlive and
     * nothing to go stale.
     */
    robo_layout_text_run_t text_run;
    /**
     * The line box this object was **placed on** (10.6), or NONE.
     *
     * Written by the inline pass at the moment of placement and cleared with the
     * lines themselves, so it is a cache of a conclusion in the same sense
     * @ref context is (RoboLayoutObject.h) -- and read through a door that validates
     * it against the arena, so a released line cannot be read back.
     *
     * NONE means "this object is not a placement": either nothing has placed it, or
     * it is a **transparent inline box with contents**, whose rectangle is the union
     * of what its contents reached and which therefore belongs to no single line
     * (RoboLayoutLineBox.h argues it). The two cases are told apart by the same fact
     * as everywhere else in this module -- whether the object has a computed result.
     */
    robo_layout_line_id_t line;
    /**
     * The baseline this object was **placed** with (10.7), or NONE.
     *
     * Written by the inline pass at the same moment @ref line is decided -- the two
     * are the same event, a participant going onto a line -- and cleared with the
     * lines, because neither outlives the pass that concluded it.
     *
     * It is on the *object* rather than in @ref result, and that is a decision worth
     * stating. A `robo_layout_result_t` is embedded in every object including every
     * block box, and a block box has no baseline: it is not on a line and nothing
     * aligns to it. A field in the shared result record would therefore be a field
     * that is meaningless for most objects and whose zero would have to be
     * distinguished from a real zero -- the same "a shape that reads as an answer"
     * failure 10.5 refused to allow for a box's run and 10.6 refused for a line id. A
     * baseline is a *placement* fact, and this is where this module keeps those.
     *
     * NONE means "not placed": either nothing has placed this object, or it is a
     * transparent inline box with contents (which is a participant of the context and
     * not a member of any line -- RoboLayoutLineBox.h). The two are told apart the
     * same way @ref line's are, by whether the object has a computed result.
     */
    robo_layout_baseline_metrics_t baseline;
    /**
     * The margins this box was **placed** with (10.9), or a cleared record.
     *
     * The used values rather than the declared ones, because those are what the pass
     * acted on: `margin: auto` is not a number until a containing block has been
     * measured against, and that is a conclusion of the pass exactly as @ref baseline
     * is. Cleared with the geometry in the same sweep and for the same reason -- a
     * used margin that outlived the classification would be a plausible-looking
     * number about a layout that no longer holds.
     *
     * It is on the object for the reason a reader will need it one phase from now: a
     * *margin box* is what paint (11) draws a shadow around and what a hit test (12)
     * includes, and neither can be recovered from the result, which holds the content
     * box by definition.
     *
     * A cleared record means **no margins were used**, which is not the same as "no
     * pass ran": `robo_layout_object_margins` refuses an object that was never placed
     * *and* an object no margin applies to, and the two halves of that are documented
     * there.
     */
    robo_layout_margin_edges_t margins;
    /**
     * The padding this box was **sized** with (10.10), or a cleared record.
     *
     * The used values, and for this property the used and the declared values are the
     * same number -- there is no `auto` to distribute and no containing block to resolve
     * against -- so what this field adds over the style is the *decision*: which sides
     * reached geometry and which were declarations this build could not use.
     *
     * It is on the object, and this is the field a reader will need first, because it is
     * the answer to the one question the result cannot answer: **where a box's contents
     * begin**. A result's `position` is the box's border-box origin and its `size` is the
     * border box, so the content origin is that origin plus `top`/`left` from here -- and
     * the content box is that border box less the four numbers. A later phase that paints
     * a background, clips a child or hit-tests an interior reaches for this record rather
     * than re-reading a style and re-deciding which declarations it could use.
     *
     * Cleared with the geometry in the same sweep and for the same reason as @ref margins:
     * a used padding that outlived the classification would be a plausible-looking number
     * about a layout that no longer holds.
     */
    robo_layout_padding_edges_t padding;
    /**
     * The border this box was **sized and drawn** with (10.11), or a cleared record.
     *
     * The third inset and the outermost of the two that belong to the box, so this is
     * the record that turns the padding box into the **border box** -- the number a
     * result's `size` carries -- and `top`/`left` from it are the *second* term of a
     * content origin:
     *
     *     content origin = the result's position
     *                    + the border's top / left   (this field)
     *                    + the padding's top / left  (@ref padding)
     *
     * It carries the style and colour of each side as well as its used width, which is
     * paint's requirement rather than geometry's: a pass that draws an edge decides all
     * three facts at the same moment, and this layer has already decided which of the
     * twelve declarations it could use. Re-reading a style to find out would re-decide
     * it, and a second reader of one question is what this module exists to avoid.
     *
     * Note what a side's width means here: the **used** width, which is zero for
     * `border: 5px none` however `border-width` was written. A paint pass therefore
     * draws a side exactly when its width is above zero, and never has to ask the style
     * a second question.
     *
     * Cleared with the geometry in the same sweep and for the same reason as @ref margins
     * and @ref padding: a used border that outlived the classification would be a
     * plausible-looking record about a layout that no longer holds.
     */
    robo_layout_border_edges_t borders;

    /* Structural links (ROBO_LAYOUT_NODE_ID_NONE = absent). */
    robo_layout_node_id_t parent;
    robo_layout_node_id_t first_child;
    robo_layout_node_id_t last_child;
    robo_layout_node_id_t prev_sibling;
    robo_layout_node_id_t next_sibling;
    size_t child_count;

    /** Distance from the root box, which is 0. */
    uint32_t depth;
    /** ROBO_LAYOUT_OBJECT_FLAG_* bits. */
    uint32_t flags;
    /** What the pass concluded about this box. */
    robo_layout_result_t result;

    /*
     * Formatting contexts (10.2). Two ids, and the pair is the point: a box
     * *participates* in the context its layout takes place in and may *establish*
     * a different one for its contents, and most boxes do only the first.
     * RoboLayoutFormattingContext.h argues why collapsing them into one fact is
     * the most expensive mistake this phase could make.
     *
     * Both stay NONE until the first pass classifies the tree, and an add or a
     * clear invalidates them (RoboLayoutTree.h) -- so a caller can always tell
     * "not classified yet" (both NONE, `robo_layout_tree_is_classified` 0) from
     * "classified and this box is transparent" (context set, owned NONE).
     */
    /** The context this box's own layout participates in. */
    robo_layout_fc_id_t context;
    /** The context this box establishes for its contents, or NONE. */
    robo_layout_fc_id_t context_owned;
} robo_layout_object_t;

/*
 * Read side. Every function takes the tree and an id, and answers ROBO_NOT_FOUND
 * for an id that names no box -- so a stale id is a reported miss rather than a
 * read of whatever now occupies that slot.
 */

/** The object, by value. @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL args or
 *  a NONE id; ROBO_NOT_FOUND for an id that names no box in @p tree. @p out is
 *  cleared first, so a failure never leaves a half-filled copy. */
robo_result_t robo_layout_object_get(const robo_layout_tree_t *tree,
                                     robo_layout_node_id_t id,
                                     robo_layout_object_t *out,
                                     robo_layout_diag_t *diag);

/** Kind of @p id; ROBO_LAYOUT_OBJECT_NONE when the id names no box. */
robo_layout_object_kind_t robo_layout_object_kind(const robo_layout_tree_t *tree,
                                                  robo_layout_node_id_t id);

/** The element @p id came from.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL args or a NONE id;
 *  ROBO_NOT_FOUND for an id that names no box. */
robo_result_t robo_layout_object_dom_node(const robo_layout_tree_t *tree,
                                          robo_layout_node_id_t id,
                                          robo_html_node_handle_t *out);

/** Borrowed computed style of @p id, or NULL when the box has none (or the id
 *  names no box -- check the id first if the difference matters). */
const robo_css_computed_style_t *robo_layout_object_style(
    const robo_layout_tree_t *tree,
    robo_layout_node_id_t id);

/** Computed display of @p id, or the display of no box (NONE) when the id names
 *  none. */
robo_css_display_t robo_layout_object_display(const robo_layout_tree_t *tree,
                                              robo_layout_node_id_t id);

/**
 * The measured run of a **text object** @p id, by value.
 *
 * @return ROBO_OK and the run; ROBO_INVALID_ARGUMENT for NULL arguments or a NONE
 *         id; ROBO_NOT_FOUND when the id names no object, names a **box** (a box is
 *         not measured, so its run would be an ill-formed handle and a zeroed
 *         rect -- a shape that reads as an answer), or names a text object whose
 *         result is not COMPUTED -- one no pass has reached, or one a pass declined.
 *
 *         The miss cases are told apart by `robo_layout_object_kind` and the
 *         object's result state, which is the same division this module draws
 *         everywhere: an id, a kind, and "did this one get laid out?" are three
 *         facts and not one. A *declined* text run is deliberately a miss rather
 *         than a zero-size answer, because its run is zeroed by construction and a
 *         zeroed run reads as a measurement.
 */
robo_result_t robo_layout_object_text_run(const robo_layout_tree_t *tree,
                                          robo_layout_node_id_t id,
                                          robo_layout_text_run_t *out,
                                          robo_layout_diag_t *diag);

/**
 * The baseline metrics @p id was **placed** with (Phase 10.7).
 *
 * The read side of the placement fact the inline pass writes, and the door a later
 * phase uses to ask "what is this participant's floor?" without re-measuring. A text
 * run's metrics came from its font (with the line gap split as half-leading);
 * everything else took the documented **bottom-edge fallback** (RoboLayoutBaseline.h).
 *
 * @return ROBO_OK and the metrics; ROBO_INVALID_ARGUMENT for NULL arguments or a NONE
 *         id; ROBO_NOT_FOUND when the id names no object, or names one that was never
 *         placed -- including a transparent inline box with contents, which encloses
 *         members without being one. @p out is cleared to NONE first, so a caller that
 *         ignores the code has an unusable baseline rather than a leftover one.
 *
 *         A *declined* object is a miss rather than a zero, for the reason
 *         `robo_layout_object_text_run` gives: a zeroed record reads as an answer, and
 *         "this participant sits at the baseline with no extent" is a different
 *         statement from "nothing placed this participant".
 */
robo_result_t robo_layout_object_baseline(const robo_layout_tree_t *tree,
                                          robo_layout_node_id_t id,
                                          robo_layout_baseline_metrics_t *out,
                                          robo_layout_diag_t *diag);

/**
 * The **margins** @p id was placed with (Phase 10.9).
 *
 * The used values of the four sides, and the record a later phase reads a *margin
 * box* out of. Signed, because a negative margin is a legal declaration: `left` may
 * be less than zero on a box that overhangs its containing block.
 *
 * The door refuses three things, and the three are three different statements:
 *
 *   an id that names nothing                     -- `robo_layout_object_get`'s miss;
 *   an object **nothing placed**, so no margins were used, and -- unlike a baseline,
 *   whose "never placed" is visible in its kind -- a margin record's cleared state is
 *   a legal value (zero margins are the initial value), so this door has to ask the
 *   result instead;
 *   a **text run**, and a **transparent inline box** with contents: no margins apply
 *   to either. A text node is not a box at all, and CSS does not apply margins to a
 *   non-replaced inline box -- so a `<span style="margin-left: 10px">` has a
 *   declaration this build can read from its style and a used margin of *nothing*.
 *   Answering a zero with a count of two resolved sides would tell a caller the
 *   author's margin had been honoured and come to zero, which is the kind of
 *   plausible-looking wrong
 *   answer this module refuses everywhere else.
 *
 * @return ROBO_OK and the used margins; ROBO_INVALID_ARGUMENT for NULL arguments or
 *         a NONE id; ROBO_NOT_FOUND for the three cases above. @p out is cleared to no
 *         margins beside @p id's containing block first, so a caller that ignores the
 *         code reads a stated absence rather than a leftover.
 */
robo_result_t robo_layout_object_margins(const robo_layout_tree_t *tree,
                                         robo_layout_node_id_t id,
                                         robo_layout_margin_edges_t *out,
                                         robo_layout_diag_t *diag);

/**
 * The **padding** @p id was sized with (Phase 10.10).
 *
 * The four used values, and the door a caller reads a box's **content box** through: a
 * result carries the border box (`RoboLayoutResult.h`), so the content box is that rect
 * inset by `top` and `left` from here and reduced by the four numbers -- which is the one
 * derivation this module does not ask a caller to do from a style, because re-reading
 * `padding-left` off a computed style means re-deciding whether the declaration was one
 * this build could use.
 *
 * The door refuses the same three things `robo_layout_object_margins` refuses, and for
 * the same reasons:
 *
 *   an id that names nothing                     -- `robo_layout_object_get`'s miss;
 *   an object **nothing placed**, so no padding was applied -- a cleared padding record is
 *   a legal value (no padding is CSS's initial state), so this door asks the result rather
 *   than reading the record's silence;
 *   a **text run**, and a **transparent inline box** with contents: no padding applies to
 *   either. A text node is not a box at all, and a transparent inline box's rectangle is
 *   the union of its contents rather than a placed size -- this build does not inflate
 *   that union by the padding an author declared, and `RoboLayoutPadding.h` says so out
 *   loud. Answering a zero with four resolved sides would tell a caller the author's
 *   padding had been honoured and come to nothing.
 *
 * @return ROBO_OK and the used padding; ROBO_INVALID_ARGUMENT for NULL arguments or a
 *         NONE id; ROBO_NOT_FOUND for the three cases above. @p out is cleared to no
 *         padding first, so a caller that ignores the code reads a stated absence rather
 *         than a leftover.
 */
robo_result_t robo_layout_object_padding(const robo_layout_tree_t *tree,
                                         robo_layout_node_id_t id,
                                         robo_layout_padding_edges_t *out,
                                         robo_layout_diag_t *diag);

/**
 * The **border** @p id was sized with (Phase 10.11).
 *
 * The four used widths *and* the style and colour that produced each, which is what
 * makes this the one record a paint pass needs: it decides whether to draw a side
 * (width above zero), how to draw it (the style) and in what colour, without re-reading
 * a computed style and re-deciding which declarations this build could use.
 *
 * It is also the outermost of the two insets that belong to the box, so it is the
 * second term of a **content origin**: a result's `position` is the border-box origin,
 * `top`/`left` here take that to the padding box, and `top`/`left` from
 * `robo_layout_object_padding` take that to where the contents begin. The three
 * numbers compose in that order and none of them is recoverable from another.
 *
 * The door refuses the same four things `robo_layout_object_padding` refuses, and for
 * the same reasons:
 *
 *   an id that names nothing                      -- `robo_layout_object_get`'s miss;
 *   an object **nothing placed**, so no border was applied -- a cleared border record is
 *   a legal value (no border is CSS's initial geometry), so this door asks the result
 *   rather than reading the record's silence;
 *   a **text run**, and a **transparent inline box** with contents: no border applies to
 *   either. A text node is not a box at all, and a transparent inline box's rectangle is
 *   the union of its contents rather than a placed size -- this build does not inflate
 *   that union by a declared border, and `RoboLayoutBorder.h` says so out loud.
 *
 * @return ROBO_OK and the used border; ROBO_INVALID_ARGUMENT for NULL arguments or a
 *         NONE id; ROBO_NOT_FOUND for the three cases above. @p out is cleared to no
 *         border first, so a caller that ignores the code reads a stated absence rather
 *         than a leftover.
 */
robo_result_t robo_layout_object_borders(const robo_layout_tree_t *tree,
                                         robo_layout_node_id_t id,
                                         robo_layout_border_edges_t *out,
                                         robo_layout_diag_t *diag);

/**
 * The parent.
 * @return ROBO_OK and the id; ROBO_INVALID_ARGUMENT for NULL args or a NONE id;
 *         ROBO_NOT_FOUND for an id that names no box, *and* for the root box,
 *         which has no parent. The two are distinguishable through
 *         robo_layout_object_is_root.
 */
robo_result_t robo_layout_object_parent(const robo_layout_tree_t *tree,
                                       robo_layout_node_id_t id,
                                       robo_layout_node_id_t *out);

/** First child of @p id, or ROBO_NOT_FOUND when it has none. */
robo_result_t robo_layout_object_first_child(const robo_layout_tree_t *tree,
                                            robo_layout_node_id_t id,
                                            robo_layout_node_id_t *out);

/** Last child of @p id, or ROBO_NOT_FOUND when it has none. */
robo_result_t robo_layout_object_last_child(const robo_layout_tree_t *tree,
                                           robo_layout_node_id_t id,
                                           robo_layout_node_id_t *out);

/** Previous sibling of @p id (the box before it *as a box*, not as a node). */
robo_result_t robo_layout_object_previous_sibling(const robo_layout_tree_t *tree,
                                                 robo_layout_node_id_t id,
                                                 robo_layout_node_id_t *out);

/** Next sibling of @p id. */
robo_result_t robo_layout_object_next_sibling(const robo_layout_tree_t *tree,
                                             robo_layout_node_id_t id,
                                             robo_layout_node_id_t *out);

/** Number of direct children; ROBO_NOT_FOUND for an id that names no box. */
robo_result_t robo_layout_object_child_count(const robo_layout_tree_t *tree,
                                            robo_layout_node_id_t id,
                                            size_t *out);

/** Distance from the root box (0 for the root). ROBO_NOT_FOUND for a miss. */
robo_result_t robo_layout_object_depth(const robo_layout_tree_t *tree,
                                      robo_layout_node_id_t id,
                                      uint32_t *out);

/** Non-zero when @p id names the tree's root box. */
int robo_layout_object_is_root(const robo_layout_tree_t *tree,
                               robo_layout_node_id_t id);

/** Non-zero when @p id names a box with at least one child. */
int robo_layout_object_has_children(const robo_layout_tree_t *tree,
                                    robo_layout_node_id_t id);

/** Non-zero when @p id names a box (any live kind). */
int robo_layout_object_is_live(const robo_layout_tree_t *tree,
                               robo_layout_node_id_t id);

/** Flag bits of @p id; 0 when the id names no box or carries no flag. */
uint32_t robo_layout_object_flags(const robo_layout_tree_t *tree,
                                  robo_layout_node_id_t id);

/** Non-zero when @p id names a box carrying @p flag. */
int robo_layout_object_has_flag(const robo_layout_tree_t *tree,
                                robo_layout_node_id_t id,
                                uint32_t flag);

/**
 * The box's result, by value.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL args or a NONE id;
 *         ROBO_NOT_FOUND for an id that names no box. A box that exists always
 *         has a result *record*; what varies is its state, so a caller that gets
 *         ROBO_OK still has to read the state to learn whether any geometry is
 *         known.
 */
robo_result_t robo_layout_object_result(const robo_layout_tree_t *tree,
                                       robo_layout_node_id_t id,
                                       robo_layout_result_t *out);

/** Borrowed result of @p id, or NULL when the id names no box. Valid until the
 *  next mutation of the tree, like every borrowed pointer here. */
const robo_layout_result_t *robo_layout_object_result_ref(
    const robo_layout_tree_t *tree,
    robo_layout_node_id_t id);

/**
 * The formatting context @p id's own layout participates in.
 *
 * @return ROBO_OK and the id; ROBO_INVALID_ARGUMENT for NULL args or a NONE id;
 *         ROBO_NOT_FOUND when there is no *live* context to read -- which covers
 *         an id that names no box, a box in a tree no pass has classified, and a
 *         box whose classification has been released by an add, a clear or an
 *         explicit invalidation. All three are one statement to a caller, and
 *         which one it was is a fact about the *tree*:
 *         `robo_layout_tree_is_classified`.
 *
 *         The validation is why a released classification cannot be read back: a
 *         box's id is a *cache* of a conclusion, and a cache that outlives what it
 *         caches is exactly the half-truth this module refuses to hand out.
 */
robo_result_t robo_layout_object_context(const robo_layout_tree_t *tree,
                                        robo_layout_node_id_t id,
                                        robo_layout_fc_id_t *out,
                                        robo_layout_diag_t *diag);

/**
 * The formatting context @p id establishes for its contents, or ROBO_NOT_FOUND
 * when it establishes none.
 *
 * "Establishes none" is the common answer and is not a failure: it means the box
 * is transparent to formatting context and its contents are formatted by an
 * ancestor's. A caller that needs to distinguish it from "not classified yet", or
 * from "classified and since released", asks `robo_layout_tree_is_classified`
 * first -- the same rule the participating-context accessor above states.
 */
robo_result_t robo_layout_object_context_owned(const robo_layout_tree_t *tree,
                                               robo_layout_node_id_t id,
                                               robo_layout_fc_id_t *out,
                                               robo_layout_diag_t *diag);

/** Non-zero when @p id names a box that establishes a context of its own that a
 *  caller can read back right now. 0 for an id that names no box, a box that
 *  establishes none, and a box whose classification has been released. */
int robo_layout_object_establishes_context(const robo_layout_tree_t *tree,
                                           robo_layout_node_id_t id);

/**
 * The line box @p id was placed on, or ROBO_NOT_FOUND when it is on none.
 *
 * "On none" is the common answer for a *box* and a normal one for an object that
 * has not been laid out, and it is reached by three routes that a caller tells apart
 * the way it always does in this module -- by asking the object's own result state,
 * which answers "was anything computed for this object at all?":
 *
 *   the object was never placed -- no pass has run, the pass declined it, or it is
 *   a block-level box, which lives in a block stack and not on a line;
 *
 *   the object was placed by a pass whose lines have since been released (a later
 *   pass, an add, a clear, or an explicit invalidation). A line id is valid until the
 *   next pass (RoboLayoutLineBox.h), and a released one is refused rather than
 *   handed back as a number that names a different line;
 *
 *   the object is a **transparent inline box with contents** -- a `<span>` around
 *   other participants -- which is a participant of its context and a member of no
 *   single line, because its rectangle is the union of what it contains.
 *
 * @return ROBO_OK and the line id (never NONE); ROBO_INVALID_ARGUMENT for NULL
 *         arguments or a NONE id; ROBO_NOT_FOUND as above.
 */
robo_result_t robo_layout_object_line(const robo_layout_tree_t *tree,
                                      robo_layout_node_id_t id,
                                      robo_layout_line_id_t *out,
                                      robo_layout_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_OBJECT_H */
