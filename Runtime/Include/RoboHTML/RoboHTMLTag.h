/*
 * RoboHTML element tag identity (Phase 8.11 — basic HTML elements).
 *
 * An element has always had a *name*; this header gives it a *type*. The
 * distinction matters because a name is bytes and a type is a symbol: before
 * this phase a consumer that wanted "is this a heading?" compared strings, so
 * every consumer owned a private copy of the vocabulary and they could drift.
 * Now there is one table, and `HtmlTagIsHeading` is the only
 * definition of "heading" in the runtime.
 *
 *   Element (8.5)  name + namespace + attributes + children
 *        │
 *        └── HtmlTag  ← the identity derived from (namespace, name)
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
 *   its canonical name, its coarse role (HtmlElementClass) and its
 *   intrinsic flags (void, heading, list, ...). The role table that Phase 8.5
 *   owned is gone — not replaced by a second table, replaced by the row
 *   itself — so `HtmlElementClassify` and `HtmlTagFromName`
 *   cannot disagree about what `div` is.
 *
 * Symbols, not numbers
 *   ROBO_HTML_TAG_* enumerators are dense and start at 1, so `IsKnown` is a
 *   range test. The numeric value is an implementation detail: it is not
 *   persisted, not part of any file format, and inserting a tag in the middle
 *   of the enum is a source-compatible change. Anything that needs stable
 *   bytes uses HtmlTagName.
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
 * @c RoboHtmlTagUnknown is the absence of an identity: a custom element
 * (`<my-widget>`), a future element, or any name in a non-HTML namespace.
 * It is never an error and never prevents the element from working — it means
 * "no element type in this runtime answers to that name".
 */
typedef enum HtmlTag {
    /** No recognised element type answers to this name. */
    RoboHtmlTagUnknown = 0,

    /* Document structure (Phase 8.11). */
    RoboHtmlTagHtml = 1,
    RoboHtmlTagHead = 2,
    RoboHtmlTagBody = 3,
    RoboHtmlTagTitle = 4,

    /* Structural content (Phase 8.11). */
    RoboHtmlTagDiv = 5,
    RoboHtmlTagSpan = 6,
    RoboHtmlTagP = 7,
    RoboHtmlTagBr = 8,

    /* Headings (Phase 8.11). */
    RoboHtmlTagH1 = 9,
    RoboHtmlTagH2 = 10,
    RoboHtmlTagH3 = 11,
    RoboHtmlTagH4 = 12,
    RoboHtmlTagH5 = 13,
    RoboHtmlTagH6 = 14,

    /* Lists (Phase 8.11). */
    RoboHtmlTagUl = 15,
    RoboHtmlTagOl = 16,
    RoboHtmlTagLi = 17,

    /* Inline semantics (Phase 8.11). */
    RoboHtmlTagStrong = 18,
    RoboHtmlTagEm = 19,
    RoboHtmlTagB = 20,
    RoboHtmlTagI = 21,
    RoboHtmlTagU = 22,
    RoboHtmlTagSmall = 23,
    RoboHtmlTagCode = 24,
    RoboHtmlTagPre = 25,

    /* Generic semantic regions (Phase 8.11). */
    RoboHtmlTagSection = 26,
    RoboHtmlTagArticle = 27,
    RoboHtmlTagNav = 28,
    RoboHtmlTagMain = 29,
    RoboHtmlTagHeader = 30,
    RoboHtmlTagFooter = 31,
    RoboHtmlTagAside = 32,

    /* Remaining document metadata (classified by name in 8.5). */
    RoboHtmlTagBase = 33,
    RoboHtmlTagBasefont = 34,
    RoboHtmlTagBgsound = 35,
    RoboHtmlTagLink = 36,
    RoboHtmlTagMeta = 37,
    RoboHtmlTagNoscript = 38,
    RoboHtmlTagScript = 39,
    RoboHtmlTagStyle = 40,
    RoboHtmlTagTemplate = 41,

    /* Remaining block containers. */
    RoboHtmlTagAddress = 42,
    RoboHtmlTagBlockquote = 43,
    RoboHtmlTagDd = 44,
    RoboHtmlTagDetails = 45,
    RoboHtmlTagDl = 46,
    RoboHtmlTagDt = 47,
    RoboHtmlTagFieldset = 48,
    RoboHtmlTagFigcaption = 49,
    RoboHtmlTagFigure = 50,
    RoboHtmlTagHgroup = 51,
    RoboHtmlTagHr = 52,
    RoboHtmlTagLegend = 53,
    RoboHtmlTagMenu = 54,
    RoboHtmlTagSummary = 55,

    /* Remaining text-level elements. */
    RoboHtmlTagA = 56,
    RoboHtmlTagAbbr = 57,
    RoboHtmlTagBdi = 58,
    RoboHtmlTagBdo = 59,
    RoboHtmlTagCite = 60,
    RoboHtmlTagData = 61,
    RoboHtmlTagDel = 62,
    RoboHtmlTagDfn = 63,
    RoboHtmlTagIns = 64,
    RoboHtmlTagKbd = 65,
    RoboHtmlTagMark = 66,
    RoboHtmlTagQ = 67,
    RoboHtmlTagRp = 68,
    RoboHtmlTagRt = 69,
    RoboHtmlTagRuby = 70,
    RoboHtmlTagS = 71,
    RoboHtmlTagSamp = 72,
    RoboHtmlTagSub = 73,
    RoboHtmlTagSup = 74,
    RoboHtmlTagTime = 75,
    RoboHtmlTagVar = 76,
    RoboHtmlTagWbr = 77,

    /* Replaced / embedded content. */
    RoboHtmlTagArea = 78,
    RoboHtmlTagAudio = 79,
    RoboHtmlTagCanvas = 80,
    RoboHtmlTagEmbed = 81,
    RoboHtmlTagIframe = 82,
    RoboHtmlTagImg = 83,
    RoboHtmlTagMap = 84,
    RoboHtmlTagObject = 85,
    RoboHtmlTagParam = 86,
    RoboHtmlTagPicture = 87,
    RoboHtmlTagSource = 88,
    RoboHtmlTagTrack = 89,
    RoboHtmlTagVideo = 90,

    /* Form controls. */
    RoboHtmlTagButton = 91,
    RoboHtmlTagDatalist = 92,
    RoboHtmlTagForm = 93,
    RoboHtmlTagInput = 94,
    RoboHtmlTagLabel = 95,
    RoboHtmlTagMeter = 96,
    RoboHtmlTagOptgroup = 97,
    RoboHtmlTagOption = 98,
    RoboHtmlTagOutput = 99,
    RoboHtmlTagProgress = 100,
    RoboHtmlTagSelect = 101,
    RoboHtmlTagTextarea = 102,

    /* Void elements with no classified role yet. */
    RoboHtmlTagCol = 103,
    RoboHtmlTagFrame = 104,
    RoboHtmlTagKeygen = 105,

    /** Number of defined tags, excluding UNKNOWN. */
    RoboHtmlTagCount = 106
} HtmlTag;

/** Non-zero if @p tag is a defined tag enumerator other than UNKNOWN. */
int HtmlTagIsKnown(HtmlTag tag);

/**
 * Canonical lowercase name of @p tag; "" for UNKNOWN and for an undefined
 * value. The returned pointer is static storage, valid for the life of the
 * process — it is the table's own bytes, not a copy, so it never needs
 * freeing and never goes stale with a document.
 */
const char *HtmlTagName(HtmlTag tag);

/**
 * Coarse role of @p tag, as defined by the element model.
 *
 * UNKNOWN — and any undefined value — is @c GENERIC, so an unrecognised
 * element and a recognised-but-unclassified one (`<col>`) report the same
 * role while remaining distinguishable by identity. This is the single
 * definition of an element's role: @c HtmlElementClassify delegates
 * here, so the two can never disagree.
 */
HtmlElementClass HtmlTagClass(HtmlTag tag);

/**
 * Intrinsic flags of @p tag (mask of ROBO_HTML_TAG_FLAG_*); 0 for UNKNOWN.
 *
 * The named predicates below are the readable form of the questions this
 * phase asks. The mask is exposed because a future consumer may need a
 * grouping that is not one of them, and re-deriving it from names would put
 * the vocabulary back in a second place.
 */
unsigned int HtmlTagFlags(HtmlTag tag);

/**
 * Resolve @p localName in @p ns to a tag.
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
HtmlTag HtmlTagFromName(HtmlNamespace ns,
                                        const char *localName);

/** Number of defined tags (RoboHtmlTagCount excluding UNKNOWN). */
size_t HtmlTagCount(void);

/**
 * Tag at @p index in the vocabulary, for table-driven consumers and for the
 * integrity tests that check the enum and the table agree.
 * @return RoboOk; RoboInvalidArgument (NULL out);
 *         RoboNotFound (index >= HtmlTagCount()).
 */
Result HtmlTagAt(size_t index, HtmlTag *out);

/**
 * Non-zero when @p tag is an element that never has children or content.
 *
 * This is the one element fact the parser and the DOM both need. The tree
 * builder consults this table rather than keeping its own list of void
 * names, so "is `<br>` void?" has exactly one answer in the runtime.
 */
int HtmlTagIsVoid(HtmlTag tag);

/** Non-zero for the document element `<html>`. */
int HtmlTagIsRoot(HtmlTag tag);

/** Non-zero for `<html>`, `<head>` and `<body>`. */
int HtmlTagIsDocumentStructure(HtmlTag tag);

/** Non-zero for elements that belong in `<head>` (metadata). */
int HtmlTagIsMetadata(HtmlTag tag);

/**
 * Non-zero for the HTML sectioning-content elements: `<article>`, `<aside>`,
 * `<nav>`, `<section>`. `<header>`, `<footer>` and `<main>` are regions, not
 * sectioning content — see HtmlTagIsRegion.
 */
int HtmlTagIsSectioning(HtmlTag tag);

/** Non-zero for the page-level regions `<header>`, `<footer>`, `<main>`. */
int HtmlTagIsRegion(HtmlTag tag);

/** Non-zero for `<h1>` … `<h6>`. */
int HtmlTagIsHeading(HtmlTag tag);

/** Non-zero for the list containers `<ul>` and `<ol>`. */
int HtmlTagIsList(HtmlTag tag);

/** Non-zero for the list item `<li>`. */
int HtmlTagIsListItem(HtmlTag tag);

/** Non-zero for inline/text-level elements (`<span>`, `<em>`, `<br>`, ...). */
int HtmlTagIsTextLevel(HtmlTag tag);

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
int HtmlTagIsFormAssociated(HtmlTag tag);

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
int HtmlTagIsLabelable(HtmlTag tag);

/* ------------------------------------------------------------------ */
/* Intrinsic flags                                                     */
/* ------------------------------------------------------------------ */

/** No flags set. */
#define RoboHtmlTagFlagNone 0u
/** Never has children or content (inserted then immediately popped). */
#define RoboHtmlTagFlagVoid 1u
/** The document element `<html>`. */
#define RoboHtmlTagFlagRoot 2u
/** One of `<html>`, `<head>`, `<body>`. */
#define RoboHtmlTagFlagDocumentStructure 4u
/** Belongs in `<head>`. */
#define RoboHtmlTagFlagMetadata 8u
/** HTML sectioning content (`<article>`, `<aside>`, `<nav>`, `<section>`). */
#define RoboHtmlTagFlagSectioning 16u
/** Page-level region (`<header>`, `<footer>`, `<main>`). */
#define RoboHtmlTagFlagRegion 32u
/** `<h1>` … `<h6>`. */
#define RoboHtmlTagFlagHeading 64u
/** List container (`<ul>`, `<ol>`). */
#define RoboHtmlTagFlagList 128u
/** List item (`<li>`). */
#define RoboHtmlTagFlagListItem 256u
/** Inline/text-level content. */
#define RoboHtmlTagFlagTextLevel 512u
/** Can have a form owner (Phase 8.14). */
#define RoboHtmlTagFlagFormAssociated 1024u
/** A `<label>` may be attached to it (Phase 8.14). */
#define RoboHtmlTagFlagLabelable 2048u

/* ------------------------------------------------------------------ */
/* Element-side access                                                  */
/* ------------------------------------------------------------------ */

/*
 * The per-element reads — HtmlElementTag and HtmlElementIsTag
 * — are declared in RoboHTMLElement.h, with every other element accessor,
 * because they resolve a node handle against the document payload. This
 * header owns the vocabulary; that header owns the instances.
 */

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TAG_H */
