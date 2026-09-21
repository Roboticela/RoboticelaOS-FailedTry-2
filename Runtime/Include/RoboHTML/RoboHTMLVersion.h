/*
 * RoboHTML version pin — Phase 8.11 basic HTML elements milestone.
 *
 * 0.1.0 was the architecture-only surface (8.1); 0.2.0 added the tokenizer
 * (tokens, state machine, character references, raw-content families);
 * 0.3.0 added the tree builder (real document trees, insertion modes,
 * open-element stack, recovery, tree dump); 0.4.0 made the document the
 * authoritative model of that tree — metadata, explicit root ownership,
 * node ownership/cross-document checks, construction closure, reuse and
 * whole-tree invariant auditing; 0.5.0 made the element a first-class part
 * of that model — namespace and classification, element state, and
 * HtmlElementCreate as the single construction seam the tree builder
 * and the future DOM both go through. 0.6.0 replaces the temporary
 * attribute table with the authoritative attribute model: one
 * HtmlAttribute used by the tokenizer's staging, the stored payload
 * and the readers alike; deterministic order; presence distinct from an
 * empty value; and add/set/remove on a live element. 0.7.0 makes character
 * data a modelled node: real Text nodes created through one seam
 * (HtmlTextCreate), content access over the same payload arena every
 * other node kind uses, set/append mutation, byte-exact whitespace and
 * Unicode, and text runs that coalesce per run rather than per token. 0.8.0
 * makes the tree navigable: one authoritative traversal surface for every
 * node kind — relationships in both directions, absolute depth, a
 * deterministic pre-order enumeration, an O(n) child cursor, the
 * all/elements/text filters, and bounded search — replacing three separate
 * copies of the pre-order successor with the one implementation all of them
 * now share. 0.9.0 makes that tree editable: append, insert before, remove,
 * replace and bulk child removal, all through the same linking primitive the
 * parser uses, with one-parent and acyclic invariants enforced, cross-document
 * mutation refused rather than guessed, detachment as a legal state that the
 * whole-tree audit now reports separately from corruption, and a monotonic
 * tree version that lets traversal report an iteration invalidated by a
 * structural change instead of returning a node that is no longer where it
 * was read from. 0.10.0 gives the document an explicit lifecycle: the
 * web platform's own readyState vocabulary (created, loading, interactive,
 * complete, plus failed/destroying/destroyed), a transition graph with no
 * shortcut into completion, the rules that decide which entry point settles
 * where, and lifecycle predicates callers can ask without switching on the
 * enum. 0.11.0 gives an element a *type*: the tag vocabulary in
 * RoboHTMLTag.h names every element this runtime recognises, defines its
 * role and its intrinsic flags (void, heading, list, sectioning, text-level,
 * metadata, document structure) in one table, and replaces the two private
 * copies of that knowledge — the classification table in the element model
 * and the void-element list in the tree builder — so "what is <div>?" and
 * "is <br> void?" now have one answer each, readable by a caller as a
 * symbol instead of a string comparison. 0.12.0 gives `<img>` a resource:
 * RoboHTMLImage.h models the image as a state (none, pending, decoded,
 * failed, unsupported) over a RoboGraphics bitmap, reads `src`/`alt` and
 * parses declared dimensions without trusting them, fetches bytes through an
 * installed resolver instead of doing any I/O itself, ships one safe
 * in-memory asset resolver for local/test assets, refuses scheme references
 * by policy rather than reporting them missing, and attaches the result to
 * the element in a per-document table keyed by node identity. Failure is a
 * state, not an exception, so a bad `src` cannot damage a document. 0.13.0
 * gives anchors a target: RoboHTMLAnchor.h reads `href`/`target`/`rel`/
 * `download`/`title` without interpreting them, resolves a reference against
 * the document's own reference into a navigation target (route, fragment,
 * external, invalid, or none), classifies the target context, refuses
 * schemes by policy before any seam is consulted (`javascript:`, `data:` and
 * `file:` never reach a handler), resolves a fragment locally because that
 * is the one navigation needing no I/O, and reports every activation through
 * an outcome vocabulary with no "navigated" kind — so nothing can claim
 * navigation this runtime did not perform. 0.14.0 gives the form controls a
 * state: RoboHTMLForm.h reads `action`/`method`/`enctype`/`name`/`target`
 * off a `<form>`, `for` off a `<label>` and resolves it to the control it
 * names, the typed attribute set of an `<input>`/`<button>`/`<textarea>`/
 * `<select>`/`<option>` (with `min`/`max`/`step` parsed as numbers rather
 * than compared), the input type vocabulary and its `unknown`→`text`
 * degradation, the select/option ordering and selection rules, and — the
 * point of the phase — the split between a control's *declared* value (the
 * attribute, or a textarea's children) and its *current* value (an explicit
 * override kept in a per-document table beside the element that owns it),
 * the same for checkedness, and a form owner resolved on demand from the
 * live tree. Nothing is cached, so no mutation can leave a stale
 * association. Submission, HTTP, encodings, validation, file and date/range/
 * color inputs, user interaction, events and CSS are explicitly not part of
 * this. The surface still grows in 8.15+, so API freeze is NOT claimed here
 * (RoboHtmlApiFrozen=0).
 */

#ifndef ROBO_HTML_VERSION_H
#define ROBO_HTML_VERSION_H

#define RoboHtmlVersionMajor 0
#define RoboHtmlVersionMinor 14
#define RoboHtmlVersionPatch 0
#define RoboHtmlVersionString "0.14.0"

/** 1 once the Phase 8 surface is frozen (not yet — see header comment). */
#define RoboHtmlApiFrozen 0

#endif /* ROBO_HTML_VERSION_H */
