/*
 * RoboCSS object model (Phase 18.2).
 *
 * The CSSOM is the *other half* of every model 9.x built. Phase 9 through 18.1
 * turned bytes into structure — tokens, values, declarations, selectors, rules,
 * stylesheets — and read that structure; nothing ever turned it back into text,
 * and nothing ever *changed* it. This header is both: the one canonical text
 * rendering of a model object (`cssText`), and the one mutable style
 * declaration a caller can edit (`element.style`'s job).
 *
 * Three facts a reader needs, because each is a place CSSOM code goes wrong:
 *
 *   - **The text is a rendering, not the source.** `CssOMWrite*` produces a
 *     canonical spelling of the *model*: comments dropped, each run of
 *     whitespace collapsed to one space, a selector name escaped when it must
 *     be. It is deliberately not the CSSOM specification's byte-exact
 *     serialization — no number canonicalisation, no colour normalisation —
 *     and it is deliberately *re-parseable*: feeding the output back through
 *     the parser yields the same model, which is the property a round trip
 *     test asserts and the only one that matters here. `selectorText` and
 *     `cssText` in a browser are the same shape of answer.
 *
 *   - **The style declaration owns its bytes.** A parsed `CssDeclarationList`
 *     borrows from the source it was parsed out of (that is what makes parsing
 *     cheap). A `CssStyleDeclaration` cannot borrow, because the whole point of
 *     `setProperty` is to replace a value after the source is gone — so each
 *     property's name and value are *copied*, the value re-parsed from its own
 *     owned text, and the object is self-contained. That is why there is a
 *     second declaration container in this module rather than a mutable view
 *     of the first.
 *
 *   - **`var()` is not substituted here.** A declaration's value is the
 *     component-value tree as it was declared; substitution is 18.1's, at
 *     computed-value time, and a CSSOM value is the *specified* one. A
 *     `getPropertyValue` on `color: var(--brand)` answers `var(--brand)`,
 *     exactly as a browser's `style.getPropertyValue` does.
 *
 * What this build deliberately does not do, and says so instead of implying it:
 * there is no document binding (no `styleSheets`, no `ownerNode`, no
 * `getComputedStyle`), because RoboCSS has no DOM and the document layer is a
 * peer; there is no live updating of a style context when a rule is inserted;
 * there is no `@media`/`@import` object with an evaluated `media` list; there is
 * no `CSSStyleRule.parentStyleSheet` chain; and a property name is not
 * lowercased on the way in (comparisons are ASCII case-insensitive, so it does
 * not matter, and storing what the caller wrote is the model's habit).
 */

#ifndef ROBO_CSS_OM_H
#define ROBO_CSS_OM_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDeclaration.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSRule.h"
#include "RoboCSS/RoboCSSSelector.h"
#include "RoboCSS/RoboCSSStyleSheet.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Serialization — the model as text                                   */
/* ------------------------------------------------------------------ */

/*
 * Every writer here has the same contract, for the same reason every
 * diagnostics writer in this repository does:
 *
 *   @p buffer    destination, at least @p capacity bytes; may be NULL when
 *                @p capacity is zero, which is how a caller measures first.
 *   @p capacity  bytes available, NUL terminator included.
 *   @p outLength receives the number of bytes the text needs, excluding the
 *                NUL — always, including when it did not fit.
 *
 * @return RoboOk           the text was written, NUL-terminated;
 *         RoboNotSupported  it did not fit, @p outLength names the size a
 *                           second attempt needs, and @p buffer is untouched
 *                           (a partial rendering is never left behind);
 *         RoboInvalidArgument for a NULL @p outLength or a NULL @p buffer
 *                           with a non-zero @p capacity.
 */

/**
 * Write @p value's canonical component-value text.
 *
 * A TOKEN renders as its raw bytes (`10px`, `red`, `"a b"`); a FUNCTION and a
 * BLOCK keep their brackets and render their children between them; a LIST
 * renders its children in order with a single space where whitespace separated
 * them and nothing where it did not, with comments dropped. An empty value
 * renders as the empty string.
 */
Result CssOMWriteValue(const CssValue *value,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength,
                                    CssDiag *diag);

/**
 * Write a selector list as `selectorText`: complex selectors joined by `, `,
 * each a sequence of compounds joined by its combinator's spelling, each
 * compound the concatenation of its simple selectors.
 *
 * Names are escaped when they must be — a class name holding a `:` comes back
 * as `.\3a ` — so the text re-parses to the same selector. `:not(...)` and the
 * An+B family render their parsed argument where one was attached, and the
 * argument text as written otherwise.
 */
Result CssOMWriteSelectorList(const CssSelectorList *list,
                                           char *buffer,
                                           size_t capacity,
                                           size_t *outLength,
                                           CssDiag *diag);

/** Write one complex selector (no trailing comma). */
Result CssOMWriteComplexSelector(const CssComplexSelector *selector,
                                              char *buffer,
                                              size_t capacity,
                                              size_t *outLength,
                                              CssDiag *diag);

/**
 * Write a declaration as `name: value;`, appending ` !important` before the
 * `;` when the declaration carries the flag. The name is the spelling the
 * author wrote (a declaration's name is a borrowed slice), and the value is
 * the value writer's rendering. A cleared declaration renders as the empty
 * string, not as `: ;`.
 */
Result CssOMWriteDeclaration(const CssDeclaration *declaration,
                                          char *buffer,
                                          size_t capacity,
                                          size_t *outLength,
                                          CssDiag *diag);

/** Write a declaration list as its declarations joined by a single space. */
Result CssOMWriteDeclarationList(const CssDeclarationList *list,
                                              char *buffer,
                                              size_t capacity,
                                              size_t *outLength,
                                              CssDiag *diag);

/**
 * Write a rule's `cssText`.
 *
 * A style rule renders `selectors { declarations }`, its body's declarations
 * joined by a space and the block always spelling both braces — an empty block
 * is `p { }`. An at-rule renders `@name prelude` followed by `;` when it has no
 * block, and by `{ … }` when it has one; a block whose contents are rules
 * renders those rules, a block whose contents are descriptors renders those
 * declarations, and a block holding neither is `{ }`.
 */
Result CssOMWriteRule(const CssRule *rule,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   CssDiag *diag);

/** Write a rule list as its rules joined by a newline. */
Result CssOMWriteRuleList(const CssRuleList *list,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *outLength,
                                       CssDiag *diag);

/**
 * Write a live stylesheet's rules as their `cssText`, joined by newlines.
 * @return RoboNotFound for an id that does not resolve (so a stale handle is
 *         not silently rendered as an empty sheet).
 */
Result CssOMWriteStylesheet(CssStylesheetId id,
                                         char *buffer,
                                         size_t capacity,
                                         size_t *outLength,
                                         CssDiag *diag);

/* ------------------------------------------------------------------ */
/* The mutable style declaration                                       */
/* ------------------------------------------------------------------ */

/**
 * A self-contained, mutable set of declarations: `element.style`'s shape.
 *
 * Entries keep their insertion order, and a property set twice keeps its first
 * position and takes the new value — the CSSOM's rule, and the reason a
 * `setProperty` is a *replace* rather than an append. Names compare ASCII
 * case-insensitively (CSS property names do), and are stored as written.
 *
 * The object is opaque: create it, fill it, read it, destroy it.
 */
typedef struct CssStyleDeclaration CssStyleDeclaration;

/**
 * Create an empty style declaration.
 * @return RoboOk; RoboInvalidArgument for a NULL output; RoboOutOfMemory.
 */
Result CssStyleDeclarationCreate(CssStyleDeclaration **outDeclaration,
                                             CssDiag *diag);

/** Dispose @p declaration and every property it owns. Safe on NULL. */
void CssStyleDeclarationDestroy(CssStyleDeclaration *declaration);

/**
 * Remove every property, keeping the object usable. Safe on NULL.
 */
void CssStyleDeclarationClear(CssStyleDeclaration *declaration);

/**
 * Replace every property with those parsed from @p text.
 *
 * The parse is the same grammar an inline `style` attribute goes through, so a
 * declaration and an attribute cannot drift apart. Malformed declarations are
 * skipped and reported through @p outError, which is always filled — a
 * recovered problem is not a failed call, exactly as at the declaration
 * boundary. An INVALID declaration contributes nothing: a style declaration
 * holds properties, and a malformed record is not one.
 *
 * @p text is borrowed for the duration of the call only: every property's
 * name and value are copied, so the caller's buffer may die on return. That is
 * the one ownership difference from `CssDeclarationListParseText`.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL declaration or a NULL text
 *         with a non-zero length; RoboOutOfMemory when a property could not
 *         be copied (the declaration is then left empty rather than half-built).
 */
Result CssStyleDeclarationParseText(CssStyleDeclaration *declaration,
                                                 const char *text,
                                                 size_t length,
                                                 CssError *outError,
                                                 CssDiag *diag);

/**
 * Set a property, replacing any property of the same name.
 *
 * @p name may be a regular property name or a custom property (`--x`); it must
 * be non-empty and contain no whitespace, `:`, `{`, `}` or NUL. @p valueText is
 * parsed as a component-value list through the same grammar a declaration uses.
 * An empty @p valueText is CSSOM's *removal*: the property is removed, if it
 * was there, and nothing is added.
 *
 * @p priority is either NULL/empty or the string `important` (ASCII
 * case-insensitive); anything else is refused rather than stored, because a
 * priority nothing can read back is worse than a rejected call.
 *
 * A value that does not parse, or that parses to nothing, leaves the object
 * unchanged and is reported through @p outError — a value the grammar could not
 * read is not a property with an empty value.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL declaration, an unusable name
 *         or an unknown priority; RoboOutOfMemory.
 */
Result CssStyleDeclarationSetProperty(CssStyleDeclaration *declaration,
                                                   const char *name,
                                                   const char *valueText,
                                                   const char *priority,
                                                   CssError *outError,
                                                   CssDiag *diag);

/**
 * Remove the property named @p name.
 *
 * When @p outValue is non-NULL the removed value's canonical text is written
 * into it (CSSOM returns the removed value); the writer rules above apply, and
 * a buffer too small is RoboNotSupported with the property still removed — the
 * removal happened, and the rendering is what did not fit.
 *
 * @return RoboOk; RoboNotFound when no property of that name exists (the
 *         property not being there is a fact, not a failure of the call, but it
 *         is the fact the caller asked about); RoboInvalidArgument for a NULL
 *         declaration or name.
 */
Result CssStyleDeclarationRemoveProperty(CssStyleDeclaration *declaration,
                                                      const char *name,
                                                      char *outValue,
                                                      size_t outCapacity,
                                                      size_t *outLength,
                                                      CssDiag *diag);

/** Number of properties (0 for NULL). */
size_t CssStyleDeclarationLength(
    const CssStyleDeclaration *declaration);

/**
 * Borrowed name of the property at @p index in insertion order, or an absent
 * slice when out of range. This is CSSOM's `item()`: the name, never a value.
 */
CssSlice CssStyleDeclarationNameAt(
    const CssStyleDeclaration *declaration,
    size_t index);

/** Non-zero when a property named @p name is present (ASCII case-insensitive). */
int CssStyleDeclarationContains(
    const CssStyleDeclaration *declaration,
    const char *name);

/**
 * Write the value of the property named @p name, canonically.
 *
 * This is CSSOM's `getPropertyValue`, and like it the answer is the value
 * *as declared*: `color: var(--brand)` answers `var(--brand)`, because
 * substitution is 18.1's and happens at computed-value time.
 *
 * @return RoboOk; RoboNotFound when no such property exists (a browser answers
 *         `""`; this build says which of the two empty answers it is);
 *         RoboNotSupported when the text does not fit.
 */
Result CssStyleDeclarationGetPropertyValue(
    const CssStyleDeclaration *declaration,
    const char *name,
    char *buffer,
    size_t capacity,
    size_t *outLength,
    CssDiag *diag);

/**
 * Non-zero when the property named @p name carries `!important`.
 * 0 for a name that is not present, which is what CSSOM's `""` priority means.
 */
int CssStyleDeclarationGetPropertyPriority(
    const CssStyleDeclaration *declaration,
    const char *name);

/**
 * Write the declaration as `cssText`: every property as `name: value;`,
 * in insertion order, joined by a single space. An empty declaration writes
 * the empty string.
 */
Result CssStyleDeclarationCssText(
    const CssStyleDeclaration *declaration,
    char *buffer,
    size_t capacity,
    size_t *outLength,
    CssDiag *diag);

/* ------------------------------------------------------------------ */
/* Rule editing                                                        */
/* ------------------------------------------------------------------ */

/**
 * Insert the rule spelled by @p ruleText into @p id's stylesheet at @p index.
 *
 * This is CSSOM's `insertRule`: the text is parsed as a stylesheet and must
 * yield *exactly one* rule — a stylesheet body, or a second rule, is refused
 * rather than half-inserted, because an insertion that consumed only part of
 * its text would make the sheet's rules disagree with the sheet's source.
 *
 * **@p ruleText is borrowed and must outlive the rule.** A rule's slices point
 * into the bytes it was parsed from, exactly as they do for a parsed
 * stylesheet, and a stylesheet does not take ownership of a caller's buffer
 * (`CssStylesheetSetSourceMemory` states the same contract). A caller that
 * wants the insertion to own its text copies it first.
 *
 * @p index is clamped to the rule count: an index past the end appends, which
 * is what makes `insertRule(text, sheet.cssRules.length)` — the idiom every
 * caller writes — not need a second call.
 *
 * What it does not do: the stylesheet is not re-parsed, no style context is
 * notified, and a rule that a cascade had already collected is not re-collected.
 * A caller holding a live context must treat an edit as a document change.
 *
 * @return RoboOk; RoboNotFound for an id that does not resolve;
 *         RoboInvalidArgument for NULL text with a non-zero length, or text
 *         that is not exactly one rule (reported through @p outError);
 *         RoboOutOfMemory.
 */
Result CssOMStylesheetInsertRule(CssStylesheetId id,
                                             const char *ruleText,
                                             size_t length,
                                             size_t index,
                                             CssError *outError,
                                             CssDiag *diag);

/**
 * Remove the top-level rule at @p index, disposing it — CSSOM's `deleteRule`.
 * @return RoboOk; RoboNotFound for an id that does not resolve or an index out
 *         of range.
 */
Result CssOMStylesheetDeleteRule(CssStylesheetId id,
                                             size_t index,
                                             CssDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_OM_H */
