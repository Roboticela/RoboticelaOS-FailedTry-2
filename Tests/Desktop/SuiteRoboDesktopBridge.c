/*
 * DesktopBridge suite (Phase 14.5 / 14.6) — allowlisted JS → C verbs.
 */

#include "RoboDesktop/RoboDesktopBridge.h"
#include "RoboDesktopTestSupport.h"

#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNavigation.h"
#include "RoboHTML/RoboHTMLSource.h"

#include <string.h>

static void TestBridgeAllowlist(void)
{
    RoboCheckEqInt(DesktopBridgeIsAllowed("system.about"), 1);
    RoboCheckEqInt(DesktopBridgeIsAllowed("app.openInputLab"), 1);
    RoboCheckEqInt(DesktopBridgeIsAllowed("lab.primary"), 1);
    RoboCheckEqInt(DesktopBridgeIsAllowed("not.a.verb"), 0);
    RoboCheckEqInt(DesktopBridgeIsAllowed(""), 0);
    RoboCheckEqInt(DesktopBridgeIsAllowed(NULL), 0);
    RoboCheckEqInt((int)DesktopBridgeVerbFromName("system.about"),
                   (int)RoboDesktopBridgeVerbSystemAbout);
    RoboCheck(strcmp(DesktopBridgeVerbName(RoboDesktopBridgeVerbLabToggle), "lab.toggle") == 0);
}

static void TestBridgeInvokeStatus(void)
{
    Desktop *desktop = NULL;
    DesktopBridgeOutcome out;
    DesktopConfig config;

    DesktopFixture();
    DesktopConfigInit(&config);
    config.width = 800;
    config.height = 600;
    config.createWindow = 0;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboOk);
    RoboCheck(desktop != NULL);

    DesktopBridgeOutcomeInit(&out);
    RoboCheckEqInt(DesktopBridgeInvoke(desktop, "system.about", &out, NULL), RoboOk);
    RoboCheckEqInt(out.applied, 1);
    RoboCheckEqInt(out.refused, 0);
    RoboCheckEqInt((int)out.verb, (int)RoboDesktopBridgeVerbSystemAbout);
    RoboCheck(strcmp(DesktopStatus(desktop), "About RoboticelaOS") == 0);

    DesktopBridgeOutcomeInit(&out);
    RoboCheckEqInt(DesktopBridgeInvoke(desktop, "evil.spawn", &out, NULL), RoboNotSupported);
    RoboCheckEqInt(out.applied, 0);
    RoboCheckEqInt(out.refused, 1);
    RoboCheck(strcmp(DesktopStatus(desktop), "About RoboticelaOS") == 0);

    DesktopDestroy(desktop);
}

static void TestBridgeConsumeDocument(void)
{
    Desktop *desktop = NULL;
    DesktopConfig config;
    HtmlDocumentId doc = RoboHtmlDocumentIdNone;
    HtmlSource source;
    HtmlNodeHandle root;
    DesktopBridgeOutcome out;

    DesktopFixture();
    DesktopConfigInit(&config);
    config.width = 640;
    config.height = 480;
    config.createWindow = 0;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboOk);

    RoboCheckEqInt(HtmlDocumentCreate(&doc, NULL), RoboOk);
    HtmlSourceInit(&source);
    RoboCheckEqInt(HtmlSourceString(&source,
                                    "<html><body>"
                                    "<div id='overviewRoot' data-last-command='lab.primary'>Hi</div>"
                                    "</body></html>",
                                    NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentLoadSource(doc, &source, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentGetElementById(doc, "overviewRoot", &root, NULL), RoboOk);
    RoboCheck(HtmlElementAttributeValue(&root, "data-last-command", NULL) != NULL);

    DesktopBridgeOutcomeInit(&out);
    RoboCheckEqInt(DesktopBridgeConsumeDocument(desktop, doc, &out, NULL), RoboOk);
    RoboCheckEqInt(out.applied, 1);
    RoboCheckEqInt((int)out.verb, (int)RoboDesktopBridgeVerbLabPrimary);
    RoboCheck(strcmp(DesktopStatus(desktop), "Primary") == 0);
    RoboCheckEqInt(HtmlDocumentGetElementById(doc, "overviewRoot", &root, NULL), RoboOk);
    RoboCheck(HtmlElementAttributeValue(&root, "data-last-command", NULL) == NULL);

    DesktopBridgeOutcomeInit(&out);
    RoboCheckEqInt(DesktopBridgeConsumeDocument(desktop, doc, &out, NULL), RoboOk);
    RoboCheckEqInt(out.applied, 0);

    (void)HtmlDocumentDestroy(doc, NULL);
    DesktopDestroy(desktop);
}

void TestRegisterDesktopBridge(void)
{
    TestRegister("desktop.bridge.allowlist", TestBridgeAllowlist);
    TestRegister("desktop.bridge.invoke", TestBridgeInvokeStatus);
    TestRegister("desktop.bridge.consume", TestBridgeConsumeDocument);
}
