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
 *   - Attaching a source through robo_html_parser_attach_source remains
 *     the 8.1 path; the tokenizer consumes a robo_html_source_t directly.
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
typedef struct robo_html_tokenizer robo_html_tokenizer_t;

/** Upper bound on retained diagnostics per tokenizer (bounded memory). */
#define ROBO_HTML_TOKENIZER_MAX_DIAGNOSTICS 64u

/**
 * Create a tokenizer over @p src (borrowed; must stay valid until destroy).
 * @p src NULL or KIND_NONE → ROBO_INVALID_ARGUMENT.
 * @return ROBO_OK, ROBO_INVALID_ARGUMENT, ROBO_OUT_OF_MEMORY,
 *         ROBO_INIT_ERROR when the RoboHTML module is not initialized.
 */
robo_result_t robo_html_tokenizer_create(const robo_html_source_t *src,
                                         robo_html_tokenizer_t **out,
                                         robo_html_diag_t *diag);

/**
 * Read the next token into @p out_tok.
 * @return ROBO_OK with a token (possibly EOF); ROBO_INVALID_ARGUMENT for
 *         NULL args; ROBO_OUT_OF_MEMORY on allocation failure (tokenizer
 *         stays usable for destroy; retrying after destroy is undefined).
 * Repeated reads after EOF keep returning the EOF token (stable, idempotent).
 */
robo_result_t robo_html_tokenizer_next(robo_html_tokenizer_t *tok,
                                       robo_html_token_t *out_tok,
                                       robo_html_diag_t *diag);

/**
 * Copy the current token's tag name into @p out (ASCII-lowercased,
 * NUL-terminated). Capacity must be > 0. A non-tag token copies "".
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p tok / @p out or zero
 *         capacity; ROBO_NOT_SUPPORTED when the name does not fit (never
 *         truncated).
 */
robo_result_t robo_html_token_copy_tag_name(const robo_html_token_t *tok,
                                            char *out,
                                            size_t out_cap);

/**
 * Copy a decoded attribute value by (lowercased) name into @p out.
 * @return ROBO_OK; ROBO_NOT_FOUND when the token has no such attribute;
 *         ROBO_INVALID_ARGUMENT for NULL args.
 */
robo_result_t robo_html_token_copy_attr(const robo_html_token_t *tok,
                                        const char *name,
                                        char *out,
                                        size_t out_cap);

/** Reset tokenizer state to source start (same attached source). */
robo_result_t robo_html_tokenizer_reset(robo_html_tokenizer_t *tok,
                                        robo_html_diag_t *diag);

/** Non-zero after an EOF token has been produced. */
int robo_html_tokenizer_at_eof(const robo_html_tokenizer_t *tok);

/** Copy current state (explicit machine state; testable). */
robo_result_t robo_html_tokenizer_get_state(const robo_html_tokenizer_t *tok,
                                            robo_html_tokenizer_state_t *out);

/** Number of diagnostics recorded so far (bounded; see MAX_DIAGNOSTICS). */
size_t robo_html_tokenizer_diagnostic_count(const robo_html_tokenizer_t *tok);

/** Copy diagnostic number @p index (0-based). OOB → ROBO_NOT_FOUND. */
robo_result_t robo_html_tokenizer_get_diagnostic(
    const robo_html_tokenizer_t *tok,
    size_t index,
    robo_html_diag_t *out_diag);

/**
 * Controlled raw-content entry for the future tree builder (8.3).
 * Switches the machine into @p state (see RoboHTMLTokenizerStates.h).
 * Invalid/unsupported state → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_html_tokenizer_switch_state(robo_html_tokenizer_t *tok,
                                               robo_html_tokenizer_state_t state,
                                               robo_html_diag_t *diag);

/** Destroy the tokenizer; releases all internal buffers. Safe on NULL. */
void robo_html_tokenizer_destroy(robo_html_tokenizer_t *tok);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TOKENIZER_H */
