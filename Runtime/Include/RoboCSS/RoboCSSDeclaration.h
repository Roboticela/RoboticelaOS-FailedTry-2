/*
 * RoboCSS declaration model (Phase 9.1).
 *
 * A declaration is the atom CSS is made of:
 *
 *   padding-left : 12px !important ;
 *   └──── name ─┘ └─ value ─┘ └─ flag ┘
 *
 *   name         the property name as written, borrowed from the source
 *   property     its identity in a table, when the table recognised it
 *   value        the component-value tree (RoboCSSValue.h)
 *   important    the author's request, which the cascade weighs in 9.5
 *   range        where it came from, for diagnostics
 *   state        parsed, or recognised as malformed and skipped
 *
 * The name stays a borrowed slice *and* gains an id rather than being replaced
 * by one, because the two answers are different questions: the slice is what
 * the author wrote (needed for diagnostics, for custom properties, and for
 * anything the table has never heard of), and the id is what the rest of the
 * pipeline switches on.
 *
 * `important` is recorded and never interpreted here. Which declaration beats
 * which is the cascade's decision (9.5), and a model that "helpfully" ordered
 * declarations would be implementing 9.5 by accident.
 *
 * Nothing in this phase decides whether a *value* is valid for a property: a
 * declaration whose name and syntax are fine is PARSED even if the property is
 * unknown or the value is nonsense for it. Semantic validity is 9.3's and
 * 9.9's, and conflating it with syntax here would make the two impossible to
 * tell apart later.
 */

#ifndef ROBO_CSS_DECLARATION_H
#define ROBO_CSS_DECLARATION_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the model knows about a declaration. */
typedef enum CssDeclarationState {
    /** No declaration: a cleared record. */
    RoboCssDeclarationStateNone = 0,
    /** Syntactically a declaration: a name, a value, however the value is spelled. */
    RoboCssDeclarationStateParsed = 1,
    /** Recognisably malformed (no colon, empty name); skipped, and recorded. */
    RoboCssDeclarationStateInvalid = 2
} CssDeclarationState;

/** Number of defined declaration states. */
#define RoboCssDeclarationStateCount 3u

/** Non-zero if @p state is a defined declaration state enumerator. */
int CssDeclarationStateIsKnown(CssDeclarationState state);

/** Human-readable declaration state name; never NULL. */
const char *CssDeclarationStateName(CssDeclarationState state);

/** One declaration. Owns `value`; every slice borrows from the source. */
typedef struct CssDeclaration {
    CssDeclarationState state;
    /** Property name as written, borrowed from the source. */
    CssSlice name;
    /** Property identity, or RoboCssPropertyIdNone when unrecognised. */
    CssPropertyId property;
    /** Non-zero when the declaration carried `!important`. */
    int important;
    /** Extent of the whole declaration, excluding the terminating `;`. */
    CssRange range;
    /** Extent of the name alone. */
    CssRange nameRange;
    /** Extent of the value alone. */
    CssRange valueRange;
    /** Owns its children; disposable. */
    CssValue value;
} CssDeclaration;

/** Clear @p declaration to STATE_NONE. Safe on NULL. */
void CssDeclarationInit(CssDeclaration *declaration);

/**
 * Dispose @p declaration: dispose its value tree and reset to STATE_NONE.
 * Idempotent and safe on NULL.
 */
void CssDeclarationDispose(CssDeclaration *declaration);

/**
 * Begin a declaration named @p name. @p state must be PARSED or INVALID;
 * a NULL declaration or a state of NONE is rejected.
 * @p name may be empty only when @p state is INVALID, because an empty name is
 * exactly what "malformed" looks like.
 * @return RoboOk; RoboInvalidArgument otherwise. The declaration is disposed
 *         first, so a reused record never leaks.
 */
Result CssDeclarationMake(CssDeclaration *declaration,
                                       CssSlice name,
                                       CssDeclarationState state,
                                       CssDiag *diag);

/** Set the importance flag. NULL @p declaration → RoboInvalidArgument. */
Result CssDeclarationSetImportant(CssDeclaration *declaration,
                                                int important,
                                                CssDiag *diag);

/**
 * Move @p value into @p declaration, disposing any value it held. @p value is
 * reset to NONE. A declaration in state NONE is rejected.
 */
Result CssDeclarationSetValue(CssDeclaration *declaration,
                                            CssValue *value,
                                            CssDiag *diag);

/** Record the source range of @p declaration and its name/value parts. */
Result CssDeclarationSetRanges(CssDeclaration *declaration,
                                             CssRange range,
                                             CssRange nameRange,
                                             CssRange valueRange,
                                             CssDiag *diag);

/**
 * Resolve @p declaration's name against @p table and store the identity.
 * Returns the resolved id, or RoboCssPropertyIdNone when the table has no
 * such property — which is a *result*, not a failure, and leaves the
 * declaration untouched. A NULL declaration or table → NONE.
 */
CssPropertyId CssDeclarationResolveProperty(
    CssDeclaration *declaration,
    const CssPropertyTable *table);

/** Non-zero when @p declaration holds a value tree of any kind. NULL → 0. */
int CssDeclarationHasValue(const CssDeclaration *declaration);

/**
 * Non-zero when @p declaration is usable: PARSED, with a non-empty name.
 * An INVALID declaration is a record of a problem, not a usable declaration.
 */
int CssDeclarationIsUsable(const CssDeclaration *declaration);

/** Borrowed name slice; absent for NULL. */
CssSlice CssDeclarationName(const CssDeclaration *declaration);

/** Borrowed value tree; NULL pointer for NULL declaration (see the header). */
const CssValue *CssDeclarationValue(
    const CssDeclaration *declaration);

/* ------------------------------------------------------------------ */
/* Declaration lists                                                   */
/* ------------------------------------------------------------------ */

/** An owning, ordered list of declarations. */
typedef struct CssDeclarationList {
    CssDeclaration *items; /* owned; NULL when count == 0 */
    size_t count;
    size_t capacity;
} CssDeclarationList;

/** Clear @p list to empty. Safe on NULL. */
void CssDeclarationListInit(CssDeclarationList *list);

/** Dispose every declaration and free the array. Idempotent; safe on NULL. */
void CssDeclarationListDispose(CssDeclarationList *list);

/**
 * Append @p declaration by moving it in; @p declaration is left STATE_NONE.
 * @return RoboOk; RoboInvalidArgument for a NULL argument;
 *         RoboOutOfMemory when the array cannot grow (the declaration is
 *         then untouched and still owns its value).
 */
Result CssDeclarationListAppend(CssDeclarationList *list,
                                               CssDeclaration *declaration,
                                               CssDiag *diag);

/** Number of declarations (0 for NULL). */
size_t CssDeclarationListCount(const CssDeclarationList *list);

/** Borrowed declaration at @p index, or NULL when out of range. */
const CssDeclaration *CssDeclarationListAt(
    const CssDeclarationList *list,
    size_t index);

/** Non-zero when @p list holds no declarations (also for NULL). */
int CssDeclarationListIsEmpty(const CssDeclarationList *list);

/** Remove every declaration. Safe on NULL. */
void CssDeclarationListClear(CssDeclarationList *list);

/**
 * Find the first declaration whose property is @p property.
 * @return 1 and writes @p outIndex when found; 0 otherwise (a NULL list,
 *         RoboCssPropertyIdNone, or no match).
 */
int CssDeclarationListFindFirst(const CssDeclarationList *list,
                                        CssPropertyId property,
                                        size_t *outIndex);

/**
 * Find the *last* declaration whose property is @p property. Together with
 * find_first this lets a caller see both ends of a repeated property; which
 * one wins is the cascade's decision (9.5), not this module's.
 * @return 1 and writes @p outIndex when found; 0 otherwise.
 */
int CssDeclarationListFindLast(const CssDeclarationList *list,
                                       CssPropertyId property,
                                       size_t *outIndex);

/** Number of declarations whose property is @p property. */
size_t CssDeclarationListCountOf(const CssDeclarationList *list,
                                         CssPropertyId property);

/* ------------------------------------------------------------------ */
/* Inline-style boundary (9.1.10)                                      */
/* ------------------------------------------------------------------ */

/**
 * Parse the declarations in @p text into @p outList.
 *
 * This is the *whole* inline-style boundary: the DOM keeps the `style`
 * attribute, RoboCSS is handed its text, and a declaration list comes back.
 * RoboCSS never stores the attribute, never reads it back, and owns no second
 * attribute store — the caller passes the bytes, and the caller's buffer stays
 * its own.
 *
 * A real parse as of 9.3, and the same code path a style rule's block uses:
 * the text is tokenized and consumed as a declaration list, so a `style`
 * attribute and the inside of a `{ … }` cannot drift apart.
 *
 * **Ownership: @p text is borrowed, and the declarations borrow from it.** The
 * returned declarations' names and values are slices into the caller's bytes,
 * so @p text must outlive @p outList. RoboCSS still copies nothing, keeps no
 * second attribute store, and never reads the attribute back.
 *
 * @p origin is recorded by the caller later; passing INLINE here is what the
 * call site does, and any origin is accepted (an author-provided fragment is
 * the same text with different provenance).
 *
 * `outError` is always filled: the first CSS problem recovered, or a record of
 * kind NONE when the text was clean. A recovered problem is not a failed call,
 * so RoboOk comes back with a partially recovered list — a malformed
 * declaration is skipped (and appears in the list as an INVALID record) while
 * the valid declarations around it are parsed.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL outList, a NULL text with
 *         a non-zero length, or an unknown origin; RoboOutOfMemory when the
 *         list could not be built (in which case @p outList is left empty).
 */
Result CssDeclarationListParseText(const char *text,
                                                   size_t length,
                                                   CssOrigin origin,
                                                   CssDeclarationList *outList,
                                                   CssError *outError);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_DECLARATION_H */
