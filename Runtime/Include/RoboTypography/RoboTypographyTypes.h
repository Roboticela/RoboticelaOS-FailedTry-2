/*
 * RoboTypography types — roles, optical sizes, weights and the resolved style (Phase 12.3).
 *
 * ## The one rule this module exists to state
 *
 * A UI surface does not name a font file. It names a **semantic role**, and the role resolves to a
 * face, a weight, a size and metrics:
 *
 *     UI element  →  TypographyRole  →  TypographyStyle  →  a loaded FontFace + numbers
 *
 * Before 12.3 the desktop shell held one caller-supplied `FontFace` and one `labelSizePx` and drew
 * every label with them, and a document's heading sizes were whatever its author sheet happened to
 * say. Both of those are *bindings*, and a binding that lives in each surface is a binding that
 * drifts. This header is where the vocabulary lives so that there is one place to change.
 *
 * ## Roles, not font names
 *
 * The roles are Roboticela concepts (`WindowTitle`, `DocumentHeading1`, `SystemMonospace`), never a
 * family or a file. Which concrete family and weight each resolves to is the system typeface pack's
 * manifest (Phase 12.2, `RoboFontSystemPack.h`) plus this module's scale
 * (`RoboTypographyScale.h`). Nothing here reads `/System/Fonts/` or knows Inter is a file name.
 *
 * ## Three numbers a reader must not fold together
 *
 *   the **weight**    a numeric CSS weight (400/500/600/700 by the vocabulary below), resolved to
 *                     the nearest weight the packaged family can actually select;
 *   the **size**      CSS pixels, the one number a text run is measured and drawn at;
 *   the **line height** the role's explicit leading, in CSS pixels. It is *not* `size`, and it is
 *                     not the face's own line extent copied into a field: a role declares its
 *                     leading because a heading and a caption with the same size still want
 *                     different vertical rhythm.
 *
 * `ascentPx` and `descentPx` are the selected face's own layout metrics scaled to `sizePx`, filled
 * by `RoboTypographyStyleApplyFontMetrics`. They exist so that a caller placing a baseline does not
 * re-derive them, and so that the *same* face that was measured is the one drawn.
 *
 * ## What is deliberately absent
 *
 * No tracking (`letter-spacing` has nowhere to be applied by the current run walk), no font-family
 * string (that is the pack's resolution, not a style field), no shaping state, no bidi. The style
 * record is the *inputs* of a text run and nothing about how the run is arranged.
 */
#ifndef ROBO_TYPOGRAPHY_TYPES_H
#define ROBO_TYPOGRAPHY_TYPES_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontSystemPack.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Roles                                                               */
/* ------------------------------------------------------------------ */

/**
 * The semantic typography roles this build binds.
 *
 * Grouped by the surface that asks for them (the group predicates below are the grouping), and the
 * order is the enum's order so a reader can walk role → group → entry. `None` is not a role: it is
 * the value of a cleared record, and asking for it is an error rather than a default.
 */
typedef enum RoboTypographyRole {
    RoboTypographyRoleNone = 0,

    /* Desktop: the shell's own surfaces -- the panel and its identity. */
    RoboTypographyRoleDesktopTitle = 1,
    RoboTypographyRoleDesktopSubtitle = 2,
    RoboTypographyRoleWindowTitle = 3,
    RoboTypographyRoleWindowSubtitle = 4,

    /* Chrome: the controls around and inside a surface. */
    RoboTypographyRoleChromeLabel = 5,
    RoboTypographyRoleChromeButton = 6,
    RoboTypographyRoleChromeMenu = 7,
    RoboTypographyRoleChromeMenuItem = 8,
    RoboTypographyRoleChromeTab = 9,
    RoboTypographyRoleChromeAddress = 10,
    RoboTypographyRoleChromeStatus = 11,

    /* Document: the defaults of the HTML the desktop renders. */
    RoboTypographyRoleDocumentBody = 12,
    RoboTypographyRoleDocumentHeading1 = 13,
    RoboTypographyRoleDocumentHeading2 = 14,
    RoboTypographyRoleDocumentHeading3 = 15,
    RoboTypographyRoleDocumentCaption = 16,
    RoboTypographyRoleDocumentCode = 17,

    /* System: the vocabulary a UI toolkit asks for when it means "whatever the system says". */
    RoboTypographyRoleSystemLabel = 18,
    RoboTypographyRoleSystemSecondaryLabel = 19,
    RoboTypographyRoleSystemEmphasis = 20,
    RoboTypographyRoleSystemMonospace = 21
} RoboTypographyRole;

/** Number of defined roles, `None` excluded. */
#define RoboTypographyRoleCount 21u

/** Non-zero if @p role is a defined role (not `None`). */
int RoboTypographyRoleIsValid(RoboTypographyRole role);

/** Stable camel-case name (`"WindowTitle"`, `"DocumentHeading1"`); NULL for `None`/undefined. */
const char *RoboTypographyRoleName(RoboTypographyRole role);

/**
 * Parse a role name (ASCII, case-insensitive, `-`/`_`/space treated as nothing).
 * @return RoboOk; RoboNotFound for an unknown name; RoboInvalidArgument for NULL.
 */
Result RoboTypographyRoleFromName(const char *name,
                                  size_t nameLen,
                                  RoboTypographyRole *out);

/** Non-zero for the four desktop roles. */
int RoboTypographyRoleIsDesktop(RoboTypographyRole role);
/** Non-zero for the seven chrome roles. */
int RoboTypographyRoleIsChrome(RoboTypographyRole role);
/** Non-zero for the six document roles. */
int RoboTypographyRoleIsDocument(RoboTypographyRole role);
/** Non-zero for the four system roles. */
int RoboTypographyRoleIsSystem(RoboTypographyRole role);

/* ------------------------------------------------------------------ */
/* Optical size                                                        */
/* ------------------------------------------------------------------ */

/**
 * The optical-size *class* a role is set at.
 *
 * A class and not a number, because the packaged face's `opsz` axis is not applied by this build
 * (variable-axis application is a later goal) -- and because the class is the durable part: a
 * resolver that eventually maps a class to `opsz` can replace this one without a caller changing.
 * The classes are the brief's own five, in increasing size order.
 */
typedef enum RoboTypographyOpticalSize {
    RoboTypographyOpticalSmall = 0,
    RoboTypographyOpticalBody = 1,
    RoboTypographyOpticalUI = 2,
    RoboTypographyOpticalDisplay = 3,
    RoboTypographyOpticalLargeDisplay = 4
} RoboTypographyOpticalSize;

#define RoboTypographyOpticalSizeCount 5u

/** Non-zero if @p size is a defined class. */
int RoboTypographyOpticalSizeIsValid(RoboTypographyOpticalSize size);

/** Stable lower-case name (`"small"`, `"large-display"`); never NULL; `"unknown"` when undefined. */
const char *RoboTypographyOpticalSizeName(RoboTypographyOpticalSize size);

/** Parse an optical-size name. RoboOk; RoboNotFound; RoboInvalidArgument. */
Result RoboTypographyOpticalSizeFromName(const char *name,
                                         size_t nameLen,
                                         RoboTypographyOpticalSize *out);

/* ------------------------------------------------------------------ */
/* Weights                                                             */
/* ------------------------------------------------------------------ */

/** The four weights this build's scale uses, in CSS numeric terms. */
#define RoboTypographyWeightRegular ((FontWeight)400u)
#define RoboTypographyWeightMedium ((FontWeight)500u)
#define RoboTypographyWeightSemiBold ((FontWeight)600u)
#define RoboTypographyWeightBold ((FontWeight)700u)

/** Number of weights in the vocabulary. */
#define RoboTypographyWeightCount 4u

/** Stable lower-case name (`"semibold"`); NULL when @p weight is not one of the four. */
const char *RoboTypographyWeightName(FontWeight weight);

/** Parse a weight name. RoboOk; RoboNotFound for an unknown name; RoboInvalidArgument. */
Result RoboTypographyWeightFromName(const char *name,
                                    size_t nameLen,
                                    FontWeight *out);

/**
 * The nearest weight *in the vocabulary* to @p requested.
 *
 * Deterministic, and the tie rule is stated rather than left to a comparison's luck: an exact
 * distance tie returns the **lighter** weight, because a rendering that errs lighter is a rendering
 * that errs toward the face the family actually authored first. A weight outside the vocabulary's
 * range clamps to the nearest end. Use it to turn an arbitrary CSS weight into a step the scale can
 * name; it is *not* the axis-clamping rule (that is the selector's, one layer down).
 */
FontWeight RoboTypographyNearestWeight(FontWeight requested);

/* ------------------------------------------------------------------ */
/* The resolved style                                                  */
/* ------------------------------------------------------------------ */

/**
 * A role, resolved: the face to draw with and the numbers to draw it at.
 *
 * **`face` is borrowed**, exactly as `DesktopConfig.face` is: it is owned by the font registry the
 * system typeface pack registered into, and it stays valid for as long as that registry does. A
 * resolver never hands out a face it owns, so a style may be copied, stored and compared freely --
 * and it must not be released. (The one exception is documented at
 * `RoboTypographyResolveGlyphs`, where a *fallback* face is retained by the caller's own
 * `FontFallbackResult`.)
 */
typedef struct RoboTypographyStyle {
    /** The role this style answers. */
    RoboTypographyRole role;
    /** The pack role the face was selected for (`RoboFontSystemRole`). */
    RoboFontSystemRole fontRole;
    /** Borrowed loaded face; NULL when the role could not be resolved. */
    FontFace *face;
    /** The weight actually selected (not necessarily the one requested). */
    FontWeight weight;
    /** CSS pixels. */
    float sizePx;
    /** The role's explicit leading, in CSS pixels. Not `sizePx`. */
    float lineHeightPx;
    /** The role's optical-size class. */
    RoboTypographyOpticalSize opticalSize;
    /** The face's ascent scaled to `sizePx`; 0 when there is no face. */
    float ascentPx;
    /** The face's descent magnitude scaled to `sizePx`; 0 when there is no face. */
    float descentPx;
} RoboTypographyStyle;

/** Clear @p style to a valueless record (`role == None`). Safe on NULL. */
void RoboTypographyStyleInit(RoboTypographyStyle *style);

/**
 * Non-zero when @p style is a usable text input.
 *
 * A style is usable with **no face** -- a role whose family is missing still has a size, a weight
 * and a line height, and a caller that reported an error instead would make a textless desktop
 * impossible to lay out. What is refused is a style with no role, a non-positive size or line
 * height, an undefined optical class, or a weight outside the CSS range.
 */
int RoboTypographyStyleIsValid(const RoboTypographyStyle *style);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTypographyStyleEqual(const RoboTypographyStyle *a,
                             const RoboTypographyStyle *b);

/**
 * Human-readable one-line summary (role, weight, size, leading, optical class, ascent, face).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or a zero capacity; RoboNotSupported when
 *         the line does not fit, in which case @p outLength receives the length it needed and
 *         @p buffer is left empty -- a truncated dump line reads as a *different* style, which is the
 *         one thing a dump must not do.
 */
Result RoboTypographyStyleWrite(const RoboTypographyStyle *style,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength);

/**
 * Fill @p style's `ascentPx`/`descentPx` from its own face, scaled to its `sizePx`.
 *
 * The *same* two doors RoboLayout's measurer uses (`FontFaceLayoutMetrics` then
 * `FontScaleFontUnits`), called here so that a caller placing a baseline and a caller measuring a
 * run read one face's numbers rather than two derivations of them. A style with no face, or a face
 * with no usable metrics, is left with the honest fallback: `ascentPx = sizePx`, `descentPx = 0` --
 * which is what `RoboLayoutTextFont.h` does for the same absence, and not zero, which would
 * collapse a line box.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p style.
 */
Result RoboTypographyStyleApplyFontMetrics(RoboTypographyStyle *style);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TYPOGRAPHY_TYPES_H */
