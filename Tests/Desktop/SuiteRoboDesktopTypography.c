/*
 * The desktop's typography binding (Phase 12.3) — `desktop.typography.*`.
 *
 * The typography module's own suites (`Tests/Typography`) prove what a role *means* and that a role
 * resolves to a face the shipped pack installed. This suite is the other half, and it lives here
 * because it needs the shell: it proves that the *desktop* actually speaks the roles — that its chrome
 * draws from them, that a document built through the resolver gets the system's defaults instead of the
 * layout engine's initial 16px, and that the face a document was measured with is the face its contents
 * are painted through.
 *
 * The pack is opened through the typography fixture, which reads the **shipped** `System/Fonts/` with
 * the same discovery, registry, loader, selection and pack code the guest runs. The pixels, though, come
 * from the desktop's own frame surface through RoboGraphics' pixel door — so every case here is a
 * statement about a frame rather than about a resolver's return value.
 */

#include "RoboDesktopTestSupport.h"
#include "RoboTypographyTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/**
 * A desktop whose chrome draws from roles, with **no** single face at all.
 *
 * Passing no `face` is the point: if the shell were still reading the legacy field, this desktop would
 * draw nothing, and every case below would fail on `labelsUnshaped`. A shell that reaches its text
 * through the resolver alone is exactly what the phase asks for.
 */
static Desktop *TypographyDesktop(int32_t width, int32_t height, TypographyResolver *resolver)
{
    DesktopConfig config;
    Desktop *desktop = NULL;

    DesktopFixture();
    DesktopConfigInit(&config);
    config.width = width;
    config.height = height;
    config.typography = resolver;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboOk);
    RoboCheck(desktop != NULL);
    return desktop;
}

/** A window's contents whose document was built with @p resolver (or with @p face when it is NULL). */
static DesktopDocument *TypographyWindowContent(Desktop *desktop,
                                                DesktopWindowId id,
                                                const char *html,
                                                const char *css,
                                                TypographyResolver *resolver,
                                                FontFace *face)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    GfxRect client;

    RoboCheckEqInt(DesktopWindowClientBounds(desktop, id, &client), RoboOk);
    DesktopDocumentConfigInit(&config);
    config.html = html;
    config.css = css;
    config.width = client.width;
    config.height = client.height;
    config.face = face;
    config.typography = resolver;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheck(document != NULL);
    /* The face the *document* resolved, not the caller's: the window must paint through the same object
     * the layout pass measured with (`DesktopDocumentFace`). */
    RoboCheckEqInt(DesktopWindowSetContent(desktop, id, DesktopDocumentDisplayList(document),
                                           DesktopDocumentFace(document), NULL),
                   RoboOk);
    return document;
}

/** Pixels in a half-open rectangle that differ from @p background in any channel. */
static int32_t TypographyInk(const Desktop *desktop,
                             GfxRect rect,
                             GfxColorRgba8 background)
{
    const GfxSurface *surface = DesktopFrameSurface(desktop);
    int32_t count = 0;
    int32_t y;

    RoboCheck(surface != NULL);
    for (y = rect.y; y < rect.y + rect.height; y++) {
        int32_t x;

        for (x = rect.x; x < rect.x + rect.width; x++) {
            GfxColorRgba8 color;

            if (x < 0 || y < 0 || x >= (int32_t)DesktopScreenSize(desktop).width ||
                y >= (int32_t)DesktopScreenSize(desktop).height) {
                continue;
            }
            memset(&color, 0, sizeof(color));
            if (GfxSurfaceGetPixel(surface, (uint32_t)x, (uint32_t)y, &color) != RoboOk) {
                continue;
            }
            if (color.r != background.r || color.g != background.g || color.b != background.b) {
                count++;
            }
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* The chrome draws from roles                                         */
/* ------------------------------------------------------------------ */

static void TestChromeRoles(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    Desktop *desktop;
    DesktopWindowId window;
    DesktopFrameRecord first;
    DesktopFrameRecord second;
    GfxRect panel;

    if (!fixture.resolver) {
        return;
    }

    desktop = TypographyDesktop(640, 480, fixture.resolver);
    window = DesktopAddWindow(desktop, "RoboticelaOS", -1, -1, 320, 200);
    RoboCheck(window != RoboDesktopWindowIdNone);
    RoboCheckEqInt(DesktopSetStatus(desktop, "12:00", NULL), RoboOk);
    panel = DesktopGeometryOf(desktop).panel;

    /*
     * Four labels: the identity, the applications label, the status the caller set, and the window's
     * title — and **not one unshaped**, with no `face` configured at all. That pair of numbers is the
     * phase's claim as a fact rather than a resolution: the shell wrote glyphs it could only have got
     * from a role.
     */
    RoboCheckEqInt(DesktopRender(desktop, &first, NULL), RoboOk);
    RoboCheckEqInt((int)first.labels, 4);
    RoboCheckEqInt((int)first.labelsUnshaped, 0);
    RoboCheck(first.windows == 1u);

    /* And the ink is on the panel, not merely counted. */
    RoboCheck(TypographyInk(desktop, panel, DesktopPixel(desktop, 2, (uint32_t)panel.y + 2u)) > 0);

    /* A role binding must not cost determinism: two frames of one state are byte-identical. */
    RoboCheckEqInt(DesktopRender(desktop, &second, NULL), RoboOk);
    RoboCheck(DesktopFrameRecordEqual(&first, &second));
    RoboCheck(DesktopFrameChecksum(desktop) == second.checksum);

    DesktopClose(desktop);
    TypographyFixtureClose(&fixture);
}

/**
 * The binding is not a no-op: roles draw a *different* picture from the legacy single face.
 *
 * The two desktops are otherwise identical — one size, one window, one title, one status — and the
 * legacy one uses the same physical family at the shell's 11px default. If the frame checksums matched,
 * the role path would be choosing nothing.
 */
static void TestRolesChangeThePicture(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    Desktop *roles;
    Desktop *legacy;
    DesktopWindowId rolesWindow;
    DesktopWindowId legacyWindow;
    DesktopFrameRecord rolesFrame;
    DesktopFrameRecord legacyFrame;

    if (!fixture.resolver) {
        return;
    }
    /* The same family as the roles resolve to, so the *only* variable is the typography binding. */
    {
        RoboTypographyStyle body;
        FontFace *face = NULL;

        RoboCheckEqInt(RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDocumentBody, &body,
                                             NULL),
                       RoboOk);
        face = body.face;
        RoboCheck(face != NULL);
        RoboCheck(face == body.face);

        roles = TypographyDesktop(480, 320, fixture.resolver);
        legacy = DesktopOpenWithFace(480, 320, face);
    }

    rolesWindow = DesktopAddWindow(roles, "Window", -1, -1, 240, 160);
    RoboCheckEqInt(DesktopSetStatus(roles, "ok", NULL), RoboOk);
    legacyWindow = DesktopAddWindow(legacy, "Window", -1, -1, 240, 160);
    RoboCheckEqInt(DesktopSetStatus(legacy, "ok", NULL), RoboOk);
    RoboCheck(rolesWindow != RoboDesktopWindowIdNone &&
              legacyWindow != RoboDesktopWindowIdNone);

    RoboCheckEqInt(DesktopRender(roles, &rolesFrame, NULL), RoboOk);
    RoboCheckEqInt(DesktopRender(legacy, &legacyFrame, NULL), RoboOk);

    /* Both drew their text... */
    RoboCheckEqInt((int)rolesFrame.labels, 4);
    RoboCheckEqInt((int)legacyFrame.labels, 4);
    RoboCheckEqInt((int)rolesFrame.labelsUnshaped, 0);
    RoboCheckEqInt((int)legacyFrame.labelsUnshaped, 0);
    /* ...and they are not the same picture, because one drew 13px semibold titles and the other 11px
     * regular ones. The label *counts* agree, which is what makes the checksums comparable. */
    RoboCheck(rolesFrame.checksum != legacyFrame.checksum);

    DesktopClose(roles);
    DesktopClose(legacy);
    TypographyFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Document defaults                                                   */
/* ------------------------------------------------------------------ */

/**
 * The generated defaults reach the pixels: an `h1` with no author stylesheet is display-sized.
 *
 * The two documents are built with the **same face** and the same markup; the only difference is whether
 * the resolver supplied the runtime-origin default sheet. So this case measures the sheet and nothing
 * else, and the assertion is the phase's §19 in one number: the system's `h1` puts down visibly more ink
 * than the layout engine's initial 16px body text.
 */
static void TestDocumentDefaults(void)
{
    static const char markup[] = "<html><body><h1>Roboticela</h1></body></html>";
    /*
     * A minimal author sheet, and it is **shared by both documents**: `display: block` is not this
     * build's default for an unknown element (`RoboLayoutStyle.h` says an element nothing declares
     * `display` for is inline), so a case about *type* must state the boxes and leave the type setting
     * as the only variable.
     */
    static const char sheet[] =
        "html, body, h1 { display: block; margin: 0; padding: 0; }";
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    Desktop *system;
    Desktop *initial;
    DesktopWindowId window;
    DesktopDocument *systemDocument;
    DesktopDocument *initialDocument;
    GfxRect systemClient;
    GfxRect initialClient;
    FontFace *bodyFace = NULL;
    int32_t systemInk;
    int32_t initialInk;

    if (!fixture.resolver) {
        return;
    }
    {
        RoboTypographyStyle body;

        RoboCheckEqInt(RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDocumentBody, &body,
                                             NULL),
                       RoboOk);
        bodyFace = body.face;
        RoboCheck(bodyFace != NULL);
    }

    system = TypographyDesktop(480, 320, fixture.resolver);
    initial = TypographyDesktop(480, 320, fixture.resolver);
    window = DesktopAddWindow(system, "Heading", -1, -1, 400, 240);
    systemDocument = TypographyWindowContent(system, window, markup, sheet, fixture.resolver, NULL);
    window = DesktopAddWindow(initial, "Heading", -1, -1, 400, 240);
    /* No typography: the same face and the same boxes, the layout engine's own initial type setting. */
    initialDocument = TypographyWindowContent(initial, window, markup, sheet, NULL, bodyFace);

    RoboCheckEqInt(DesktopWindowClientBounds(system, window, &systemClient), RoboOk);
    RoboCheckEqInt(DesktopWindowClientBounds(initial, window, &initialClient), RoboOk);
    RoboCheck(DesktopRender(system, NULL, NULL) == RoboOk);
    RoboCheck(DesktopRender(initial, NULL, NULL) == RoboOk);

    /* The client area's background is the window surface: nothing declares a page colour here, which is
     * deliberate -- this case is about type, not about the page. */
    {
        GfxColorRgba8 paper = DesktopPixel(system, (uint32_t)systemClient.x, (uint32_t)systemClient.y);

        systemInk = TypographyInk(system, systemClient, paper);
        initialInk = TypographyInk(initial, initialClient, paper);
    }
    RoboCheck(initialInk > 0);
    RoboCheckMsg(systemInk > initialInk * 3,
                 "the system's h1 did not draw visibly larger than the layout engine's initial text");

    /* The same face was chosen for measurement and paint, by construction. */
    RoboCheck(DesktopDocumentFace(systemDocument) == bodyFace);
    RoboCheck(DesktopDocumentFace(initialDocument) == bodyFace);

    DesktopDocumentDestroy(initialDocument);
    DesktopDocumentDestroy(systemDocument);
    DesktopClose(initial);
    DesktopClose(system);
    TypographyFixtureClose(&fixture);
}

/** The document's face is the resolver's when it names one, and the caller's when it does not. */
static void TestDocumentFacePrecedence(void)
{
    static const char markup[] = "<html><body><p>Hello</p></body></html>";
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    DesktopDocumentConfig config;
    DesktopDocument *viaResolver = NULL;
    DesktopDocument *viaCaller = NULL;
    RoboTypographyStyle body;
    FontFace *resolved = NULL;

    if (!fixture.resolver) {
        return;
    }
    RoboCheckEqInt(RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDocumentBody, &body, NULL),
                   RoboOk);
    resolved = body.face;
    RoboCheck(resolved != NULL);

    DesktopDocumentConfigInit(&config);
    config.html = markup;
    config.width = 320;
    config.height = 200;
    config.typography = fixture.resolver;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &viaResolver, NULL), RoboOk);
    /* The resolver is the authority: no `face` was given, and the document still has one. */
    RoboCheck(DesktopDocumentFace(viaResolver) == resolved);

    /* With no resolver, `face` is what the document uses -- and NULL stays NULL, which is the supported
     * textless build rather than an error. */
    DesktopDocumentConfigInit(&config);
    config.html = markup;
    config.width = 320;
    config.height = 200;
    config.face = resolved;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &viaCaller, NULL), RoboOk);
    RoboCheck(DesktopDocumentFace(viaCaller) == resolved);
    {
        DesktopDocument *textless = NULL;

        DesktopDocumentConfigInit(&config);
        config.html = markup;
        config.width = 320;
        config.height = 200;
        RoboCheckEqInt(DesktopDocumentBuild(&config, &textless, NULL), RoboOk);
        RoboCheck(DesktopDocumentFace(textless) == NULL);
        /* A textless document is still a document: it owns its list and reports what it painted. */
        RoboCheck(DesktopDocumentDisplayList(textless) != NULL);
        DesktopDocumentDestroy(textless);
    }

    DesktopDocumentDestroy(viaCaller);
    DesktopDocumentDestroy(viaResolver);
    TypographyFixtureClose(&fixture);
}

/**
 * A role whose family is not installed degrades to the caller's face rather than to no text.
 *
 * The pack here declares only the UI Text role, so the panel identity (`DesktopTitle`, a *display*
 * role) has no family at all. The shell must still draw it — through the fallback face — because the
 * alternative is a blank panel reporting nothing about why it is blank.
 */
static void TestMissingFamilyFallsBack(void)
{
    static const char manifest[] =
        "system-font-pack 1\n"
        "root /System/Fonts\n"
        "family Inter\n"
        "  file UI/Inter.ttf\n"
        "  role text weight 400\n"
        "  weight 400\n"
        "  style normal\n"
        "end\n";
    static const char markup[] = "<html><body><h1>Still here</h1></body></html>";
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    TypographyVirtualFixture narrow;
    DesktopConfig config;
    Desktop *desktop = NULL;
    DesktopWindowId window;
    DesktopFrameRecord record;
    FontFace *fallbackFace;

    if (!fixture.resolver) {
        return;
    }
    {
        RoboTypographyStyle body;

        RoboCheckEqInt(RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDocumentBody, &body,
                                             NULL),
                       RoboOk);
        fallbackFace = body.face;
    }
    narrow = TypographyVirtualPack(manifest);
    if (!narrow.resolver) {
        TypographyFixtureClose(&fixture);
        return;
    }

    /* Sanity: the narrow pack really does have no display family. */
    {
        RoboTypographyStyle display;

        RoboCheckEqInt(RoboTypographyResolve(narrow.resolver, RoboTypographyRoleDesktopTitle, &display,
                                             NULL),
                       RoboOk);
        RoboCheck(display.face == NULL);
    }

    DesktopFixture();
    DesktopConfigInit(&config);
    config.width = 400;
    config.height = 300;
    config.typography = narrow.resolver;
    config.face = fallbackFace;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboOk);
    window = DesktopAddWindow(desktop, "Title", -1, -1, 200, 120);
    RoboCheck(window != RoboDesktopWindowIdNone);
    {
        DesktopDocument *document = TypographyWindowContent(desktop, window, markup, NULL,
                                                            narrow.resolver, fallbackFace);

        RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
        /* Every label still shaped: the roles with a family used it, and the one without used the
         * caller's face. */
        RoboCheckEqInt((int)record.labelsUnshaped, 0);
        RoboCheck((int)record.labels >= 3);
        DesktopDocumentDestroy(document);
    }

    DesktopClose(desktop);
    TypographyVirtualClose(&narrow);
    TypographyFixtureClose(&fixture);
}

void TestRegisterDesktopTypography(void)
{
    TestRegister("desktop.typography.chrome_roles", TestChromeRoles);
    TestRegister("desktop.typography.roles_change_picture", TestRolesChangeThePicture);
    TestRegister("desktop.typography.document_defaults", TestDocumentDefaults);
    TestRegister("desktop.typography.document_face", TestDocumentFacePrecedence);
    TestRegister("desktop.typography.missing_family_fallback", TestMissingFamilyFallsBack);
}
