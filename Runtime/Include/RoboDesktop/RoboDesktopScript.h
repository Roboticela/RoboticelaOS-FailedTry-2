/*
 * The shell's RoboJS bridge — one file, one runtime, one document.
 *
 * `Assert-RoboDesktopBoundaries` refuses RoboJS everywhere under RoboDesktop except here: a shell that
 * evaluated a page's script in every chrome file would invent a second engine surface, and a scene that
 * reached for RoboJS by name would make the exemption a directory rather than a door. The pattern matches
 * `RoboDesktopDocument` for markup and `RoboDesktopCompose` for the compositor.
 *
 * ## Lifecycle
 *
 *   create  `JsRuntimeCreate` → `JsDomHtmlAttach` → `JsRuntimeEvaluate` of the page script;
 *   pump    advance the deterministic clock, then `JsLoopRunUntilIdle` (timers, queued events, microtasks);
 *   click   `JsEventQueue` a `"click"` record for a node the pointer released on;
 *   destroy `JsDomHtmlDetach` then `JsRuntimeDestroy` — RoboJS never destroys the HTML document.
 *
 * The document id is borrowed. Destroy this bridge *before* destroying the document it was attached to.
 *
 * Authority: `Documentation/RoboJS/Integration.md` §4 (the shell runs a page's script).
 */

#ifndef ROBO_DESKTOP_SCRIPT_H
#define ROBO_DESKTOP_SCRIPT_H

#include "RoboBase/RoboError.h"
#include "RoboDesktop/RoboDesktopDiagnostics.h"
#include "RoboHTML/RoboHTMLTypes.h"
#include "RoboLayout/RoboLayoutTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LayoutTree LayoutTree;
typedef struct DesktopScript DesktopScript;

/**
 * Attach a runtime to @p document, install the HTML DOM adapter, and evaluate @p source once.
 *
 * @param sourceLength  byte length of @p source, or `(size_t)-1` / a negative cast for NUL-terminated.
 * @return RoboOk and a live @p out; RoboInvalidArgument for a missing document or source;
 *         RoboOutOfMemory when the runtime cannot be created; the evaluate failure when the script
 *         does not run (the runtime is still destroyed — a half-attached bridge is never handed out).
 */
Result DesktopScriptCreate(HtmlDocumentId document,
                           const char *source,
                           size_t sourceLength,
                           DesktopScript **out,
                           DesktopDiag *diag);

/** Detach the DOM and destroy the runtime. Safe on NULL. */
void DesktopScriptDestroy(DesktopScript *script);

/** Non-zero while a create has succeeded and destroy has not run. */
int DesktopScriptIsLive(const DesktopScript *script);

/**
 * Drive the page's event loop for one host frame: advance the clock, then drain until idle.
 *
 * @param deltaMs  milliseconds to advance the deterministic clock (0 leaves it); typical host frame is 16.
 */
Result DesktopScriptPump(DesktopScript *script, uint64_t deltaMs, DesktopDiag *diag);

/**
 * Queue a `"click"` for the DOM node under @p layoutNode (resolved through @p tree).
 *
 * A missing tree, an unset layout node, or a layout id that does not resolve is a no-op that returns
 * RoboOk — a click on chrome that is not a document node must not fail the frame. The event runs on the
 * next @ref DesktopScriptPump, never inside this call.
 */
Result DesktopScriptPostClick(DesktopScript *script,
                              const LayoutTree *tree,
                              LayoutNodeId layoutNode,
                              double clientX,
                              double clientY,
                              DesktopDiag *diag);

/** The document this bridge was created for, or `RoboHtmlDocumentIdNone`. */
HtmlDocumentId DesktopScriptDocument(const DesktopScript *script);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_SCRIPT_H */
