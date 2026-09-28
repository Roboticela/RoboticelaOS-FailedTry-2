/*
 * DesktopBridge — allowlisted JS → C verbs (Phase 14.5 / 14.6).
 *
 * Page script stamps `data-last-command` on a document root. After `DesktopScriptPump`,
 * the shell calls DesktopBridgeConsumeDocument: an allowlisted verb runs in C; an unknown
 * verb is refused. No host FS/network/process from a page verb.
 *
 * Authority: Documentation/Runtime/Desktop/ShellArchitecture.md
 */

#ifndef ROBO_DESKTOP_BRIDGE_H
#define ROBO_DESKTOP_BRIDGE_H

#include "RoboBase/RoboError.h"
#include "RoboDesktop/RoboDesktopDiagnostics.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Desktop Desktop;

/** Known verbs. Append only — values are part of the allowlist contract. */
typedef enum DesktopBridgeVerb {
    RoboDesktopBridgeVerbNone = 0,
    RoboDesktopBridgeVerbSystemAbout = 1,
    RoboDesktopBridgeVerbAppOpenInputLab = 2,
    RoboDesktopBridgeVerbLabPrimary = 3,
    RoboDesktopBridgeVerbLabSecondary = 4,
    RoboDesktopBridgeVerbLabToggle = 5,
    RoboDesktopBridgeVerbLabCheck = 6
} DesktopBridgeVerb;

#define RoboDesktopBridgeVerbCount 6u
#define RoboDesktopBridgeStatusMax 64u

typedef struct DesktopBridgeOutcome {
    DesktopBridgeVerb verb;
    /** Non-zero when the verb was allowlisted and applied. */
    int applied;
    /** Non-zero when a non-empty command string was not on the allowlist. */
    int refused;
    /** Status string written to the shell panel (NUL-terminated). */
    char status[RoboDesktopBridgeStatusMax];
} DesktopBridgeOutcome;

void DesktopBridgeOutcomeInit(DesktopBridgeOutcome *out);

/** Non-zero when @p verb is a known allowlisted spelling. */
int DesktopBridgeIsAllowed(const char *verb);

/** Map a spelling to a verb, or RoboDesktopBridgeVerbNone. */
DesktopBridgeVerb DesktopBridgeVerbFromName(const char *verb);

/** Canonical spelling for @p verb, or NULL. */
const char *DesktopBridgeVerbName(DesktopBridgeVerb verb);

/**
 * Run one allowlisted verb against @p desktop (today: panel status).
 *
 * @return RoboOk when applied; RoboNotSupported when refused/unknown;
 *         RoboInvalidArgument when arguments are missing.
 */
Result DesktopBridgeInvoke(Desktop *desktop,
                           const char *verb,
                           DesktopBridgeOutcome *out,
                           DesktopDiag *diag);

/**
 * Read `data-last-command` from known document roots, invoke, then clear the attribute.
 *
 * Roots probed: `overviewRoot`, `inputLabRoot`. Empty / missing is RoboOk with applied=0.
 */
Result DesktopBridgeConsumeDocument(Desktop *desktop,
                                    HtmlDocumentId document,
                                    DesktopBridgeOutcome *out,
                                    DesktopDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_BRIDGE_H */
