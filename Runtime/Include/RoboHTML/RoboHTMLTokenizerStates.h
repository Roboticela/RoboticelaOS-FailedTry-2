/*
 * RoboHTML tokenizer states (Phase 8.2).
 *
 * An explicit, testable state machine following the HTML tokenizer model.
 * State names are Roboticela's own; the behavior contracts reference the
 * WHATWG tokenizer states conceptually (the standard is public; no
 * Chromium code, data structures, or identifiers are used).
 *
 * The four content families (data / PLAINTEXT / RCDATA / RAWTEXT / script
 * data) are reached two ways:
 *   - the default DATA flow, and
 *   - HtmlTokenizerSwitchState(), the minimal controlled hook the
 *     future tree builder (8.3) calls after start tags such as <textarea>
 *     (RCDATA), <title> (RCDATA), <style>/<xmp>/iframe contents (RAWTEXT),
 *     <script> (script data) and after <plaintext>.
 *
 * Phase 8.2 implements the data + tag + comment + doctype + character
 * reference states fully; RAWTEXT/RCDATA/script/plaintext families are
 * wired (states exist, entry point exists, basic text behavior works) but
 * their special sub-rules (script escaping, RCDATA reference handling) are
 * flagged for the tree-builder phase.
 */

#ifndef ROBO_HTML_TOKENIZER_STATES_H
#define ROBO_HTML_TOKENIZER_STATES_H

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HtmlTokenizerState {
    /* --- data family --------------------------------------------------- */
    RoboHtmlTokenizerStateData = 0,
    RoboHtmlTokenizerStatePlaintext = 1,

    /* --- markup entry --------------------------------------------------- */
    RoboHtmlTokenizerStateTagOpen = 2,
    RoboHtmlTokenizerStateEndTagOpen = 3,
    RoboHtmlTokenizerStateTagName = 4,

    /* --- attributes ------------------------------------------------------ */
    RoboHtmlTokenizerStateBeforeAttributeName = 5,
    RoboHtmlTokenizerStateAttributeName = 6,
    RoboHtmlTokenizerStateAfterAttributeName = 7,
    RoboHtmlTokenizerStateBeforeAttributeValue = 8,
    RoboHtmlTokenizerStateAttributeValueDoubleQuoted = 9,
    RoboHtmlTokenizerStateAttributeValueSingleQuoted = 10,
    RoboHtmlTokenizerStateAttributeValueUnquoted = 11,
    RoboHtmlTokenizerStateAfterAttributeValueQuoted = 12,
    RoboHtmlTokenizerStateSelfClosingStartTag = 13,

    /* --- markup declarations -------------------------------------------- */
    RoboHtmlTokenizerStateMarkupDeclarationOpen = 14,
    RoboHtmlTokenizerStateBogusComment = 15,
    RoboHtmlTokenizerStateCommentStart = 16,
    RoboHtmlTokenizerStateCommentStartDash = 17,
    RoboHtmlTokenizerStateComment = 18,
    RoboHtmlTokenizerStateCommentLessThan = 19,
    RoboHtmlTokenizerStateCommentLessThanBang = 20,
    RoboHtmlTokenizerStateCommentLessThanBangDashes = 21,
    RoboHtmlTokenizerStateCommentEndDash = 22,
    RoboHtmlTokenizerStateCommentEnd = 23,
    RoboHtmlTokenizerStateCommentEndBang = 24,

    /* --- doctype ---------------------------------------------------------- */
    RoboHtmlTokenizerStateDoctype = 25,
    RoboHtmlTokenizerStateBeforeDoctypeName = 26,
    RoboHtmlTokenizerStateDoctypeName = 27,
    RoboHtmlTokenizerStateAfterDoctypeName = 28,
    RoboHtmlTokenizerStateBogusDoctype = 29,
    RoboHtmlTokenizerStateAfterDoctypePublicKeyword = 30,
    RoboHtmlTokenizerStateBeforeDoctypePublicIdentifier = 31,
    RoboHtmlTokenizerStateDoctypePublicIdentifierDoubleQuoted = 32,
    RoboHtmlTokenizerStateDoctypePublicIdentifierSingleQuoted = 33,
    RoboHtmlTokenizerStateAfterDoctypePublicIdentifier = 34,
    RoboHtmlTokenizerStateBetweenDoctypePublicAndSystemIdentifiers = 35,
    RoboHtmlTokenizerStateAfterDoctypeSystemKeyword = 36,
    RoboHtmlTokenizerStateBeforeDoctypeSystemIdentifier = 37,
    RoboHtmlTokenizerStateDoctypeSystemIdentifierDoubleQuoted = 38,
    RoboHtmlTokenizerStateDoctypeSystemIdentifierSingleQuoted = 39,
    RoboHtmlTokenizerStateAfterDoctypeSystemIdentifier = 40,
    RoboHtmlTokenizerStateBogusDoctypeQuirks = 41,

    /* --- character references --------------------------------------------- */
    RoboHtmlTokenizerStateCharacterReference = 42,
    RoboHtmlTokenizerStateCharacterReferenceNamed = 43,
    RoboHtmlTokenizerStateCharacterReferenceNumeric = 44,
    RoboHtmlTokenizerStateCharacterReferenceNumericHex = 45,
    RoboHtmlTokenizerStateCharacterReferenceNumericEnd = 46,

    /* --- raw content families (tree-builder entry via switch_state) ------ */
    RoboHtmlTokenizerStateRawtext = 47,
    RoboHtmlTokenizerStateRawtextLessThanSign = 48,
    RoboHtmlTokenizerStateRawtextEndTagOpen = 49,
    RoboHtmlTokenizerStateRawtextEndTagName = 50,
    RoboHtmlTokenizerStateRcdata = 51,
    RoboHtmlTokenizerStateRcdataLessThanSign = 52,
    RoboHtmlTokenizerStateRcdataEndTagOpen = 53,
    RoboHtmlTokenizerStateRcdataEndTagName = 54,
    RoboHtmlTokenizerStateScriptData = 55,
    RoboHtmlTokenizerStateScriptDataLessThanSign = 56,
    RoboHtmlTokenizerStateScriptDataEndTagOpen = 57,
    RoboHtmlTokenizerStateScriptDataEndTagName = 58,
    RoboHtmlTokenizerStateScriptDataEscapeStart = 59,
    RoboHtmlTokenizerStateScriptDataEscapeStartDash = 60,
    RoboHtmlTokenizerStateScriptDataEscaped = 61,
    RoboHtmlTokenizerStateScriptDataEscapedDash = 62,
    RoboHtmlTokenizerStateScriptDataEscapedDashDash = 63,
    RoboHtmlTokenizerStateScriptDataEscapedLessThanSign = 64,
    RoboHtmlTokenizerStateScriptDataEscapedEndTagOpen = 65,
    RoboHtmlTokenizerStateScriptDataEscapedEndTagName = 66,
    RoboHtmlTokenizerStateScriptDataDoubleEscapeStart = 67,
    RoboHtmlTokenizerStateScriptDataDoubleEscaped = 68,
    RoboHtmlTokenizerStateScriptDataDoubleEscapedDash = 69,
    RoboHtmlTokenizerStateScriptDataDoubleEscapedDashDash = 70,
    RoboHtmlTokenizerStateScriptDataDoubleEscapedLessThanSign = 71,
    RoboHtmlTokenizerStateScriptDataDoubleEscapedEndTagOpen = 72,
    RoboHtmlTokenizerStateScriptDataDoubleEscapedEndTagName = 73,

    RoboHtmlTokenizerStateCount = 74
} HtmlTokenizerState;

/** Non-zero if @p state is a defined state enumerator. */
int HtmlTokenizerStateIsKnown(HtmlTokenizerState state);

/** Human-readable state name (snake_case); never NULL. */
const char *HtmlTokenizerStateName(HtmlTokenizerState state);

/**
 * Non-zero when @p state belongs to a raw content family (RAWTEXT /
 * RCDATA / script data / PLAINTEXT) in which the tokenizer must not emit
 * start/end-tag tokens from arbitrary markup (only matching end tags).
 */
int HtmlTokenizerStateIsRawContent(
    HtmlTokenizerState state);

/**
 * Minimal controlled state switch for the future tree builder (8.3):
 * after emitting a start tag for e.g. <textarea>, the builder asks the
 * tokenizer to continue in RCDATA; <style> → RAWTEXT; <script> → script
 * data; <plaintext> → PLAINTEXT; DATA resets to ordinary content.
 * @return RoboOk for those five entry points; RoboInvalidArgument for
 *         tokenizer sub-states (unreachable from outside) and for
 *         undefined enumerators.
 * (Phase 8.2 exposes the mechanism; no element semantics are applied.)
 */
Result HtmlTokenizerStateSwitchValidate(
    HtmlTokenizerState state);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TOKENIZER_STATES_H */
