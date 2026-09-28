/*
 * DesktopBridge — allowlisted JS → C verbs (see RoboDesktopBridge.h).
 */

#include "RoboDesktop/RoboDesktopBridge.h"

#include "RoboBase/RoboLog.h"
#include "RoboDesktop/RoboDesktop.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLElement.h"

#include <string.h>

typedef struct BridgeEntry {
    DesktopBridgeVerb verb;
    const char *name;
    const char *status;
} BridgeEntry;

static const BridgeEntry kBridgeTable[] = {
    { RoboDesktopBridgeVerbSystemAbout, "system.about", "About RoboticelaOS" },
    { RoboDesktopBridgeVerbAppOpenInputLab, "app.openInputLab", "Input Lab" },
    { RoboDesktopBridgeVerbLabPrimary, "lab.primary", "Primary" },
    { RoboDesktopBridgeVerbLabSecondary, "lab.secondary", "Secondary" },
    { RoboDesktopBridgeVerbLabToggle, "lab.toggle", "Toggle" },
    { RoboDesktopBridgeVerbLabCheck, "lab.check", "Check" },
};

static const char *const kBridgeRoots[] = {
    "overviewRoot",
    "inputLabRoot",
};

static int BridgeNameEqual(const char *a, const char *b)
{
    size_t index = 0u;

    if (!a || !b) {
        return 0;
    }
    while (a[index] != '\0' && b[index] != '\0') {
        if (a[index] != b[index]) {
            return 0;
        }
        index++;
    }
    return a[index] == '\0' && b[index] == '\0';
}

static void BridgeCopyStatus(char *dest, size_t destSize, const char *src)
{
    size_t index = 0u;

    if (!dest || destSize == 0u) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    while (src[index] != '\0' && index + 1u < destSize) {
        dest[index] = src[index];
        index++;
    }
    dest[index] = '\0';
}

void DesktopBridgeOutcomeInit(DesktopBridgeOutcome *out)
{
    if (!out) {
        return;
    }
    out->verb = RoboDesktopBridgeVerbNone;
    out->applied = 0;
    out->refused = 0;
    out->status[0] = '\0';
}

DesktopBridgeVerb DesktopBridgeVerbFromName(const char *verb)
{
    size_t index;

    if (!verb || verb[0] == '\0') {
        return RoboDesktopBridgeVerbNone;
    }
    for (index = 0u; index < (sizeof(kBridgeTable) / sizeof(kBridgeTable[0])); index++) {
        if (BridgeNameEqual(verb, kBridgeTable[index].name)) {
            return kBridgeTable[index].verb;
        }
    }
    return RoboDesktopBridgeVerbNone;
}

const char *DesktopBridgeVerbName(DesktopBridgeVerb verb)
{
    size_t index;

    for (index = 0u; index < (sizeof(kBridgeTable) / sizeof(kBridgeTable[0])); index++) {
        if (kBridgeTable[index].verb == verb) {
            return kBridgeTable[index].name;
        }
    }
    return NULL;
}

int DesktopBridgeIsAllowed(const char *verb)
{
    return DesktopBridgeVerbFromName(verb) != RoboDesktopBridgeVerbNone;
}

Result DesktopBridgeInvoke(Desktop *desktop,
                           const char *verb,
                           DesktopBridgeOutcome *out,
                           DesktopDiag *diag)
{
    DesktopBridgeOutcome local;
    DesktopBridgeVerb mapped;
    size_t index;
    const BridgeEntry *entry = NULL;
    Result status;

    DesktopBridgeOutcomeInit(&local);
    if (!desktop || !verb) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                               "desktop and verb are required");
    }

    mapped = DesktopBridgeVerbFromName(verb);
    if (mapped == RoboDesktopBridgeVerbNone) {
        local.refused = 1;
        if (out) {
            *out = local;
        }
        LogDebug("robo_desktop: bridge refused unknown verb");
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboNotSupported,
                               "desktop bridge refused an unknown verb");
    }

    for (index = 0u; index < (sizeof(kBridgeTable) / sizeof(kBridgeTable[0])); index++) {
        if (kBridgeTable[index].verb == mapped) {
            entry = &kBridgeTable[index];
            break;
        }
    }
    if (!entry) {
        local.refused = 1;
        if (out) {
            *out = local;
        }
        return RoboNotSupported;
    }

    BridgeCopyStatus(local.status, sizeof(local.status), entry->status);
    status = DesktopSetStatus(desktop, local.status, diag);
    if (status != RoboOk) {
        if (out) {
            *out = local;
        }
        return status;
    }

    local.verb = mapped;
    local.applied = 1;
    if (out) {
        *out = local;
    }
    LogDebug("robo_desktop: bridge applied %s", entry->name);
    return RoboOk;
}

Result DesktopBridgeConsumeDocument(Desktop *desktop,
                                    HtmlDocumentId document,
                                    DesktopBridgeOutcome *out,
                                    DesktopDiag *diag)
{
    DesktopBridgeOutcome local;
    size_t rootIndex;
    Result status;

    DesktopBridgeOutcomeInit(&local);
    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                               "desktop is required");
    }
    if (document == RoboHtmlDocumentIdNone) {
        if (out) {
            *out = local;
        }
        return RoboOk;
    }

    for (rootIndex = 0u; rootIndex < (sizeof(kBridgeRoots) / sizeof(kBridgeRoots[0])); rootIndex++) {
        HtmlNodeHandle root;
        const char *command = NULL;
        size_t commandLength = 0u;

        status = HtmlDocumentGetElementById(document, kBridgeRoots[rootIndex], &root, NULL);
        if (status != RoboOk) {
            continue;
        }
        command = HtmlElementAttributeValue(&root, "data-last-command", &commandLength);
        if (!command || commandLength == 0u) {
            continue;
        }

        status = DesktopBridgeInvoke(desktop, command, &local, diag);
        (void)HtmlElementRemoveAttribute(&root, "data-last-command", NULL);
        if (out) {
            *out = local;
        }
        return status;
    }

    if (out) {
        *out = local;
    }
    return RoboOk;
}
