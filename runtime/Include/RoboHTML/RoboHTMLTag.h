/*
 * RoboHTML element tag identity (Phase 8.11 — basic HTML elements).
 *
 * An element has always had a *name*; this header gives it a *type*. The
 * distinction matters because a name is bytes and a type is a symbol: before
 * this phase a consumer that wanted "is this a heading?" compared strings, so
 * every consumer owned a private copy of the vocabulary and they could drift.
 * Now there is one table, and `robo_html_tag_is_heading` is the only
 * definition of "heading" in the runtime.
 *
 *   Element (8.5)  name + namespace + attributes + children
 *        │
 *        └── robo_html_tag_t  ← the identity derived from (namespace, name)
 *
 * Derived, never stored
 *   Tag identity is a pure function of (namespace, local name), exactly like
 *   the Phase 8.5 classification that now derives from this table. Nothing is
 *   added to a node payload, no arena grows, and a name that arrived before
 *   this phase existed classifies identically to one that arrives after. The
 *   cost is a bounded linear scan at read time — the same shape and the same
 *   cost the classification lookup already had, and it runs when an element
 *   is inspected, not per painted pixel.
 *
 * One vocabulary, three facts per entry
 *   A table row is everything the runtime knows about an element type:
 *   its canonical name, its coarse role (robo_html_element_class_t) and its
 *   intrinsic flags (void, heading, list, ...). The role table that Phase 8.5
 *   owned is gone — not replaced by a second table, replaced by the row
 *   itself — so `robo_html_element_classify` and `robo_html_tag_from_name`
 *   cannot disagree about what `div` is.
 *
 * Symbols, not numbers
 *   ROBO_HTML_TAG_* enumerators are dense and start at 1, so `is_known` is a
 *   range test. The numeric value is an implementation detail: it is not
 *   persisted, not part of any file format, and inserting a tag in the middle
 *   of the enum is a source-compatible change. Anything that needs stable
 *   bytes uses robo_html_tag_name.
 *
 * What this phase is NOT: CSS, selectors, layout, paint, JavaScript, events,
 * custom-element upgrade, foreign-content namespace switching, accessibility
 * mapping, or an exhaustive element vocabulary. A tag that is not in the
 * table is UNKNOWN — which is a supported state, not an error — and the
 * element keeps its name, attributes, children and full DOM behaviour.
 */

#ifndef ROBO_HTML_TAG_H
#define ROBO_HTML_TAG_H

#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Tag identity                                                        */
/* ------------------------------------------------------------------ */

/**
 * A recognised element type.
 *
 * The first group is the vocabulary Phase 8.11 introduces as *basic HTML
 * elements* — the ones a document is actually made of today. The rest is the
 * vocabulary Phase 8.5 already classified by name, promoted to real identity
 * so the classification table and the tag table cannot diverge; every name
 * that could be classified before is a known tag now, with the same role.
 *
 * @c ROBO_HTML_TAG_UNKNOWN is the absence of an identity: a custom element
 * (`<my-widget>`), a future element, or any name in a non-HTML namespace.
 * It is never an error and never prevents the element from working — it means
 * "no element type in this runtime answers to that name".
 */
typedef enum robo_html_tag {
    /** No recognised element type answers to this name. */
    ROBO_HTML_TAG_UNKNOWN = 0,

    /* Document structure (Phase 8.11). */
    ROBO_HTML_TAG_HTML = 1,
    ROBO_HTML_TAG_HEAD = 2,
    ROBO_HTML_TAG_BODY = 3,
    ROBO_HTML_TAG_TITLE = 4,

    /* Structural content (Phase 8.11). */
    ROBO_HTML_TAG_DIV = 5,
    ROBO_HTML_TAG_SPAN = 6,
    ROBO_HTML_TAG_P = 7,
    ROBO_HTML_TAG_BR = 8,

    /* Headings (Phase 8.11). */
    ROBO_HTML_TAG_H1 = 9,
    ROBO_HTML_TAG_H2 = 10,
    ROBO_HTML_TAG_H3 = 11,
    ROBO_HTML_TAG_H4 = 12,
    ROBO_HTML_TAG_H5 = 13,
    ROBO_HTML_TAG_H6 = 14,

    /* Lists (Phase 8.11). */
    ROBO_HTML_TAG_UL = 15,
    ROBO_HTML_TAG_OL = 16,
    ROBO_HTML_TAG_LI = 17,

    /* Inline semantics (Phase 8.11). */
    ROBO_HTML_TAG_STRONG = 18,
    ROBO_HTML_TAG_EM = 19,
    ROBO_HTML_TAG_B = 20,
    ROBO_HTML_TAG_I = 21,
    ROBO_HTML_TAG_U = 22,
    ROBO_HTML_TAG_SMALL = 23,
    ROBO_HTML_TAG_CODE = 24,
    ROBO_HTML_TAG_PRE = 25,

    /* Generic semantic regions (Phase 8.11). */
    ROBO_HTML_TAG_SECTION = 26,
    ROBO_HTML_TAG_ARTICLE = 27,
    ROBO_HTML_TAG_NAV = 28,
    ROBO_HTML_TAG_MAIN = 29,
    ROBO_HTML_TAG_HEADER = 30,
    ROBO_HTML_TAG_FOOTER = 31,
    ROBO_HTML_TAG_ASIDE = 32,

    /* Remaining document metadata (classified by name in 8.5). */
    ROBO_HTML_TAG_BASE = 33,
    ROBO_HTML_TAG_BASEFONT = 34,
    ROBO_HTML_TAG_BGSOUND = 35,
    ROBO_HTML_TAG_LINK = 36,
    ROBO_HTML_TAG_META = 37,
    ROBO_HTML_TAG_NOSCRIPT = 38,
    ROBO_HTML_TAG_SCRIPT = 39,
    ROBO_HTML_TAG_STYLE = 40,
    ROBO_HTML_TAG_TEMPLATE = 41,

    /* Remaining block containers. */
    ROBO_HTML_TAG_ADDRESS = 42,
    ROBO_HTML_TAG_BLOCKQUOTE = 43,
    ROBO_HTML_TAG_DD = 44,
    ROBO_HTML_TAG_DETAILS = 45,
    ROBO_HTML_TAG_DL = 46,
    ROBO_HTML_TAG_DT = 47,
    ROBO_HTML_TAG_FIELDSET = 48,
    ROBO_HTML_TAG_FIGCAPTION = 49,
    ROBO_HTML_TAG_FIGURE = 50,
    ROBO_HTML_TAG_HGROUP = 51,
    ROBO_HTML_TAG_HR = 52,
    ROBO_HTML_TAG_LEGEND = 53,
    ROBO_HTML_TAG_MENU = 54,
    ROBO_HTML_TAG_SUMMARY = 55,

    /* Remaining text-level elements. */
    ROBO_HTML_TAG_A = 56,
    ROBO_HTML_TAG_ABBR = 57,
    ROBO_HTML_TAG_BDI = 58,
    ROBO_HTML_TAG_BDO = 59,
    ROBO_HTML_TAG_CITE = 60,
    ROBO_HTML_TAG_DATA = 61,
    ROBO_HTML_TAG_DEL = 62,
    ROBO_HTML_TAG_DFN = 63,
    ROBO_HTML_TAG_INS = 64,
    ROBO_HTML_TAG_KBD = 65,
    ROBO_HTML_TAG_MARK = 66,
    ROBO_HTML_TAG_Q = 67,
    ROBO_HTML_TAG_RP = 68,
    ROBO_HTML_TAG_RT = 69,
    ROBO_HTML_TAG_RUBY = 70,
    ROBO_HTML_TAG_S = 71,
    ROBO_HTML_TAG_SAMP = 72,
    ROBO_HTML_TAG_SUB = 73,
    ROBO_HTML_TAG_SUP = 74,
    ROBO_HTML_TAG_TIME = 75,
    ROBO_HTML_TAG_VAR = 76,
    ROBO_HTML_TAG_WBR = 77,

    /* Replaced / embedded content. */
    ROBO_HTML_TAG_AREA = 78,
    ROBO_HTML_TAG_AUDIO = 79,
    ROBO_HTML_TAG_CANVAS = 80,
    ROBO_HTML_TAG_EMBED = 81,
    ROBO_HTML_TAG_IFRAME = 82,
    ROBO_HTML_TAG_IMG = 83,
    ROBO_HTML_TAG_MAP = 84,
    ROBO_HTML_TAG_OBJECT = 85,
    ROBO_HTML_TAG_PARAM = 86,
    ROBO_HTML_TAG_PICTURE = 87,
    ROBO_HTML_TAG_SOURCE = 88,
    ROBO_HTML_TAG_TRACK = 89,
    ROBO_HTML_TAG_VIDEO = 90,

    /* Form controls. */
    ROBO_HTML_TAG_BUTTON = 91,
    ROBO_HTML_TAG_DATALIST = 92,
    ROBO_HTML_TAG_FORM = 93,
    ROBO_HTML_TAG_INPUT = 94,
    ROBO_HTML_TAG_LABEL = 95,
    ROBO_HTML_TAG_METER = 96,
    ROBO_HTML_TAG_OPTGROUP = 97,
    ROBO_HTML_TAG_OPTION = 98,
    ROBO_HTML_TAG_OUTPUT = 99,
    ROBO_HTML_TAG_PROGRESS = 100,
    ROBO_HTML_TAG_SELECT = 101,
    ROBO_HTML_TAG_TEXTAREA = 102,

    /* Void elements with no classified role yet. */
    ROBO_HTML_TAG_COL = 103,
    ROBO_HTML_TAG_FRAME = 104,
    ROBO_HTML_TAG_KEYGEN = 105,

    /** Number of defined tags, excluding UNKNOWN. */
    ROBO_HTML_TAG_COUNT = 106
} robo_html_tag_t;

/** Non-zero if @p tag is a defined tag enumerator other than UNKNOWN. */
int robo_html_tag_is_known(robo_html_tag_t tag);

/**
 * Canonical lowercase name of @p tag; "" for UNKNOWN and for an undefined
 * value. The returned pointer is static storage, valid for the life of the
 * process — it is the table's own bytes, not a copy, so it never needs
 * freeing and never goes stale with a document.
 */
const char *robo_html_tag_name(robo_html_tag_t tag);

/**
 * Coarse role of @p tag, as defined by the element model.
 *
 * UNKNOWN — and any undefined value — is @c GENERIC, so an unrecognised
 * element and a recognised-but-unclassified one (`<col>`) report the same
 * role while remaining distinguishable by identity. This is the single
 * definition of an element's role: @c robo_html_element_classify delegates
 * here, so the two can never disagree.
 */
robo_html_element_class_t robo_html_tag_class(robo_html_tag_t tag);

/**
 * Intrinsic flags of @p tag (mask of ROBO_HTML_TAG_FLAG_*); 0 for UNKNOWN.
 *
 * The named predicates below are the readable form of the questions this
 * phase asks. The mask is exposed because a future consumer may need a
 * grouping that is not one of them, and re-deriving it from names would put
 * the vocabulary back in a second place.
 */
unsigned int robo_html_tag_flags(robo_html_tag_t tag);

/**
 * Resolve @p local_name in @p ns to a tag.
 *
 * Comparison is ASCII case-insensitive in both directions, so a caller may
 * pass "DIV", "div" or anything a tokenizer produced and get the same answer
 * — the element model owns the one comparison rule so every caller agrees.
 *
 * Only the HTML namespace is recognised: `ns` that is not HTML yields
 * UNKNOWN whatever the name, because an SVG `<a>` is not an HTML `<a>` and
 * answering otherwise would be a lie. An unrecognised name is UNKNOWN, never
 * an error.
 */
robo_html_tag_t robo_html_tag_from_name(robo_html_namespace_t ns,
                                        const char *local_name);

/** Number of defined tags (ROBO_HTML_TAG_COUNT excluding UNKNOWN). */
size_t robo_html_tag_count(void);

/**
 * Tag at @p index in the vocabulary, for table-driven consumers and for the
 * integrity tests that check the enum and the table agree.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL out);
 *         ROBO_NOT_FOUND (index >= robo_html_tag_count()).
 */
robo_result_t robo_html_tag_at(size_t index, robo_html_tag_t *out);

/**
 * Non-zero when @p tag is an element that never has children or content.
 *
 * This is the one element fact the parser and the DOM both need. The tree
 * builder consults this table rather than keeping its own list of void
 * names, so "is `<br>` void?" has exactly one answer in the runtime.
 */
int robo_html_tag_is_void(robo_html_tag_t tag);

/** Non-zero for the document element `<html>`. */
int robo_html_tag_is_root(robo_html_tag_t tag);

/** Non-zero for `<html>`, `<head>` and `<body>`. */
int robo_html_tag_is_document_structure(robo_html_tag_t tag);

/** Non-zero for elements that belong in `<head>` (metadata). */
int robo_html_tag_is_metadata(robo_html_tag_t tag);

/**
 * Non-zero for the HTML sectioning-content elements: `<article>`, `<aside>`,
 * `<nav>`, `<section>`. `<header>`, `<footer>` and `<main>` are regions, not
 * sectioning content — see robo_html_tag_is_region.
 */
int robo_html_tag_is_sectioning(robo_html_tag_t tag);

/** Non-zero for the page-level regions `<header>`, `<footer>`, `<main>`. */
int robo_html_tag_is_region(robo_html_tag_t tag);

/** Non-zero for `<h1>` … `<h6>`. */
int robo_html_tag_is_heading(robo_html_tag_t tag);

/** Non-zero for the list containers `<ul>` and `<ol>`. */
int robo_html_tag_is_list(robo_html_tag_t tag);

/** Non-zero for the list item `<li>`. */
int robo_html_tag_is_list_item(robo_html_tag_t tag);

/** Non-zero for inline/text-level elements (`<span>`, `<em>`, `<br>`, ...). */
int robo_html_tag_is_text_level(robo_html_tag_t tag);

/**
 * Non-zero for an element that can have a *form owner*: `<button>`,
 * `<fieldset>`, `<input>`, `<object>`, `<output>`, `<select>`, `<textarea>`.
 *
 * This is a fact about the element type, so it is a column of the vocabulary
 * rather than a list inside the forms module — the same reasoning that put
 * void-ness here. Phase 8.14 reads it to decide which elements have an
 * owner at all; a `<div>` inside a `<form>` is not owned by it, and asking
 * "which form owns this?" about a `<div>` would be a category error rather
 * than a lookup that happens to fail.
 */
int robo_html_tag_is_form_associated(robo_html_tag_t tag);

/**
 * Non-zero for an element a `<label>` may be attached to: `<button>`,
 * `<input>`, `<meter>`, `<output>`, `<progress>`, `<select>`, `<textarea>`.
 *
 * Distinct from form-associated: an `<output>` is form-associated but not
 * labelable, and a `<label>` is neither. Phase 8.14 uses it to decide whether
 * a `for="..."` reference names something a label can actually label.
 * `<input type=hidden>` is labelable by this rule but not by the HTML rule;
 * that refinement is a check on the input's *type* and belongs to the caller
 * that has the type, not to a name table.
 */
int robo_html_tag_is_labelable(robo_html_tag_t tag);

/* ------------------------------------------------------------------ */
/* Intrinsic flags                                                     */
/* ------------------------------------------------------------------ */

/** No flags set. */
#define ROBO_HTML_TAG_FLAG_NONE 0u
/** Never has children or content (inserted then immediately popped). */
#define ROBO_HTML_TAG_FLAG_VOID 1u
/** The document element `<html>`. */
#define ROBO_HTML_TAG_FLAG_ROOT 2u
/** One of `<html>`, `<head>`, `<body>`. */
#define ROBO_HTML_TAG_FLAG_DOCUMENT_STRUCTURE 4u
/** Belongs in `<head>`. */
#define ROBO_HTML_TAG_FLAG_METADATA 8u
/** HTML sectioning content (`<article>`, `<aside>`, `<nav>`, `<section>`). */
#define ROBO_HTML_TAG_FLAG_SECTIONING 16u
/** Page-level region (`<header>`, `<footer>`, `<main>`). */
#define ROBO_HTML_TAG_FLAG_REGION 32u
/** `<h1>` … `<h6>`. */
#define ROBO_HTML_TAG_FLAG_HEADING 64u
/** List container (`<ul>`, `<ol>`). */
#define ROBO_HTML_TAG_FLAG_LIST 128u
/** List item (`<li>`). */
#define ROBO_HTML_TAG_FLAG_LIST_ITEM 256u
/** Inline/text-level content. */
#define ROBO_HTML_TAG_FLAG_TEXT_LEVEL 512u
/** Can have a form owner (Phase 8.14). */
#define ROBO_HTML_TAG_FLAG_FORM_ASSOCIATED 1024u
/** A `<label>` may be attached to it (Phase 8.14). */
#define ROBO_HTML_TAG_FLAG_LABELABLE 2048u

/* ------------------------------------------------------------------ */
/* Element-side access                                                  */
/* ------------------------------------------------------------------ */

/*
 * The per-element reads — robo_html_element_tag and robo_html_element_is_tag
 * — are declared in RoboHTMLElement.h, with every other element accessor,
 * because they resolve a node handle against the document payload. This
 * header owns the vocabulary; that header owns the instances.
 */

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TAG_H */
