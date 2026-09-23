/*
 * RoboLayout's resolved font **request** (Phase 12.17).
 *
 * `RoboLayoutText.h`'s `LayoutTextStyle` is the *measurement* seam: four scalars (size, weight, an
 * italic flag, a direction) a measurer can be handed without knowing CSS. What it deliberately does not
 * carry is `font-family` — not because the family does not matter, but because a family is an ordered,
 * variable-length list and `LayoutTextStyle` is copied **by value** into every text run and every paint
 * record (11.6/12.16 store one in a `PaintTextRun`). A pointer or an owned array in that record would
 * make a value type into a lifetime question and would break the by-value copy the record model relies on.
 *
 * So the *selection* question gets its own record, and it is this one:
 *
 *     CssComputedStyle
 *           │  font-family / font-size / font-weight / font-style / font-stretch
 *           ▼
 *     LayoutFontRequest            ← this file (the CSS-side representation, post-cascade and -inheritance)
 *           │  families + sizePx + weight + stretch + italic + direction
 *           ▼
 *     (a font resolver selects a face per run — RoboText's selector over the pack)
 *
 * ## What this file is, and what it is not
 *
 * It **reads CSS and normalizes it**; it does not load a font, does not map a Unicode scalar to a glyph, does
 * not choose a fallback face and does not shape. Those belong to RoboText (Phase 12.5) and the font system
 * (Phase 5), and a second implementation of any of them here would be the "second font system" the phase
 * forbids. What this adds is the one thing that was missing between CSS and RoboText: an ordered family
 * list, classified, with the generic and system keywords kept as keywords rather than flattened to a name.
 *
 * ## Keep the order, keep the keywords
 *
 * A CSS family list is **ordered** and its tail is usually a generic:
 *
 *     font-family: "UI Text", "Inter", sans-serif;
 *
 * Resolving that to a single face here would destroy per-script fallback — the very thing 12.5's selector
 * exists to do. So the list is preserved entry for entry, a **quoted** name keeps its spelling with the
 * quotes removed, and a **keyword** (`serif`, `sans-serif`, `monospace`, `cursive`, `fantasy`, `system-ui`,
 * `emoji`, `math`, `ui-serif`, `ui-sans-serif`, `ui-monospace`, `ui-rounded`) is kept as a *generic* —
 * `RoboFontGenericFamily`, RoboFont's own vocabulary — so this module never invents a family-name mapping
 * of its own. `RoboFontGenericFamilyFromName` is that vocabulary's one reader.
 *
 * ## Initial values, in one place
 *
 * The phase's rule is that CSS defaults live once. The four scalar defaults already live in
 * `RoboLayoutText.h` (`RoboLayoutTextInitialFontSizePx`, the two weights, LTR); the family initial is
 * spelled here (`serif`, which is what `RoboCSSPropertyRegistry.c` records as `font-family`'s own initial
 * value for this build) and `stretch` is `normal`. A document that declares nothing therefore resolves to
 * exactly what the registry says a declaration-less element is, and neither table invents a second answer.
 */
#ifndef ROBO_LAYOUT_FONT_REQUEST_H
#define ROBO_LAYOUT_FONT_REQUEST_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboFont/RoboFontStyle.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutText.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most families a single `font-family` declaration is kept for; the rest are dropped and counted. */
#define RoboLayoutFontMaxFamilies 12u
/** Longest family name this build keeps, in bytes (including the NUL). */
#define RoboLayoutFontMaxFamilyBytes 64u

/**
 * What one family entry is.
 *
 * The distinction is the phase's central one: a **named** family is a string a font database is asked to
 * match, and a **generic** is a keyword whose mapping is configured (`FontGenericResolver`). Flattening a
 * generic to a name here would hard-code `sans-serif → Inter` into the CSS seam, which is exactly the
 * scattered mapping §6 forbids.
 */
typedef enum LayoutFontFamilyKind {
    /** A concrete family name (possibly one the database does not have). */
    LayoutFontFamilyNamed = 0,
    /** A CSS generic or `ui-*` keyword; @ref LayoutFontFamily::generic says which. */
    LayoutFontFamilyGeneric = 1
} LayoutFontFamilyKind;

/** One entry of a family list: a named family or a generic, never both. */
typedef struct LayoutFontFamily {
    /** Which of the two this is. */
    LayoutFontFamilyKind kind;
    /** The generic this entry names when @ref kind is `Generic`; `RoboFontGenericNone` otherwise. */
    FontGenericFamily generic;
    /** The name as written with surrounding quotes removed; `""` for a generic. NUL-terminated. */
    char name[RoboLayoutFontMaxFamilyBytes];
} LayoutFontFamily;

/** Clear @p family to an absent named entry. Safe on NULL. */
void LayoutFontFamilyInit(LayoutFontFamily *family);

/** Non-zero when @p a and @p b are the same entry (kind, generic and case-insensitive name). */
int LayoutFontFamilyEqual(const LayoutFontFamily *a, const LayoutFontFamily *b);

/** Human-readable entry: the name, or the generic's own keyword. `"none"` for NULL. */
const char *LayoutFontFamilyText(const LayoutFontFamily *family);

/** The CSS family list a request carries: entries in author order, bounded. */
typedef struct LayoutFontFamilyList {
    /** The entries, in the order the declaration wrote them. */
    LayoutFontFamily entries[RoboLayoutFontMaxFamilies];
    /** How many entries are in use. */
    uint32_t count;
} LayoutFontFamilyList;

/** Clear @p list to empty (no families). Safe on NULL. */
void LayoutFontFamilyListInit(LayoutFontFamilyList *list);

/** Append @p family. @return RoboOk; RoboInvalidArgument for NULL; RoboAlreadyExists when full. */
Result LayoutFontFamilyListAppend(LayoutFontFamilyList *list,
                                  const LayoutFontFamily *family);

/** Non-zero when @p list names no family at all. Safe on NULL. */
int LayoutFontFamilyListIsEmpty(const LayoutFontFamilyList *list);

/** Field-for-field equality (order matters). Safe on NULL (two NULLs are equal). */
int LayoutFontFamilyListEqual(const LayoutFontFamilyList *a,
                              const LayoutFontFamilyList *b);

/**
 * Parse a `font-family` value's text into @p out, replacing whatever it held.
 *
 * The text is the CSS **computed** value's own (`RoboCssComputedValueFamilyList` keeps the list as
 * written, whitespace-normalized), so the grammar is the small one that survives that step:
 *
 *   - entries are separated by commas;
 *   - an entry is either a run of unquoted words (`Gill Sans`) whose spaces are significant, or a quoted
 *     string in single or double quotes (Ruby pairs escaped with a backslash are **not** unescaped here —
 *     the computed value already carries the text, and this build does not re-enter the tokenizer);
 *   - leading and trailing whitespace around an entry is dropped;
 *   - a case-insensitive match against RoboFont's generic vocabulary makes the entry a **generic**; every
 *     other entry is a **named** family.
 *
 * Overflow is not an error: after @ref RoboLayoutFontMaxFamilies entries the rest are dropped, so a
 * pathological declaration cannot make layout fail.
 *
 * @p length may be 0, in which case @p text is read as a NUL-terminated string (RoboFont's own
 * name-taking convention). An empty or NULL @p text leaves @p out empty.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p out.
 */
Result LayoutFontFamilyListParse(const char *text,
                                 size_t length,
                                 LayoutFontFamilyList *out);

/**
 * The CSS font request, resolved: everything a font selection needs and nothing it does not.
 *
 * This is the CSS-side representation of §3 — the post-cascade, post-inheritance answer — carried as a
 * **value**: the family list is bounded and inline, the scalars are numbers, and there is no pointer in the
 * record. A caller can hold one on its stack, copy it, and pass it to a selector.
 */
typedef struct LayoutFontRequest {
    /** Families, in author order; never holds a pointer. */
    LayoutFontFamilyList families;
    /** Font size in CSS pixels; always finite and > 0. */
    double sizePx;
    /** CSS numeric font weight (the standard scale is 100..900). */
    int32_t weight;
    /** CSS font stretch as a percentage (100 = normal). */
    FontStretch stretch;
    /** Non-zero for `font-style: italic` or `oblique`. */
    int italic;
    /** The declared direction. */
    LayoutTextDirection direction;
    /**
     * Non-zero when the computed style's `font-family` could not be read as a list at all.
     *
     * The one **diagnostic** flag on the record, and it exists so that "the declaration was unusable" and
     * "the declaration was absent" are tellable apart: both resolve to the initial list, and a diagnostic
     * that could not say which is a diagnostic that hides a malformed stylesheet behind a default.
     */
    int familyUnresolved;
} LayoutFontRequest;

/** Clear @p request to the CSS initial values (see the header's "Initial values" note). Safe on NULL. */
void LayoutFontRequestInit(LayoutFontRequest *request);

/**
 * Resolve @p computed into @p outRequest.
 *
 * The Phase 12.17 CSS adapter. It reads five properties and nothing else:
 *
 *   font-family   the family list, through @ref LayoutFontFamilyListParse; the initial list when absent;
 *   font-size     delegated to `LayoutTextStyleResolve`, so a length this build can resolve is a computed
 *                 LENGTH in px and a relative unit takes the initial (10.3's one reader);
 *   font-weight   delegated likewise — a NUMBER clamped to 1..1000, or the four keywords;
 *   font-style    delegated — `italic`/`oblique` set the flag;
 *   font-stretch  a PERCENTAGE (`75%`, `125%`) or a keyword (`normal`, `condensed`, `expanded`, …); the
 *                 initial `normal` otherwise;
 *   direction     delegated — `rtl` selects RTL.
 *
 * The scalar half is `LayoutTextStyleResolve`'s, called rather than re-implemented, so this record and the
 * measurer's scalar inputs cannot disagree about a size or a weight. @p computed may be NULL, which yields
 * the initial values rather than a failure — the same reading every other RoboLayout resolve door gives.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p outRequest.
 */
Result LayoutFontRequestResolve(const CssComputedStyle *computed,
                                LayoutFontRequest *outRequest);

/** Field-for-field equality of the families and the five scalar settings. Safe on NULL. */
int LayoutFontRequestEqual(const LayoutFontRequest *a, const LayoutFontRequest *b);

/**
 * Write a one-line summary of @p request: families, then size, weight, stretch, slope and direction.
 *
 * Complete or absent: RoboNotSupported when it does not fit @p capacity, and the caller may call again with
 * `outLength`. `RoboInvalidArgument` for NULL arguments.
 */
Result LayoutFontRequestWrite(const LayoutFontRequest *request,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_FONT_REQUEST_H */
