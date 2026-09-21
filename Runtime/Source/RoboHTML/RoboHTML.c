/*
 * RoboHTML module lifecycle + version (Phase 8.1).
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLVersion.h"

#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>

static int gInitialized;
static HtmlDocumentSlot gSlots[RoboHtmlMaxDocuments];

const char *HtmlVersionString(void)
{
    return RoboHtmlVersionString;
}

Result HtmlGetVersion(I32 *outMajor,
                                    I32 *outMinor,
                                    I32 *outPatch)
{
    if (!outMajor || !outMinor || !outPatch) {
        return RoboInvalidArgument;
    }
    *outMajor = RoboHtmlVersionMajor;
    *outMinor = RoboHtmlVersionMinor;
    *outPatch = RoboHtmlVersionPatch;
    return RoboOk;
}

HtmlDocumentSlot *HtmlDocumentSlotFind(
    HtmlDocumentId id)
{
    size_t i;

    if (id == RoboHtmlDocumentIdNone) {
        return NULL;
    }
    for (i = 0u; i < RoboHtmlMaxDocuments; i++) {
        if (gSlots[i].inUse && gSlots[i].id == id) {
            return &gSlots[i];
        }
    }
    return NULL;
}

HtmlDocumentSlot *HtmlDocumentSlotAlloc(void)
{
    size_t i;

    for (i = 0u; i < RoboHtmlMaxDocuments; i++) {
        if (!gSlots[i].inUse) {
            return &gSlots[i];
        }
    }
    return NULL;
}

int HtmlIsInitialized(void)
{
    return gInitialized;
}

size_t HtmlLiveDocumentCount(void)
{
    size_t i;
    size_t n = 0u;

    if (!gInitialized) {
        return 0u;
    }
    for (i = 0u; i < RoboHtmlMaxDocuments; i++) {
        if (gSlots[i].inUse) {
            n++;
        }
    }
    return n;
}

Result HtmlInit(void)
{
    if (gInitialized) {
        return RoboAlreadyExists;
    }
    if (!BaseIsInitialized()) {
        LogError("robo_html: init refused; RoboBase not initialized");
        return RoboInitError;
    }
    {
        size_t i;
        for (i = 0u; i < RoboHtmlMaxDocuments; i++) {
            gSlots[i].inUse = 0;
            gSlots[i].id = RoboHtmlDocumentIdNone;
        }
    }
    gInitialized = 1;
    LogInfo("robo_html: init %s", RoboHtmlVersionString);
    return RoboOk;
}

void HtmlShutdown(void)
{
    size_t i;

    if (!gInitialized) {
        return;
    }
    /* Deterministic teardown: destroy live documents oldest-first. */
    for (i = 0u; i < RoboHtmlMaxDocuments; i++) {
        if (gSlots[i].inUse) {
            (void)HtmlDocumentDestroy(gSlots[i].id, NULL);
        }
    }
    gInitialized = 0;
    LogInfo("robo_html: shutdown");
}
