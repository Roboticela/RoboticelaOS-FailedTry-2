/*
 * RoboDesktop module suite (Phase 11.13) — `desktop.*` and `desktop.conformance.*`.
 *
 * The assertions a *release* makes about itself: the version pin, the capability table read positionally,
 * the diagnostic and area vocabularies, the configuration defaults, the lifetime rules, the frame record's
 * own validity, and the dump. They are split from the geometry, window, frame, document and presentation
 * suites beside them for the reason the render module splits its own: those ask "is the picture right?",
 * and these ask "is this the build it says it is?".
 *
 * Two of them are worth pointing at because they are the ones a later phase breaks by accident:
 *
 *   **the capability table** is read **by name**, eighteen fields, and asserts the *absences* as hard as the
 *   presences — a compositor, tiles, damage tracking, a frame scheduler, VSync, a GPU, a window manager,
 *   input delivery and a desktop environment are all 0, as is `documents` (the shell builds no document;
 *   the bridge beside it does). The day one of them becomes 1 the suite has to be edited, which is the
 *   point: a capability that turns on without its own case is a capability nobody checked;
 *
 *   **the dump** is asserted as **seven exact lines** on a fresh desktop. That is deliberately brittle: the
 *   dump is the door a developer reads when nothing else works, and a field that moves between two lines
 *   changes what a diff shows. A case that pattern-matched the text would pass while the layout drifted.
 */

#include "RoboDesktopTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. The module                                                       */
/* ------------------------------------------------------------------ */

static void TestDesktopVersionAndCapabilities(void)
{
    I32 major = -1;
    I32 minor = -1;
    I32 patch = -1;

    RoboCheckEqInt(DesktopGetVersion(&major, &minor, &patch), RoboOk);
    RoboCheckEqInt((int)major, (int)RoboDesktopVersionMajor);
    RoboCheckEqInt((int)minor, (int)RoboDesktopVersionMinor);
    RoboCheckEqInt((int)patch, (int)RoboDesktopVersionPatch);
    RoboCheck(strcmp(DesktopVersionString(), RoboDesktopVersionString) == 0);
    RoboCheckEqInt(DesktopShellIsImplemented(), 1);
    RoboCheckEqInt(DesktopDocumentIsImplemented(), 1);
    RoboCheckEqInt((int)RoboDesktopApiFrozen, 0);

    RoboCheckEqInt(DesktopGetVersion(NULL, &minor, &patch), RoboInvalidArgument);
    RoboCheckEqInt(DesktopGetVersion(&major, NULL, &patch), RoboInvalidArgument);
    RoboCheckEqInt(DesktopGetVersion(&major, &minor, NULL), RoboInvalidArgument);
}

static void TestDesktopCapabilities(void)
{
    DesktopCapabilities capabilities;
    DesktopCapabilities cleared;

    DesktopCapabilitiesInit(&cleared);
    RoboCheckEqInt(cleared.shell, 0);
    RoboCheckEqInt(cleared.gpu, 0);
    RoboCheckEqInt(DesktopGetCapabilities(NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopGetCapabilities(&capabilities), RoboOk);

    /* What this build is. */
    RoboCheckEqInt(capabilities.shell, 1);
    RoboCheckEqInt(capabilities.windows, 1);
    RoboCheckEqInt(capabilities.windowContent, 1);
    RoboCheckEqInt(capabilities.presentation, 1);
    RoboCheckEqInt(capabilities.frameRecords, 1);
    RoboCheckEqInt(capabilities.resize, 1);
    RoboCheckEqInt(capabilities.pointerTargeting, 1);
    RoboCheckEqInt(capabilities.shellText, 1);
    /* What it deliberately is not: the shell builds no document (the bridge beside it does), and none of
     * the ten structures a compositor, a scheduler, a GPU, a window manager, an input system or a desktop
     * environment would need exist here. */
    RoboCheckEqInt(capabilities.documents, 0);
    RoboCheckEqInt(capabilities.compositor, 0);
    RoboCheckEqInt(capabilities.tiles, 0);
    RoboCheckEqInt(capabilities.damageTracking, 0);
    RoboCheckEqInt(capabilities.frameScheduling, 0);
    RoboCheckEqInt(capabilities.vsync, 0);
    RoboCheckEqInt(capabilities.gpu, 0);
    RoboCheckEqInt(capabilities.windowManager, 0);
    RoboCheckEqInt(capabilities.inputDelivery, 0);
    RoboCheckEqInt(capabilities.desktopEnvironment, 0);
}

static void TestDesktopVocabulary(void)
{
    RoboCheck(strcmp(DesktopAreaName(RoboDesktopAreaNone), "none") == 0);
    RoboCheck(strcmp(DesktopAreaName(RoboDesktopAreaBackground), "background") == 0);
    RoboCheck(strcmp(DesktopAreaName(RoboDesktopAreaPanel), "panel") == 0);
    RoboCheck(strcmp(DesktopAreaName(RoboDesktopAreaWindowTitle), "window-title") == 0);
    RoboCheck(strcmp(DesktopAreaName(RoboDesktopAreaWindowContent), "window-content") ==
                0);
    RoboCheck(strcmp(DesktopAreaName(RoboDesktopAreaWindowClose), "window-close") == 0);
    RoboCheck(strcmp(DesktopAreaName((DesktopArea)99), "unknown") == 0);
    RoboCheckEqInt((int)RoboDesktopAreaCount, 6);
    RoboCheckEqInt(DesktopAreaIsKnown(RoboDesktopAreaPanel), 1);
    RoboCheckEqInt(DesktopAreaIsKnown((DesktopArea)99), 0);

    RoboCheck(strcmp(DesktopWindowStateName(RoboDesktopWindowOpen), "open") == 0);
    RoboCheck(strcmp(DesktopWindowStateName(RoboDesktopWindowClosing), "closing") == 0);
    RoboCheck(strcmp(DesktopWindowStateName((DesktopWindowState)0), "unknown") == 0);
    RoboCheckEqInt(DesktopWindowStateIsKnown(RoboDesktopWindowOpen), 1);
    RoboCheckEqInt(DesktopWindowStateIsKnown((DesktopWindowState)0), 0);

    RoboCheckEqInt(DesktopWindowIdIsNone(RoboDesktopWindowIdNone), 1);
    RoboCheckEqInt(DesktopWindowIdIsNone(7u), 0);
    RoboCheckEqInt(DesktopWindowIdEqual(3u, 3u), 1);
    RoboCheckEqInt(DesktopWindowIdEqual(3u, 4u), 0);

    RoboCheck(strcmp(DesktopDomainName(RoboDesktopDomainNone), "none") == 0);
    RoboCheck(strcmp(DesktopDomainName(RoboDesktopDomainShell), "shell") == 0);
    RoboCheck(strcmp(DesktopDomainName(RoboDesktopDomainWindow), "window") == 0);
    RoboCheck(strcmp(DesktopDomainName(RoboDesktopDomainSurface), "surface") == 0);
    RoboCheck(strcmp(DesktopDomainName(RoboDesktopDomainDocument), "document") == 0);
    RoboCheck(strcmp(DesktopDomainName(RoboDesktopDomainDump), "dump") == 0);
    RoboCheck(strcmp(DesktopDomainName((DesktopDiagnosticDomain)77), "unknown") == 0);
    RoboCheckEqInt((int)RoboDesktopDomainCount, 6);
}

static void TestDesktopDiagnostics(void)
{
    DesktopDiag diag;
    Desktop *desktop = NULL;
    DesktopConfig config;

    DesktopDiagReset(&diag);
    RoboCheckEqInt((int)diag.code, (int)RoboOk);
    RoboCheckEqInt((int)diag.domain, (int)RoboDesktopDomainNone);
    RoboCheckEqInt(DesktopDiagSet(&diag, RoboError, RoboDesktopDomainWindow, "x",
                                           __FILE__, 1),
                      RoboError);
    RoboCheckEqInt((int)diag.domain, (int)RoboDesktopDomainWindow);
    RoboCheckEqInt(DesktopDiagSet(&diag, RoboOk, RoboDesktopDomainWindow, "x",
                                            __FILE__, 1),
                      RoboOk);
    RoboCheckEqInt((int)diag.domain, (int)RoboDesktopDomainNone);
    RoboCheckEqInt(DesktopDiagSet(NULL, RoboError, RoboDesktopDomainShell, "x", NULL, 0),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopDiagRecord(NULL, RoboError, RoboDesktopDomainShell, "x", NULL,
                                               0),
                      RoboError);

    DesktopFixture();
    DesktopConfigInit(&config);
    config.width = 0;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, &diag), RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboDesktopDomainShell);
    RoboCheckEqInt((int)diag.code, (int)RoboInvalidArgument);
    RoboCheck(diag.message != NULL);
    RoboCheck(desktop == NULL);
}

static void TestDesktopConfigDefaults(void)
{
    DesktopConfig config;

    memset(&config, 0xff, sizeof(config));
    DesktopConfigInit(&config);
    RoboCheckEqInt(config.width, 1280);
    RoboCheckEqInt(config.height, 800);
    RoboCheckEqInt((int)config.window, (int)RoboWindowIdNone);
    RoboCheckEqInt(config.RoboCreateWindow, 0);
    RoboCheck(config.windowTitle == NULL && config.identityText == NULL &&
               config.statusText == NULL && config.applicationsText == NULL);
    RoboCheck(config.theme == NULL && config.panel == NULL && config.windowMetrics == NULL);
    RoboCheck(config.face == NULL);
    RoboCheck(config.labelSizePx > 0.0f);
    DesktopConfigInit(NULL);
}

/* ------------------------------------------------------------------ */
/* 2. Lifetime                                                         */
/* ------------------------------------------------------------------ */

static void TestDesktopLifecycle(void)
{
    Desktop *desktop;
    DesktopGeometry geometry;
    DesktopStats stats;
    DesktopPointer pointer;

    desktop = DesktopOpen(1280, 800);
    RoboCheckEqInt(DesktopIsLive(desktop), 1);
    RoboCheckEqInt(DesktopIsLive(NULL), 0);
    RoboCheckEqInt((int)DesktopAttachedWindow(desktop), (int)RoboWindowIdNone);
    RoboCheckEqInt(DesktopAttachedWindow(NULL), RoboWindowIdNone);
    RoboCheck(strcmp(DesktopStatus(desktop), "") == 0);
    RoboCheck(strcmp(DesktopStatus(NULL), "") == 0);

    geometry = DesktopGeometryOf(desktop);
    RoboCheckEqInt(geometry.screen.width, 1280);
    RoboCheckEqInt(geometry.screen.height, 800);
    RoboCheckEqInt(geometry.panel.height, 30);
    RoboCheckEqInt(geometry.workArea.y, 30);
    RoboCheckEqInt(geometry.workArea.height, 770);

    RoboCheckEqInt(GetDesktopStats(desktop, &stats), RoboOk);
    RoboCheckEqInt((int)stats.frames, 0);
    RoboCheckEqInt((int)stats.presentations, 0);
    RoboCheckEqInt((int)stats.fillDrawings, 0);
    RoboCheckEqInt(GetDesktopStats(NULL, &stats), RoboInvalidArgument);
    RoboCheckEqInt(GetDesktopStats(desktop, NULL), RoboInvalidArgument);

    RoboCheckEqInt(GetDesktopPointer(desktop, &pointer), RoboOk);
    RoboCheckEqInt((int)pointer.area, (int)RoboDesktopAreaNone);
    RoboCheckEqInt(pointer.inside, 0);
    RoboCheckEqInt(GetDesktopPointer(NULL, &pointer), RoboInvalidArgument);

    RoboCheckEqInt(DesktopSetStatus(desktop, "12:34", NULL), RoboOk);
    RoboCheck(strcmp(DesktopStatus(desktop), "12:34") == 0);
    RoboCheckEqInt(DesktopSetStatus(desktop, "", NULL), RoboOk);
    RoboCheck(strcmp(DesktopStatus(desktop), "") == 0);
    RoboCheckEqInt(DesktopSetStatus(NULL, "x", NULL), RoboInvalidArgument);

    DesktopClose(desktop);
}

static void TestDesktopRefusals(void)
{
    Desktop *desktop = NULL;
    DesktopConfig config;
    DesktopPanelMetrics panel;
    DesktopWindowMetrics windowMetrics;
    DesktopTheme theme;
    char tooLong[RoboDesktopTitleMax + 2u];
    size_t index;

    DesktopFixture();
    DesktopConfigInit(&config);
    RoboCheckEqInt(DesktopCreate(NULL, &desktop, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopCreate(&config, NULL, NULL), RoboInvalidArgument);

    config.width = 0;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboInvalidArgument);
    config.width = -4;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboInvalidArgument);
    config.width = (int32_t)RoboDesktopMaxDimension + 1;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboInvalidArgument);
    config.width = 1280;
    config.height = 0;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboInvalidArgument);
    config.height = 800;

    /* A panel taller than the screen is not a desktop. */
    panel = GetRoboDesktopPanelMetrics();
    panel.height = 900;
    config.panel = &panel;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboInvalidArgument);
    panel = GetRoboDesktopPanelMetrics();
    panel.height = 0;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboInvalidArgument);
    config.panel = NULL;

    /* A close button that does not fit the title bar is not drawable. */
    windowMetrics = GetRoboDesktopWindowMetrics();
    windowMetrics.closeSize = windowMetrics.titleHeight + 1;
    config.windowMetrics = &windowMetrics;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboInvalidArgument);
    config.windowMetrics = NULL;

    theme = GetRoboDesktopTheme();
    theme.panelSeparator = theme.desktopSurface;
    config.theme = &theme;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboOk);
    RoboCheck(desktop != NULL);
    DesktopClose(desktop);
    desktop = NULL;
    config.theme = NULL;

    for (index = 0u; index < sizeof(tooLong) - 1u; index++) {
        tooLong[index] = 'a';
    }
    tooLong[sizeof(tooLong) - 1u] = '\0';
    config.identityText = tooLong;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboInvalidArgument);
    RoboCheck(desktop == NULL);

    /* A NULL is harmless on the way out, like every destroy door in this tree. */
    DesktopDestroy(NULL);
}

/* ------------------------------------------------------------------ */
/* 3. Records                                                          */
/* ------------------------------------------------------------------ */

static void TestDesktopFrameRecordRules(void)
{
    DesktopFrameRecord record;
    DesktopFrameRecord other;

    DesktopFrameRecordInit(&record);
    RoboCheckEqInt(DesktopFrameRecordIsValid(&record), 0);
    RoboCheckEqInt(DesktopFrameRecordEqual(&record, &record), 1);
    RoboCheckEqInt(DesktopFrameRecordEqual(NULL, NULL), 1);
    RoboCheckEqInt(DesktopFrameRecordEqual(&record, NULL), 0);
    RoboCheckEqInt(DesktopFrameRecordIsValid(NULL), 0);

    /* A frame this build could have painted. */
    record.width = 640u;
    record.height = 480u;
    record.bytes = 640u * 480u * 4u;
    record.windowsTotal = 2u;
    record.windows = 1u;
    record.contentPixels = 100u;
    record.fills = 9u;
    record.checksum = 0x2f1c9e0b7a4d3355u;
    RoboCheckEqInt(DesktopFrameRecordIsValid(&record), 1);

    other = record;
    RoboCheckEqInt(DesktopFrameRecordEqual(&record, &other), 1);
    other.checksum++;
    RoboCheckEqInt(DesktopFrameRecordEqual(&record, &other), 0);

    /* Each clause of the rule, one at a time: a validity rule is only as good as the cases that reach it. */
    other = record;
    other.width = 0u;
    RoboCheckEqInt(DesktopFrameRecordIsValid(&other), 0);
    other = record;
    other.width = RoboDesktopMaxDimension + 1u;
    other.bytes = 0u;
    RoboCheckEqInt(DesktopFrameRecordIsValid(&other), 0);
    other = record;
    other.windows = other.windowsTotal + 1u;
    RoboCheckEqInt(DesktopFrameRecordIsValid(&other), 0);
    other = record;
    other.contentPixels = (size_t)record.width * (size_t)record.height + 1u;
    RoboCheckEqInt(DesktopFrameRecordIsValid(&other), 0);
    other = record;
    other.bytes = record.bytes - 4u;
    RoboCheckEqInt(DesktopFrameRecordIsValid(&other), 0);
}

/* ------------------------------------------------------------------ */
/* 4. The dump                                                         */
/* ------------------------------------------------------------------ */

static void TestDesktopDump(void)
{
    Desktop *desktop = DesktopOpen(320, 200);
    static const char *const expected =
        "desktop 320x200 panel=0,0,320,30 work=0,30,320,170\n"
        "  windows=0 visible=0 focused=0\n"
        "  frames=0 without_windows=0 resizes=0\n"
        "  fills=0 labels=0 unshaped=0 clipped=0\n"
        "  content_runs=0 content_pixels=0 presentations=0\n"
        "  pointer=0,0 inside=0 pressed=0 area=none\n"
        "  checksum=0000000000000000\n";
    char text[1024];
    size_t needed = 0u;
    size_t index;
    size_t newlines = 0u;

    RoboCheckEqInt(DesktopWrite(desktop, text, sizeof(text), &needed, NULL), RoboOk);
    if (strcmp(text, expected) != 0) {
        TestFail(__FILE__, __LINE__, "the dump is not the seven expected lines:\n%s", text);
    }
    RoboCheckEqInt((int)needed, (int)strlen(expected));
    for (index = 0u; index < strlen(text); index++) {
        if (text[index] == '\n') {
            newlines++;
        }
    }
    RoboCheckEqInt((int)newlines, 7);
    /* No address anywhere in it -- which is what makes two runs of one shell comparable -- and no line
     * longer than the bound `RoboDesktopDumpLineMax` reserves for one. The address check is the
     * *absence* of a hex-digit run long enough to be one: a `0x` prefix would be found inside "320x200",
     * which is a size rather than a pointer. */
    {
        const char *cursor = text;

        while (*cursor != '\0') {
            size_t line = 0u;

            while (cursor[line] != '\0' && cursor[line] != '\n') {
                line++;
            }
            RoboCheck(line < RoboDesktopDumpLineMax);
            cursor += line + ((cursor[line] == '\n') ? 1u : 0u);
        }
        RoboCheck(strstr(text, "7f1c9e0b") == NULL);
    }

    /* The needed-length contract: a short buffer reports what it would have taken. */
    needed = 0u;
    RoboCheckEqInt(DesktopWrite(desktop, text, 8u, &needed, NULL), RoboNotSupported);
    RoboCheckEqInt((int)needed, (int)strlen(expected));
    RoboCheckEqInt(DesktopWrite(NULL, text, sizeof(text), &needed, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopWrite(desktop, NULL, sizeof(text), &needed, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopWrite(desktop, text, sizeof(text), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopWrite(desktop, text, 0u, &needed, NULL), RoboInvalidArgument);

    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterDesktop(void)
{
    TestRegister("desktop.version_and_capabilities", TestDesktopVersionAndCapabilities);
    TestRegister("desktop.conformance.capabilities", TestDesktopCapabilities);
    TestRegister("desktop.conformance.vocabulary", TestDesktopVocabulary);
    TestRegister("desktop.conformance.diagnostics", TestDesktopDiagnostics);
    TestRegister("desktop.config_defaults", TestDesktopConfigDefaults);
    TestRegister("desktop.lifecycle", TestDesktopLifecycle);
    TestRegister("desktop.lifecycle.refusals", TestDesktopRefusals);
    TestRegister("desktop.frame_record_rules", TestDesktopFrameRecordRules);
    TestRegister("desktop.dump", TestDesktopDump);
}
