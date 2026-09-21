/*
 * SuiteRoboDisplay — Goal 7.3 display abstraction tests.
 *
 * Mock backend is test-only; not a production platform implementation.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

#define MOCK_MAX 8

typedef struct mockDisplayBackend {
    DisplayInfo displays[MOCK_MAX];
    size_t count;
    int failInit;
    int failEnumerate;
    int provideModes;
    DisplayMode modes[4];
    size_t modeCount;
    DisplayId modesForId;
} MockDisplayBackend;

static MockDisplayBackend gMock;
static DisplayBackendCaps gMockCaps;
static DisplayBackendVtbl gMockVtbl;

static void FillDisplay(DisplayInfo *d,
                         DisplayId id,
                         int32_t x,
                         int32_t y,
                         int32_t w,
                         int32_t h,
                         int32_t workInset,
                         int32_t resW,
                         int32_t resH,
                         float hz,
                         float scale,
                         float dpi,
                         DisplayOrientation ori,
                         int primary)
{
    DisplayInfoInit(d);
    d->id = id;
    d->bounds = MakeGfxRect(x, y, w, h);
    d->workArea = MakeGfxRect(x + workInset, y + workInset,
                                 w - 2 * workInset, h - 2 * workInset);
    d->resolutionWidthPx = resW;
    d->resolutionHeightPx = resH;
    d->refreshHz = hz;
    d->scale = scale;
    d->dpiX = dpi;
    d->dpiY = dpi;
    d->orientation = ori;
    d->isPrimary = primary;
    d->connection = RoboDisplayConnectionConnected;
}

static Result MockInit(void *ud)
{
    MockDisplayBackend *m = (MockDisplayBackend *)ud;
    if (!m) {
        return RoboInvalidArgument;
    }
    if (m->failInit) {
        return RoboIoError;
    }
    return RoboOk;
}

static void MockShutdown(void *ud)
{
    (void)ud;
}

static Result MockEnumerate(void *ud,
                                    DisplayInfo *out,
                                    size_t capacity,
                                    size_t *outCount)
{
    MockDisplayBackend *m = (MockDisplayBackend *)ud;
    size_t n;
    size_t i;

    if (!m || !outCount) {
        return RoboInvalidArgument;
    }
    if (m->failEnumerate) {
        return RoboIoError;
    }
    n = m->count;
    *outCount = n;
    if (!out || capacity == 0u) {
        return RoboOk;
    }
    if (n > capacity) {
        n = capacity;
    }
    for (i = 0u; i < n; i++) {
        out[i] = m->displays[i];
    }
    *outCount = n;
    return RoboOk;
}

static Result MockPrimary(void *ud, DisplayInfo *out)
{
    MockDisplayBackend *m = (MockDisplayBackend *)ud;
    size_t i;

    if (!m || !out) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < m->count; i++) {
        if (m->displays[i].isPrimary) {
            *out = m->displays[i];
            return RoboOk;
        }
    }
    return RoboNotFound;
}

static Result MockGetInfo(void *ud,
                                   DisplayId id,
                                   DisplayInfo *out)
{
    MockDisplayBackend *m = (MockDisplayBackend *)ud;
    size_t i;

    if (!m || !out) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < m->count; i++) {
        if (m->displays[i].id == id) {
            *out = m->displays[i];
            return RoboOk;
        }
    }
    return RoboNotFound;
}

static Result MockGetModes(void *ud,
                                    DisplayId id,
                                    DisplayMode *out,
                                    size_t capacity,
                                    size_t *outCount)
{
    MockDisplayBackend *m = (MockDisplayBackend *)ud;
    size_t n;
    size_t i;

    if (!m || !outCount) {
        return RoboInvalidArgument;
    }
    if (!m->provideModes || id != m->modesForId) {
        return RoboNotFound;
    }
    n = m->modeCount;
    *outCount = n;
    if (!out || capacity == 0u) {
        return RoboOk;
    }
    if (n > capacity) {
        n = capacity;
    }
    for (i = 0u; i < n; i++) {
        out[i] = m->modes[i];
    }
    *outCount = n;
    return RoboOk;
}

static void MockPrepareVtbl(int withModes)
{
    memset(&gMockCaps, 0, sizeof(gMockCaps));
    gMockCaps.enumerate = 1;
    gMockCaps.primary = 1;
    gMockCaps.GetInfo = 1;
    gMockCaps.workArea = 1;
    gMockCaps.refreshRate = 1;
    gMockCaps.scaleDpi = 1;
    gMockCaps.orientation = 1;
    gMockCaps.modes = withModes ? 1 : 0;

    memset(&gMockVtbl, 0, sizeof(gMockVtbl));
    gMockVtbl.name = "mock_display";
    gMockVtbl.capabilities = &gMockCaps;
    gMockVtbl.initialize = MockInit;
    gMockVtbl.shutdown = MockShutdown;
    gMockVtbl.enumerate = MockEnumerate;
    gMockVtbl.GetPrimary = MockPrimary;
    gMockVtbl.GetInfo = MockGetInfo;
    gMockVtbl.GetModes = withModes ? MockGetModes : NULL;
}

static void DisplayFixtureReset(void)
{
    WindowShutdown();
    DisplayShutdown();
    memset(&gMock, 0, sizeof(gMock));
}

static void DisplayFixtureMock(void)
{
    DisplayFixtureReset();
    RoboCheckEqInt(DisplayInit(), RoboOk);
    MockPrepareVtbl(0);
    RoboCheckEqInt(DisplayBackendRegister(&gMockVtbl, &gMock),
                      RoboOk);
}

static void TestDisplayIdentity(void)
{
    DisplayInfo info;

    DisplayFixtureMock();
    FillDisplay(&gMock.displays[0], (DisplayId)42u, 0, 0, 100, 100, 0,
                 100, 100, 60.0f, 1.0f, 96.0f,
                 RoboDisplayOrientationLandscape, 1);
    gMock.count = 1u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);

    RoboCheck(DisplayIdIsNone(RoboDisplayIdNone));
    RoboCheck(!DisplayIdIsNone((DisplayId)42u));
    RoboCheck(DisplayIdEqual((DisplayId)42u,
                                     (DisplayId)42u));
    RoboCheck(!DisplayIdEqual(RoboDisplayIdNone, RoboDisplayIdNone));
    RoboCheck(!DisplayIdEqual((DisplayId)1u,
                                      (DisplayId)2u));
    RoboCheck(DisplayIdIsKnown((DisplayId)42u));
    RoboCheck(!DisplayIdIsKnown((DisplayId)99u));
    RoboCheck(!DisplayIdIsKnown(RoboDisplayIdNone));
    RoboCheckEqInt(DisplayGet((DisplayId)42u, &info), RoboOk);
    RoboCheckEqInt(DisplayGet((DisplayId)99u, &info),
                      RoboNotFound);
    RoboCheckEqInt(DisplayGet(RoboDisplayIdNone, &info),
                      RoboNotFound);
    DisplayFixtureReset();
}

static void TestDisplayGeometry(void)
{
    DisplayInfo a;
    DisplayInfo b;

    DisplayFixtureMock();
    FillDisplay(&gMock.displays[0], (DisplayId)10u, 0, 0, 1920, 1080,
                 40, 1920, 1080, 60.0f, 1.0f, 96.0f,
                 RoboDisplayOrientationLandscape, 1);
    FillDisplay(&gMock.displays[1], (DisplayId)20u, -1280, 100, 1280,
                 800, 0, 2560, 1600, 120.0f, 2.0f, 192.0f,
                 RoboDisplayOrientationLandscape, 0);
    gMock.count = 2u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt((int)DisplayCount(), 2);
    RoboCheckEqInt(DisplayAt(0u, &a), RoboOk);
    RoboCheckEqInt(DisplayAt(1u, &b), RoboOk);
    RoboCheckEqInt(a.bounds.x, 0);
    RoboCheckEqInt(a.workArea.y, 40);
    RoboCheckEqInt(b.bounds.x, -1280);
    RoboCheckEqInt(b.bounds.y, 100);
    RoboCheckEqInt(b.resolutionWidthPx, 2560);
    RoboCheckEqInt(b.resolutionHeightPx, 1600);
    DisplayFixtureReset();
}

static void TestDisplayRefreshRate(void)
{
    float nanBits;
    uint32_t u = 0x7fc00000u;

    RoboCheckEqInt(DisplayRefreshHzValidate(60.0f), RoboOk);
    RoboCheckEqInt(DisplayRefreshHzValidate(59.94f), RoboOk);
    RoboCheckEqInt(DisplayRefreshHzValidate(144.0f), RoboOk);
    RoboCheckEqInt(DisplayRefreshHzValidate(0.0f), RoboOk);
    RoboCheckEqInt(DisplayRefreshHzValidate(-1.0f),
                      RoboInvalidArgument);
    memcpy(&nanBits, &u, sizeof(nanBits));
    RoboCheckEqInt(DisplayRefreshHzValidate(nanBits),
                      RoboInvalidArgument);

    DisplayFixtureMock();
    FillDisplay(&gMock.displays[0], (DisplayId)1u, 0, 0, 100, 100, 0,
                 100, 100, 59.94f, 1.0f, 96.0f,
                 RoboDisplayOrientationLandscape, 1);
    gMock.count = 1u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    {
        DisplayInfo info;
        RoboCheckEqInt(DisplayPrimary(&info), RoboOk);
        RoboCheck(info.refreshHz > 59.9f && info.refreshHz < 60.0f);
    }
    DisplayFixtureReset();
}

static void TestDisplayScaleDpi(void)
{
    int32_t phys = 0;
    int32_t logical = 0;

    RoboCheckEqInt(DisplayLogicalToPhysical(100, 2.0f, &phys),
                      RoboOk);
    RoboCheckEqInt(phys, 200);
    RoboCheckEqInt(DisplayPhysicalToLogical(200, 2.0f, &logical),
                      RoboOk);
    RoboCheckEqInt(logical, 100);
    RoboCheckEqInt(DisplayLogicalToPhysical(100, 0.0f, &phys),
                      RoboInvalidArgument);

    DisplayFixtureMock();
    FillDisplay(&gMock.displays[0], (DisplayId)7u, 0, 0, 1280, 800, 0,
                 2560, 1600, 60.0f, 2.0f, 192.0f,
                 RoboDisplayOrientationLandscape, 1);
    gMock.count = 1u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    {
        DisplayInfo info;
        RoboCheckEqInt(DisplayPrimary(&info), RoboOk);
        RoboCheck(info.scale > 1.9f && info.scale < 2.1f);
        RoboCheck(info.dpiX > 191.0f && info.dpiX < 193.0f);
        RoboCheckEqInt(info.bounds.width, 1280);
        RoboCheckEqInt(info.resolutionWidthPx, 2560);
    }
    DisplayFixtureReset();
}

static void TestDisplayOrientation(void)
{
    RoboCheck(DisplayOrientationIsKnown(
        RoboDisplayOrientationLandscape));
    RoboCheck(DisplayOrientationIsKnown(
        RoboDisplayOrientationPortrait));
    RoboCheck(DisplayOrientationIsKnown(
        RoboDisplayOrientationLandscapeFlipped));
    RoboCheck(DisplayOrientationIsKnown(
        RoboDisplayOrientationPortraitFlipped));
    RoboCheck(!DisplayOrientationIsKnown(
        RoboDisplayOrientationUnknown));
    RoboCheck(!DisplayOrientationIsKnown(
        (DisplayOrientation)99));
    RoboCheck(strcmp(DisplayOrientationName(
                          RoboDisplayOrientationPortrait),
                      "portrait") == 0);
    RoboCheck(strcmp(DisplayConnectionName(
                          RoboDisplayConnectionConnected),
                      "connected") == 0);
}

static void TestDisplayPrimary(void)
{
    DisplayInfo info;

    DisplayFixtureMock();
    gMock.count = 0u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt((int)DisplayCount(), 0);
    RoboCheckEqInt(DisplayPrimary(&info), RoboNotFound);

    FillDisplay(&gMock.displays[0], (DisplayId)5u, 0, 0, 800, 600, 0,
                 800, 600, 60.0f, 1.0f, 96.0f,
                 RoboDisplayOrientationLandscape, 0);
    FillDisplay(&gMock.displays[1], (DisplayId)9u, 800, 0, 800, 600, 0,
                 800, 600, 75.0f, 1.0f, 96.0f,
                 RoboDisplayOrientationLandscape, 1);
    gMock.count = 2u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt(DisplayPrimary(&info), RoboOk);
    RoboCheckEqInt((int)info.id, 9);
    RoboCheck(info.id != (DisplayId)0u);
    DisplayFixtureReset();
}

static void TestDisplayEnumeration(void)
{
    DisplayInfo info;

    DisplayFixtureMock();
    gMock.count = 0u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt((int)DisplayCount(), 0);
    RoboCheckEqInt(DisplayAt(0u, &info), RoboNotFound);

    FillDisplay(&gMock.displays[0], (DisplayId)1u, 0, 0, 10, 10, 0, 10,
                 10, 60.0f, 1.0f, 96.0f, RoboDisplayOrientationLandscape, 1);
    gMock.count = 1u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt((int)DisplayCount(), 1);

    FillDisplay(&gMock.displays[1], (DisplayId)2u, 10, 0, 10, 10, 0,
                 10, 10, 60.0f, 1.0f, 96.0f,
                 RoboDisplayOrientationLandscape, 0);
    gMock.count = 2u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt((int)DisplayCount(), 2);

    gMock.failEnumerate = 1;
    RoboCheckEqInt(DisplayRefresh(), RoboIoError);
    RoboCheckEqInt((int)DisplayCount(), 0);
    DisplayFixtureReset();
}

static void TestDisplayBackendLifecycle(void)
{
    DisplayBackendCaps caps;

    DisplayFixtureReset();
    RoboCheckEqInt(DisplayBackendUseNull(), RoboInitError);
    RoboCheckEqInt(DisplayInit(), RoboOk);
    RoboCheckEqInt(DisplayInit(), RoboOk);
    RoboCheckEqInt(DisplayBackendUseNull(), RoboOk);
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt((int)DisplayCount(), 0);
    RoboCheck(DisplayBackendIsRegistered());
    RoboCheck(strcmp(DisplayBackendName(), "null") == 0);

    RoboCheckEqInt(DisplayBackendUsePlaceholder(), RoboOk);
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt((int)DisplayCount(), 1);
    RoboCheck(strcmp(DisplayBackendName(), "placeholder") == 0);

    DisplayBackendClear();
    RoboCheck(!DisplayBackendIsRegistered());
    RoboCheckEqInt(DisplayBackendCapabilities(&caps),
                      RoboNotSupported);
    RoboCheckEqInt(DisplayRefresh(), RoboNotSupported);

    MockPrepareVtbl(0);
    gMock.failInit = 1;
    RoboCheckEqInt(DisplayBackendRegister(&gMockVtbl, &gMock),
                      RoboIoError);
    RoboCheck(!DisplayBackendIsRegistered());

    DisplayShutdown();
    DisplayShutdown();
    RoboCheck(!DisplayIsInitialized());
    DisplayFixtureReset();
}

static void TestDisplayCapabilities(void)
{
    DisplayBackendCaps caps;
    DisplayMode modes[4];
    size_t n = 0u;

    DisplayFixtureMock();
    FillDisplay(&gMock.displays[0], (DisplayId)3u, 0, 0, 100, 100, 0,
                 100, 100, 60.0f, 1.0f, 96.0f,
                 RoboDisplayOrientationLandscape, 1);
    gMock.count = 1u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt(DisplayBackendCapabilities(&caps), RoboOk);
    RoboCheck(caps.enumerate);
    RoboCheck(caps.primary);
    RoboCheck(caps.GetInfo);
    RoboCheck(!caps.modes);
    RoboCheckEqInt(DisplayGetModes((DisplayId)3u, modes, 4u,
                                             &n),
                      RoboNotSupported);

    MockPrepareVtbl(1);
    gMock.provideModes = 1;
    gMock.modesForId = (DisplayId)3u;
    gMock.modeCount = 2u;
    gMock.modes[0].widthPx = 100;
    gMock.modes[0].heightPx = 100;
    gMock.modes[0].refreshHz = 60.0f;
    gMock.modes[1].widthPx = 100;
    gMock.modes[1].heightPx = 100;
    gMock.modes[1].refreshHz = 120.0f;
    RoboCheckEqInt(DisplayBackendRegister(&gMockVtbl, &gMock),
                      RoboOk);
    RoboCheckEqInt(DisplayGetModes((DisplayId)3u, modes, 4u,
                                             &n),
                      RoboOk);
    RoboCheckEqInt((int)n, 2);
    DisplayFixtureReset();
}

static void TestDisplayLookup(void)
{
    DisplayInfo info;

    DisplayFixtureMock();
    FillDisplay(&gMock.displays[0], (DisplayId)11u, 0, 0, 50, 50, 0,
                 50, 50, 60.0f, 1.0f, 96.0f,
                 RoboDisplayOrientationLandscape, 1);
    gMock.count = 1u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt(DisplayGet((DisplayId)11u, &info), RoboOk);
    RoboCheckEqInt(DisplayGet((DisplayId)12u, &info),
                      RoboNotFound);
    RoboCheckEqInt(DisplayGet(RoboDisplayIdNone, &info),
                      RoboNotFound);
    DisplayFixtureReset();
}

static void TestDisplayWindowIntegration(void)
{
    WindowConfig cfg;
    WindowId wid = RoboWindowIdNone;
    DisplayInfo info;
    WindowState st;

    DisplayFixtureReset();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendUseNull(), RoboOk);
    RoboCheckEqInt((int)DisplayCount(), 1);
    RoboCheckEqInt(DisplayPrimary(&info), RoboOk);

    WindowConfigInit(&cfg);
    cfg.width = 320;
    cfg.height = 240;
    cfg.title = "display-assoc";
    RoboCheckEqInt(WindowCreate(&cfg, &wid), RoboOk);
    RoboCheckEqInt(WindowGetAssociatedDisplay(wid, &info), RoboOk);
    RoboCheck(info.id != RoboDisplayIdNone);
    RoboCheckEqInt(WindowGetState(wid, &st), RoboOk);
    RoboCheck(DisplayIdEqual(st.displayId, info.id));

    RoboCheckEqInt(
        WindowSetAssociatedDisplay(wid, (DisplayId)999u),
        RoboNotFound);
    RoboCheckEqInt(
        WindowSetAssociatedDisplay(wid, RoboDisplayIdNone), RoboOk);
    RoboCheckEqInt(WindowGetAssociatedDisplay(wid, &info),
                      RoboNotFound);
    RoboCheckEqInt(WindowSetAssociatedDisplay(wid, st.displayId),
                      RoboOk);
    RoboCheckEqInt(WindowGetAssociatedDisplay(wid, &info), RoboOk);

    DisplayFixtureReset();
}

static void TestDisplayVersion(void)
{
    RoboCheck(strcmp(DisplayVersionString(), "0.4.0") == 0);
    RoboCheckEqInt(RoboDisplayApiFrozen, 0);
}

void TestRegisterDisplay(void)
{
    TestRegister("display.version", TestDisplayVersion);
    TestRegister("display.identity", TestDisplayIdentity);
    TestRegister("display.geometry", TestDisplayGeometry);
    TestRegister("display.refresh_rate", TestDisplayRefreshRate);
    TestRegister("display.scale_dpi", TestDisplayScaleDpi);
    TestRegister("display.orientation", TestDisplayOrientation);
    TestRegister("display.primary", TestDisplayPrimary);
    TestRegister("display.enumeration", TestDisplayEnumeration);
    TestRegister("display.backend_lifecycle",
                       TestDisplayBackendLifecycle);
    TestRegister("display.capabilities", TestDisplayCapabilities);
    TestRegister("display.lookup", TestDisplayLookup);
    TestRegister("display.window_integration",
                       TestDisplayWindowIntegration);
}
