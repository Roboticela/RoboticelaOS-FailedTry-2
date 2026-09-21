/*
 * RoboHTML tokenizer API (Phase 8.2).
 *
 * Converts attached HTML source into a sequential token stream for the
 * future tree builder (8.3). The tokenizer is independent of the tree
 * builder and knows nothing about CSS/layout/paint/JS/network.
 *
 * Relationship to the Phase 8.1 parser boundary:
 *   - The tokenizer is a plain object created against a source view; it
 *     does NOT itself take the document's parser lease. Phase 8.3 (tree
 *     builder) will own the lease, create the tokenizer, drive it, and
 *     feed tokens into document node registration.
 *   - Attaching a source through HtmlParserAttachSource remains
 *     the 8.1 path; the tokenizer consumes a HtmlSource directly.
 *
 * Ownership / lifetime:
 *   - The caller owns the tokenizer object (create/destroy) and the
 *     source bytes (borrowed for the tokenizer's lifetime).
 *   - Tokens are borrowing views: valid until the next next()/reset() or
 *     destroy (see RoboHTMLToken.h).
 *   - Malformed HTML is error-tolerant: tokenization continues, diagnostics
 *     are recorded on the tokenizer (bounded count) and surfaced per token.
 *     Only NULL args, invalid state, or allocation failure return errors.
 *
 * The tokenizer processes bytes as UTF-8 (Phase 8.1 encoding boundary:
 * decoded-text input). Ill-formed UTF-8 sequences are emitted as U+FFFD
 * replacement characters via RoboUnicode REPLACE mode and recorded as a
 * diagnostic — never read out of bounds, never fatal.
 *
 * Input completeness: Phase 8.2 takes the whole source at create time
 * (COMPLETE input). The machine itself is re-entrant — every next() call
 * resumes from the stored state and cursor, and `reset()` returns to the
 * start — so a future incremental/chunked provider (Goal 8.2+ / parser
 * MODE_INCREMENTAL) can grow the source between calls without redesigning
 * the states or the token contract. No streaming/network input exists yet.
 */

#ifndef ROBO_HTML_TOKENIZER_H
#define ROBO_HTML_TOKENIZER_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTokenizerStates.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque tokenizer object (layout private to RoboHTML sources). */
typedef struct HtmlTokenizer HtmlTokenizer;

/** Upper bound on retained diagnostics per tokenizer (bounded memory). */
#define RoboHtmlTokenizerMaxDiagnostics 64u

/**
 * Create a tokenizer over @p src (borrowed; must stay valid until destroy).
 * @p src NULL or KIND_NONE → RoboInvalidArgument.
 * @return RoboOk, RoboInvalidArgument, RoboOutOfMemory,
 *         RoboInitError when the RoboHTML module is not initialized.
 */
Result HtmlTokenizerCreate(const HtmlSource *src,
                                         HtmlTokenizer **out,
                                         HtmlDiag *diag);

/**
 * Read the next token into @p outTok.
 * @return RoboOk with a token (possibly EOF); RoboInvalidArgument for
 *         NULL args; RoboOutOfMemory on allocation failure (tokenizer
 *         stays usable for destroy; retrying after destroy is undefined).
 * Repeated reads after EOF keep returning the EOF token (stable, idempotent).
 */
Result HtmlTokenizerNext(HtmlTokenizer *tok,
                                       HtmlToken *outTok,
                                       HtmlDiag *diag);

/**
 * Copy the current token's tag name into @p out (ASCII-lowercased,
 * NUL-terminated). Capacity must be > 0. A non-tag token copies "".
 * @return RoboOk; RoboInvalidArgument for NULL @p tok / @p out or zero
 *         capacity; RoboNotSupported when the name does not fit (never
 *         truncated).
 */
Result HtmlTokenCopyTagName(const HtmlToken *tok,
                                            char *out,
                                            size_t outCap);

/**
 * Copy a decoded attribute value by (lowercased) name into @p out.
 * @return RoboOk; RoboNotFound when the token has no such attribute;
 *         RoboInvalidArgument for NULL args.
 */
Result HtmlTokenCopyAttr(const HtmlToken *tok,
                                        const char *name,
                                        char *out,
                                        size_t outCap);

/** Reset tokenizer state to source start (same attached source). */
Result HtmlTokenizerReset(HtmlTokenizer *tok,
                                        HtmlDiag *diag);

/** Non-zero after an EOF token has been produced. */
int HtmlTokenizerAtEof(const HtmlTokenizer *tok);

/** Copy current state (explicit machine state; testable). */
Result HtmlTokenizerGetState(const HtmlTokenizer *tok,
                                            HtmlTokenizerState *out);

/** Number of diagnostics recorded so far (bounded; see MAX_DIAGNOSTICS). */
size_t HtmlTokenizerDiagnosticCount(const HtmlTokenizer *tok);

/** Copy diagnostic number @p index (0-based). OOB → RoboNotFound. */
Result HtmlTokenizerGetDiagnostic(
    const HtmlTokenizer *tok,
    size_t index,
    HtmlDiag *outDiag);

/**
 * Controlled raw-content entry for the future tree builder (8.3).
 * Switches the machine into @p state (see RoboHTMLTokenizerStates.h).
 * Invalid/unsupported state → RoboInvalidArgument.
 */
Result HtmlTokenizerSwitchState(HtmlTokenizer *tok,
                                               HtmlTokenizerState state,
                                               HtmlDiag *diag);

/** Destroy the tokenizer; releases all internal buffers. Safe on NULL. */
void HtmlTokenizerDestroy(HtmlTokenizer *tok);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TOKENIZER_H */
