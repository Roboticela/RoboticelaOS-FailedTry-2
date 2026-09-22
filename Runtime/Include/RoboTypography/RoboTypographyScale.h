/*
 * RoboTypography scale — the one table every surface reads (Phase 12.3).
 *
 * ## Why a table and not constants at each call site
 *
 * The phase's requirement is that a UI element maps to a *role*, and a role maps to one type
 * setting. If `WindowTitle` were `13px` at the window's paint site and `12px` at its measurement
 * site, or if `h1` were 32px in the document defaults and 28px in a heading test, there would be no
 * typography system -- only several spellings of one that happen to agree today. So the numbers live
 * here, once, in an array a caller can read, dump, replace and test.
 *
 * ## The scale is data, and a caller may replace it
 *
 * `RoboTypographyResolverConfig` takes a scale; passing none uses `RoboTypographyDefaultScale`. That
 * is what makes a restyle a *dataset* change rather than a code change, and it is why the scale is a
 * plain array of entries rather than a switch statement: a test can build a scale with one entry
 * changed and prove the change reaches the pixels without patching the resolver.
 *
 * ## The size steps
 *
 * Six deliberate steps -- Display 32, Heading 24, Title 19, Body 16, Caption 13, Label 11/12 -- with
 * roles placed on them rather than each role inventing a size. The largest values are the document
 * headings (the one place a document legitimately wants display type); the UI roles cluster in the
 * 11–13 px band the shell's 24/30 px bands were drawn for, so binding them changes the *face* and
 * the *weight* without changing the geometry the window metrics already fixed.
 *
 * ## Document defaults are generated from this table, not written beside it
 *
 * `RoboTypographyBuildDocumentStyleSheet` emits the document roles as CSS and is intended to be
 * added to a document as a **runtime-origin** sheet (lower priority than the author's own), so the
 * defaults a document gets for `body`, `h1`, `h2`, `h3`, `small` and `code` are this table's numbers
 * and never a second copy in the document builder. That is the smallest CSS integration the phase
 * asks for: no `@font-face`, no `font-family`, no matching -- only the default type setting of the
 * elements a default exists for.
 *
 * ## What the sheet deliberately does not carry
 *
 * A `font-family` declaration, because face selection is the pack's resolution and CSS font
 * matching is a later phase. `RoboFontSystemPackApplyGenericConfig` is the pack's bridge for that
 * day. `line-height` *is* emitted, and it is worth being exact about why: the desktop consumes the
 * role's leading today (it places a baseline from it), while RoboLayout does not yet resolve
 * `line-height` as a used value (`RoboLayoutVersion.h` says so) -- so the declaration is the
 * document's half of a contract this phase states and a layout phase will honour.
 */
#ifndef ROBO_TYPOGRAPHY_SCALE_H
#define ROBO_TYPOGRAPHY_SCALE_H

#include "RoboBase/RoboError.h"
#include "RoboTypography/RoboTypographyTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One role's type setting. */
typedef struct RoboTypographyScaleEntry {
    RoboTypographyRole role;
    /** Which pack role the face is selected for. */
    RoboFontSystemRole fontRole;
    /** The weight the role is set at. */
    FontWeight weight;
    /** CSS pixels. */
    float sizePx;
    /** The role's leading, in CSS pixels. */
    float lineHeightPx;
    /** The optical-size class the role is set at. */
    RoboTypographyOpticalSize opticalSize;
} RoboTypographyScaleEntry;

/** Entries in the default scale -- one per defined role. */
#define RoboTypographyScaleCount RoboTypographyRoleCount

/** Bytes the largest generated document sheet can need. */
#define RoboTypographyDocumentSheetMaxBytes 4096u

/**
 * Copy this build's default scale into @p out.
 *
 * @param out       receives up to @p capacity entries.
 * @param capacity  entries @p out can hold.
 * @param outCount  optional; receives the number written.
 * @return RoboOk; RoboInvalidArgument for a NULL @p out or a zero @p capacity;
 *         RoboInvalidArgument with a `SCALE` diagnostic when @p capacity is too small for the whole
 *         scale -- a partial scale is refused rather than truncated, because a scale missing a role
 *         would make that role unresolvable while looking like a restyle.
 */
Result RoboTypographyDefaultScale(RoboTypographyScaleEntry *out,
                                  size_t capacity,
                                  size_t *outCount);

/**
 * The entry for @p role, from this build's default scale.
 *
 * @return RoboOk; RoboInvalidArgument for an undefined @p role or NULL @p out; RoboNotFound with a
 *         `SCALE` diagnostic when the scale has no entry for @p role.
 */
Result RoboTypographyScaleForRole(RoboTypographyRole role,
                                  RoboTypographyScaleEntry *out);

/** The entry for @p role in a caller's own scale, or NULL (including for an undefined role). */
const RoboTypographyScaleEntry *RoboTypographyScaleFindEntry(
    const RoboTypographyScaleEntry *scale,
    size_t scaleCount,
    RoboTypographyRole role);

/**
 * Non-zero when @p entry is a type setting this build can draw.
 *
 * The numbers are checked rather than trusted for the same reason the desktop's metrics are: a size
 * of zero or a line height below the size is a setting that would collapse or overlap a line, and a
 * caller that built its own scale deserves to be told at the seam rather than in the pixels.
 */
int RoboTypographyScaleEntryIsValid(const RoboTypographyScaleEntry *entry);

/**
 * The document role a HTML element's default type setting comes from.
 *
 * The vocabulary is deliberately small and named rather than derived: the six document roles, and
 * the elements a default exists for. Elements with no default of their own (a `p`, a `li`, a `td`,
 * a `span`) answer `RoboNotFound` and **inherit the body's setting**, which is exactly what the
 * generated sheet relies on -- it declares a default only where the default differs from body.
 *
 * @return RoboOk and @p out; RoboNotFound when the element has no default of its own;
 *         RoboInvalidArgument for a NULL name or @p out.
 */
Result RoboTypographyRoleForHtmlElement(const char *name,
                                        size_t nameLen,
                                        RoboTypographyRole *out);

/**
 * Write the document defaults as CSS.
 *
 * One rule per document element group, in a stable order, each carrying the role's `font-size`,
 * `font-weight` and `line-height`. Deterministic: the same scale produces byte-identical text, which
 * is what lets a document's frame checksum be a statement about its typography.
 *
 * @param scale       entries to emit from; NULL means the default scale.
 * @param scaleCount  0 with a NULL @p scale means the default scale.
 * @param buffer      receives NUL-terminated CSS.
 * @param capacity    bytes @p buffer can hold, NUL included.
 * @param outLength   optional; receives the text length, excluding the NUL.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p buffer or a zero @p capacity; RoboNotSupported
 *         with a `DOCUMENT` diagnostic when the text does not fit, in which case @p outLength
 *         receives the length it needed.
 */
Result RoboTypographyBuildDocumentStyleSheet(const RoboTypographyScaleEntry *scale,
                                             size_t scaleCount,
                                             char *buffer,
                                             size_t capacity,
                                             size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TYPOGRAPHY_SCALE_H */
