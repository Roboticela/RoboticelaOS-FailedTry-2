/*
 * RoboDesktop ↔ RoboJS: the one bridge file (see RoboDesktopScript.h).
 */

#include "RoboDesktop/RoboDesktopScript.h"

#include "RoboBase/RoboLog.h"
#include "RoboDesktopInternal.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboJS/RoboJS.h"
#include "RoboJS/RoboJSDomHtml.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboMem/RoboMem.h"

struct DesktopScript {
    JsRuntime *runtime;
    HtmlDocumentId document;
    int attached;
};

Result DesktopScriptCreate(HtmlDocumentId document,
                           const char *source,
                           size_t sourceLength,
                           DesktopScript **out,
                           DesktopDiag *diag)
{
    DesktopScript *script;
    JsRuntimeConfig config;
    Result status;
    int32_t length;

    if (!out) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                               "a script out-parameter is required");
    }
    *out = NULL;
    if (document == RoboHtmlDocumentIdNone || !source) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                               "a document and a page script are required");
    }

    script = (DesktopScript *)Malloc(sizeof(*script));
    if (!script) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboOutOfMemory,
                               "could not allocate the script bridge");
    }
    script->runtime = NULL;
    script->document = document;
    script->attached = 0;

    JsRuntimeConfigInit(&config);
    config.deterministic = 1;
    config.enableEventLoop = 1;
    config.enableTimers = 1;
    config.enableConsole = 1;
    script->runtime = JsRuntimeCreate(&config);
    if (!script->runtime) {
        RoboFree(script);
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboOutOfMemory,
                               "could not create the JavaScript runtime");
    }

    status = JsDomHtmlAttach(script->runtime, document);
    if (status != RoboOk) {
        JsRuntimeDestroy(script->runtime);
        RoboFree(script);
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, status,
                               "JsDomHtmlAttach refused the document");
    }
    script->attached = 1;

    if (sourceLength == (size_t)-1) {
        length = -1;
    } else if (sourceLength > (size_t)0x7fffffff) {
        length = -1;
    } else {
        length = (int32_t)sourceLength;
    }
    status = JsRuntimeEvaluate(script->runtime, "desktop-page", source, length, NULL);
    if (status != RoboOk) {
        LogDebug("robo_desktop: page script evaluate failed (%d)", (int)status);
        DesktopScriptDestroy(script);
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, status,
                               "the page script did not run");
    }

    /* Drain any microtasks the page script posted during evaluate (queueMicrotask at top level). */
    (void)JsLoopRunUntilIdle(script->runtime, 0);

    LogDebug("robo_desktop: page script attached to document %llu",
             (unsigned long long)document);
    *out = script;
    return RoboOk;
}

void DesktopScriptDestroy(DesktopScript *script)
{
    if (!script) {
        return;
    }
    if (script->runtime) {
        if (script->attached) {
            (void)JsDomHtmlDetach(script->runtime);
            script->attached = 0;
        }
        JsRuntimeDestroy(script->runtime);
        script->runtime = NULL;
    }
    script->document = RoboHtmlDocumentIdNone;
    RoboFree(script);
}

int DesktopScriptIsLive(const DesktopScript *script)
{
    return script != NULL && script->runtime != NULL && script->attached != 0;
}

Result DesktopScriptPump(DesktopScript *script, uint64_t deltaMs, DesktopDiag *diag)
{
    Result status;

    if (!script || !script->runtime) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                               "a live script bridge is required");
    }
    if (deltaMs > 0u) {
        (void)JsRuntimeAdvanceClock(script->runtime, deltaMs);
    }
    status = JsLoopRunUntilIdle(script->runtime, 0);
    if (status != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, status,
                               "JsLoopRunUntilIdle refused");
    }
    return RoboOk;
}

Result DesktopScriptPostClick(DesktopScript *script,
                              const LayoutTree *tree,
                              LayoutNodeId layoutNode,
                              double clientX,
                              double clientY,
                              DesktopDiag *diag)
{
    HtmlNodeHandle handle;
    JsEventRecord record;
    Result status;

    if (!script || !script->runtime) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                               "a live script bridge is required");
    }
    if (!tree || layoutNode == RoboLayoutNodeIdNone) {
        return RoboOk;
    }
    if (LayoutObjectDomNode(tree, layoutNode, &handle) != RoboOk ||
        !HtmlNodeHandleIsWellformed(&handle)) {
        return RoboOk;
    }

    status = JsEventRecordInit(&record, "click", (JsDomNodeId)handle.nodeId);
    if (status != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, status,
                               "JsEventRecordInit refused the click");
    }
    record.clientX = clientX;
    record.clientY = clientY;
    record.button = 0;
    status = JsEventQueue(script->runtime, &record);
    if (status != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, status,
                               "JsEventQueue refused the click");
    }
    LogDebug("robo_desktop: queued click on node %llu at (%.0f, %.0f)",
             (unsigned long long)handle.nodeId, clientX, clientY);
    return RoboOk;
}

HtmlDocumentId DesktopScriptDocument(const DesktopScript *script)
{
    return script ? script->document : RoboHtmlDocumentIdNone;
}
