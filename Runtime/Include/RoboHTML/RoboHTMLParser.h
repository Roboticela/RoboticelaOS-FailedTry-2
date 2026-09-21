/*
 * RoboHTML parser boundary (Phase 8.1) — architecture only.
 *
 * The seam between HTML source and the future tokenizer (8.2) /
 * tree builder (8.3):
 *
 *   HtmlSource (RoboHTMLSource.h)
 *        │  attach
 *        ▼
 *   HtmlParser  ── feeds ──▶  document node registry (RoboHTMLNode.h)
 *        │                             + future DOM (RoboHTMLDom.h)
 *        ▼
 *   parser state machine (see below)
 *
 * Design decisions frozen here so later phases plug in without redesign:
 *   - One parser serves one document at a time (exclusive lease).
 *   - Parsers are heap objects created against an existing document;
 *     they never outlive their document (document destroy fails a live
 *     parser lease by retiring the parser first).
 *   - Input modes distinguish COMPLETE (whole source attached once) from
 *     INCREMENTAL (chunks appended across calls; 8.2+).
 *   - The parser never decodes text; decoded input arrives through the
 *     encoding boundary (RoboHTMLEncoding.h).
 *
 * No tokenizer states, no character references, no parsing algorithms.
 */

#ifndef ROBO_HTML_PARSER_H
#define ROBO_HTML_PARSER_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HtmlParserMode {
    RoboHtmlParserModeNone = 0,
    /** Whole source supplied once; parse runs to completion. */
    RoboHtmlParserModeComplete = 1,
    /** Source appended in chunks (Phase 8.2+). */
    RoboHtmlParserModeIncremental = 2
} HtmlParserMode;

/** Non-zero if @p mode is a defined parser mode enumerator. */
int HtmlParserModeIsKnown(HtmlParserMode mode);

/** Human-readable parser mode name; never NULL. */
const char *HtmlParserModeName(HtmlParserMode mode);

typedef enum HtmlParserState {
    RoboHtmlParserStateIdle = 0,
    /** Source attached; input pending (COMPLETE: ready to feed 8.2). */
    RoboHtmlParserStateFeeding = 1,
    /** Input exhausted; tree builder may finalize (8.3). */
    RoboHtmlParserStateComplete = 2,
    /** Parser hit a boundary failure (diagnostics carry the domain). */
    RoboHtmlParserStateFailed = 3,
    /** Detached from its document (parser object still owned by caller). */
    RoboHtmlParserStateDetached = 4
} HtmlParserState;

/** Non-zero if @p state is a defined parser state enumerator. */
int HtmlParserStateIsKnown(HtmlParserState state);

/** Human-readable parser state name; never NULL. */
const char *HtmlParserStateName(HtmlParserState state);

/**
 * Opaque parser object. Layout is private (RoboHTMLInternal.h); public
 * code only holds the pointer. NULL is never a valid parser.
 */
typedef struct HtmlParser HtmlParser;

/**
 * Create a parser leased to @p document.
 * The document must be in CREATED, LOADING or READY lifecycle - i.e. any
 * state that accepts content. LOADING qualifies because it is the state
 * that means "a source is attached and awaiting parsing". The parser starts
 * in STATE_IDLE with MODE_NONE; attach_source picks the mode.
 * Ownership: caller owns the parser object and must eventually call
 * HtmlParserDestroy. Destroying the document while a parser is
 * leased is rejected with RoboBusy (lease must be released first).
 */
Result HtmlParserCreate(HtmlDocumentId documentId,
                                      HtmlParser **outParser,
                                      HtmlDiag *diag);

/**
 * Attach @p src and transition to STATE_FEEDING.
 * COMPLETE sources move through FEEDING → COMPLETE on finish (8.2+);
 * in 8.1 attaching a COMPLETE source marks the document LOADING and the
 * parser stays FEEDING — feeding/finalization are later-phase work.
 * Re-attach while FEEDING → RoboBusy. After FAILED → ROBO_INVALID_STATE
 * semantics via RoboError (diagnostics record the parser domain).
 */
Result HtmlParserAttachSource(HtmlParser *parser,
                                             const HtmlSource *src,
                                             HtmlDiag *diag);

/**
 * Detach the parser from its document and release the document lease.
 * Document returns to READY (from LOADING). Parser → STATE_DETACHED.
 * Idempotent for an already-detached parser (RoboOk).
 */
Result HtmlParserDetach(HtmlParser *parser,
                                      HtmlDiag *diag);

/** Copy current parser state; NULL parser → RoboInvalidArgument. */
Result HtmlParserGetState(const HtmlParser *parser,
                                         HtmlParserState *outState);

/** Copy current parser mode; NULL parser → RoboInvalidArgument. */
Result HtmlParserGetMode(const HtmlParser *parser,
                                        HtmlParserMode *outMode);

/** Owning document id of @p parser (NONE when detached); NULL → NONE. */
HtmlDocumentId HtmlParserDocument(
    const HtmlParser *parser);

/**
 * Destroy a parser created by HtmlParserCreate. Detaches first if
 * still leased. Safe on NULL. After destroy the pointer must not be used.
 */
void HtmlParserDestroy(HtmlParser *parser);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_PARSER_H */
