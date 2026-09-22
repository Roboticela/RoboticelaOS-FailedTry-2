/*
 * RoboFont system typeface pack suite (Phase 12.2) — `font.systempack.*`.
 *
 * The pack is the layer that turns `/System/Fonts/` and its manifest into
 * *roles* a shell can ask for. Its two halves are tested the way they are built:
 *
 *   the parser   is exercised on embedded text, so a malformed manifest, a
 *                duplicate family or a missing `file` is a case rather than a
 *                filesystem accident;
 *
 *   the pack     is opened against the **shipped** pack
 *                (`System/Fonts/`, packed into the image at `/System/Fonts/`).
 *                That is deliberate: the phase's claim is that the OS ships a
 *                discoverable, licensed, multi-weight system face, and a suite
 *                that tested a synthetic stand-in could pass while the real pack
 *                was broken. It reads through a minimal host IO adapter, so the
 *                *same* discovery/registry/loader/selection code the guest runs
 *                is what is exercised.
 *
 * Missing-file and missing-manifest cases use paths that cannot exist, so they
 * are deterministic and do not touch the network or a host font directory.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(RoboSystemFontRootFromBuild)
#include "robo_system_font_root.h"
#endif
#ifndef RoboSystemFontRoot
#define RoboSystemFontRoot "System/Fonts"
#endif

#define RF_TAG_WGHT RoboFontTag('w', 'g', 'h', 't')
#define RF_TAG_OPSZ RoboFontTag('o', 'p', 's', 'z')

/* ------------------------------------------------------------------ */
/* Minimal read-only host IO (the pack only ever reads files)          */
/* ------------------------------------------------------------------ */

static Result SysRead(void *userdata,
                      const char *path,
                      size_t maxSize,
                      uint8_t **outBytes,
                      size_t *outSize)
{
    FILE *file;
    long size;
    uint8_t *bytes;

    (void)userdata;
    if (!path || !outBytes || !outSize) {
        return RoboInvalidArgument;
    }
    *outBytes = NULL;
    *outSize = 0u;
    file = fopen(path, "rb");
    if (!file) {
        return RoboNotFound;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return RoboParseError;
    }
    size = ftell(file);
    if (size <= 0L || (size_t)size > maxSize || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return RoboParseError;
    }
    /* The discovery IO contract hands the pack a RoboMem buffer to RoboFree. */
    bytes = (uint8_t *)Malloc((size_t)size);
    if (!bytes) {
        fclose(file);
        return RoboOutOfMemory;
    }
    if (fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        RoboFree(bytes);
        fclose(file);
        return RoboParseError;
    }
    fclose(file);
    *outBytes = bytes;
    *outSize = (size_t)size;
    return RoboOk;
}

static const FontDiscoveryIo *SysIo(void)
{
    static FontDiscoveryIo io;

    memset(&io, 0, sizeof(io));
    io.RoboReadFile = SysRead;
    return &io;
}

/* ------------------------------------------------------------------ */
/* A deck of paths that cannot exist                                   */
/* ------------------------------------------------------------------ */

#define NoSuchRoot "/System/NoSuchFontsRoboticela"
#define NoSuchManifest NoSuchRoot "/system-fonts.manifest"

/* ------------------------------------------------------------------ */
/* Manifest parsing                                                    */
/* ------------------------------------------------------------------ */

static void TestManifestParse(void)
{
    static const char text[] =
        "# a comment\n"
        "system-font-pack 3\n"
        "root /System/Fonts\n"
        "\n"
        "family Inter\n"
        "  file UI/Inter.ttf\n"
        "  role text weight 400\n"
        "  role display weight 600\n"
        "  weight 400\n"
        "  stretch 100\n"
        "  style normal\n"
        "  variable\n"
        "  axis wght 100 400 900\n"
        "  axis opsz 14 14 32\n"
        "  license OFL-1.1\n"
        "  source The Inter Project Authors (https://github.com/rsms/inter)\n"
        "  version 4.000\n"
        "end\n";
    FontSystemManifest m;
    const FontSystemFamily *fam;
    RoboFontSystemRole role;

    FontSystemManifestInit(&m);
    RoboCheckEqInt(FontSystemManifestParse(text, sizeof(text) - 1u, &m), RoboOk);
    RoboCheckEqInt((int)m.version, 3);
    RoboCheckEqInt((int)m.familyCount, 1);
    RoboCheck(strcmp(m.root, "/System/Fonts") == 0);

    fam = FontSystemManifestFamily(&m, "inter"); /* casefold */
    RoboCheck(fam != NULL);
    if (!fam) {
        return;
    }
    RoboCheck(strcmp(fam->family, "Inter") == 0);
    RoboCheck(strcmp(fam->file, "UI/Inter.ttf") == 0);
    RoboCheckEqInt((int)fam->roleCount, 2);
    RoboCheckEqInt((int)fam->axisCount, 2);
    RoboCheckEqInt((int)fam->weight, 400);
    RoboCheckEqInt((int)fam->stretch, 100);
    RoboCheckEqInt((int)fam->style, RoboFontStyleNormal);
    RoboCheckEqInt((int)fam->variable, 1);
    RoboCheck(strcmp(fam->licenseId, "OFL-1.1") == 0);
    RoboCheck(strcmp(fam->version, "4.000") == 0);
    RoboCheck(strstr(fam->source, "github.com/rsms/inter") != NULL);
    RoboCheckEqInt((int)fam->roles[0].role, RoboFontSystemRoleUIText);
    RoboCheckEqInt((int)fam->roles[0].weight, 400);
    RoboCheckEqInt((int)fam->roles[1].role, RoboFontSystemRoleUIDisplay);
    RoboCheckEqInt((int)fam->roles[1].weight, 600);
    RoboCheckEqInt((int)fam->axes[0].tag, RF_TAG_WGHT);
    RoboCheckEqInt((int)fam->axes[0].minValue, 100);
    RoboCheckEqInt((int)fam->axes[0].defaultValue, 400);
    RoboCheckEqInt((int)fam->axes[0].maxValue, 900);
    RoboCheckEqInt((int)fam->axes[1].tag, RF_TAG_OPSZ);
    RoboCheckEqInt((int)fam->axes[1].minValue, 14);
    RoboCheckEqInt((int)fam->axes[1].maxValue, 32);

    /* A bare parse must not invent bound fields. */
    RoboCheckEqInt((int)fam->faceCount, 0);
    RoboCheckEqInt((int)fam->resolvedVariable, 0);
    RoboCheckEqInt((int)fam->hasWghtAxis, 0);

    RoboCheckEqInt(RoboFontSystemRoleFromName("DISPLAY", 0u, &role), RoboOk);
    RoboCheckEqInt((int)role, RoboFontSystemRoleUIDisplay);
    RoboCheckEqInt(RoboFontSystemRoleFromName("bogus", 0u, &role), RoboNotFound);
    RoboCheck(strcmp(RoboFontSystemRoleName(RoboFontSystemRoleUIText), "text") ==
              0);
    RoboCheck(RoboFontSystemRoleName(RoboFontSystemRoleNone) == NULL);
    RoboCheck(!RoboFontSystemRoleIsValid(RoboFontSystemRoleNone));
}

static void TestManifestRejects(void)
{
    FontSystemManifest m;
    static const char unknown[] =
        "system-font-pack 1\nfamily A\n  file A.ttf\n  wat 1\nend\n";
    static const char unterminated[] =
        "system-font-pack 1\nfamily A\n  file A.ttf\n";
    static const char noFile[] = "system-font-pack 1\nfamily A\nend\n";
    static const char dupFamily[] =
        "system-font-pack 1\nfamily A\n  file A.ttf\nend\n"
        "family a\n  file B.ttf\nend\n";
    static const char badWeight[] =
        "system-font-pack 1\nfamily A\n  file A.ttf\n  weight 5000\nend\n";
    static const char badAxis[] =
        "system-font-pack 1\nfamily A\n  file A.ttf\n  axis wght 900 400 100\n"
        "end\n";
    static const char noFamilies[] = "system-font-pack 1\n";
    static const char badRole[] =
        "system-font-pack 1\nfamily A\n  file A.ttf\n  role nope\nend\n";

    RoboCheckEqInt(FontSystemManifestParse(unknown, sizeof(unknown) - 1u, &m),
                      RoboParseError);
    RoboCheckEqInt(
        FontSystemManifestParse(unterminated, sizeof(unterminated) - 1u, &m),
        RoboParseError);
    RoboCheckEqInt(FontSystemManifestParse(noFile, sizeof(noFile) - 1u, &m),
                      RoboParseError);
    RoboCheckEqInt(
        FontSystemManifestParse(dupFamily, sizeof(dupFamily) - 1u, &m),
        RoboParseError);
    RoboCheckEqInt(FontSystemManifestParse(badWeight, sizeof(badWeight) - 1u, &m),
                      RoboParseError);
    RoboCheckEqInt(FontSystemManifestParse(badAxis, sizeof(badAxis) - 1u, &m),
                      RoboParseError);
    RoboCheckEqInt(FontSystemManifestParse(noFamilies, sizeof(noFamilies) - 1u, &m),
                      RoboParseError);
    RoboCheckEqInt(FontSystemManifestParse(badRole, sizeof(badRole) - 1u, &m),
                      RoboParseError);
    RoboCheckEqInt(FontSystemManifestParse("", 0u, &m), RoboInvalidArgument);
    RoboCheckEqInt(FontSystemManifestParse(NULL, 5u, &m), RoboInvalidArgument);
    RoboCheckEqInt(FontSystemManifestParse("x", 1u, NULL), RoboInvalidArgument);

    /* Fuzz entry must never crash. */
    {
        uint8_t junk[128];
        size_t i;
        for (i = 0u; i < sizeof(junk); i++) {
            junk[i] = (uint8_t)(i * 37u + 11u);
        }
        RoboCheckEqInt(FontSystemPackFuzzParse(junk, sizeof(junk)), RoboOk);
        RoboCheckEqInt(FontSystemPackFuzzParse(NULL, 0u), RoboOk);
    }
}

/* ------------------------------------------------------------------ */
/* The shipped pack                                                    */
/* ------------------------------------------------------------------ */

/** Open the shipped pack; returns RoboOk and fills @p outPack. */
static Result OpenShippedPack(FontRegistry **outReg,
                              FontLoader **outLoader,
                              FontSystemPack **outPack)
{
    FontSystemPackConfig cfg;
    FontLoaderConfig loaderCfg;

    *outReg = NULL;
    *outLoader = NULL;
    *outPack = NULL;

    if (FontRegistryCreate(outReg) != RoboOk) {
        return RoboOutOfMemory;
    }
    FontLoaderConfigInit(&loaderCfg);
    if (FontLoaderCreate(*outReg, &loaderCfg, outLoader) != RoboOk) {
        FontRegistryDestroy(*outReg);
        *outReg = NULL;
        return RoboOutOfMemory;
    }
    FontSystemPackConfigInit(&cfg);
    cfg.root = RoboSystemFontRoot;
    cfg.io = SysIo();
    if (FontSystemPackOpen(*outReg, *outLoader, &cfg, outPack) != RoboOk) {
        FontLoaderDestroy(*outLoader);
        FontRegistryDestroy(*outReg);
        *outReg = NULL;
        *outLoader = NULL;
        return RoboNotFound;
    }
    return RoboOk;
}

static void CloseShippedPack(FontRegistry *reg,
                             FontLoader *loader,
                             FontSystemPack *pack)
{
    FontSystemPackClose(pack);
    FontLoaderDestroy(loader);
    FontRegistryDestroy(reg);
}

static void TestShippedPack(void)
{
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontSystemPack *pack = NULL;
    const FontSystemManifest *m;
    const FontSystemFamily *fam = NULL;
    const FontCacheStats *stats;

    if (OpenShippedPack(&reg, &loader, &pack) != RoboOk) {
        RoboCheckMsg(0, "the shipped system font pack failed to open");
        return;
    }

    m = FontSystemPackManifest(pack);
    RoboCheck(m != NULL);
    RoboCheckEqInt((int)m->version, 1);
    RoboCheckEqInt((int)FontSystemPackFamilyCount(pack), 1);
    RoboCheckEqInt((int)FontSystemPackRegisteredFaces(pack), 1);

    fam = FontSystemPackFamilyAt(pack, 0u);
    RoboCheck(fam != NULL);
    if (fam) {
        RoboCheck(strcmp(fam->family, "Inter") == 0);
        RoboCheck(strcmp(fam->file, "UI/Inter.ttf") == 0);
        RoboCheck(strcmp(fam->licenseId, "OFL-1.1") == 0);
        RoboCheckEqInt((int)fam->faceCount, 1);
        /* Inter is a variable face, and the manifest says so — the pack must
         * have confirmed it against the font rather than trusted the text. */
        RoboCheckEqInt((int)fam->resolvedVariable, 1);
        RoboCheckEqInt((int)fam->hasWghtAxis, 1);
        RoboCheckEqInt((int)fam->wghtMin, 100);
        RoboCheckEqInt((int)fam->wghtDefault, 400);
        RoboCheckEqInt((int)fam->wghtMax, 900);
    }

    /* Cache warming loaded every registration without failure. */
    RoboCheckEqInt((int)FontSystemPackLoadFailures(pack), 0);
    stats = GetFontCacheStats(FontLoaderCache(loader));
    RoboCheck(stats != NULL);
    if (stats) {
        RoboCheck(stats->entries >= 1u);
        RoboCheck(stats->inserts >= 1u);
    }

    CloseShippedPack(reg, loader, pack);
}

static void TestRoleResolution(void)
{
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontSystemPack *pack = NULL;
    const FontSystemFamily *fam = NULL;
    FontWeight weight = 0u;
    FontSelection sel;

    if (OpenShippedPack(&reg, &loader, &pack) != RoboOk) {
        RoboCheckMsg(0, "the shipped system font pack failed to open");
        return;
    }

    /* The manifest binds both roles to one face at two weights: same family,
     * no second file. */
    RoboCheckEqInt(FontSystemPackRoleBinding(pack, RoboFontSystemRoleUIText, &fam,
                                             &weight),
                      RoboOk);
    RoboCheck(fam != NULL && strcmp(fam->family, "Inter") == 0);
    RoboCheckEqInt((int)weight, 400);
    RoboCheckEqInt(FontSystemPackRoleBinding(pack, RoboFontSystemRoleUIDisplay,
                                             &fam, &weight),
                      RoboOk);
    RoboCheckEqInt((int)weight, 600);

    FontSelectionInit(&sel);
    RoboCheckEqInt(FontSystemPackResolveRole(pack, RoboFontSystemRoleUIText, 0u,
                                             0u, RoboFontStyleNormal, &sel),
                      RoboOk);
    RoboCheckEqInt((int)sel.found, 1);
    RoboCheck(sel.face != NULL);
    FontSelectionClear(&sel);

    /* A display-weight request resolves through the wght axis. */
    FontSelectionInit(&sel);
    RoboCheckEqInt(
        FontSystemPackResolveRole(pack, RoboFontSystemRoleUIDisplay, 0u, 0u,
                                  RoboFontStyleNormal, &sel),
        RoboOk);
    RoboCheckEqInt((int)sel.found, 1);
    FontSelectionClear(&sel);

    /* An explicit heavier weight is carried as an axis setting, not a
     * synthesized emboldening. */
    FontSelectionInit(&sel);
    RoboCheckEqInt(
        FontSystemPackResolveRole(pack, RoboFontSystemRoleUIText, 700u, 0u,
                                  RoboFontStyleNormal, &sel),
        RoboOk);
    RoboCheckEqInt((int)sel.found, 1);
    FontSelectionClear(&sel);

    /* No face declares an italic slope, so the family gate still matches but
     * the selector must not pretend otherwise — it returns the family face. */
    FontSelectionInit(&sel);
    RoboCheckEqInt(
        FontSystemPackResolveRole(pack, RoboFontSystemRoleUIText, 0u, 0u,
                                  RoboFontStyleItalic, &sel),
        RoboOk);
    RoboCheckEqInt((int)sel.found, 1);
    FontSelectionClear(&sel);

    CloseShippedPack(reg, loader, pack);
}

static void TestFallbackAndGeneric(void)
{
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontSystemPack *pack = NULL;
    FontFallbackResolver *fallback = NULL;
    FontFallbackPolicy policy;
    FontGenericResolver *generic = NULL;
    FontGenericResolverOptions opts;
    FontGenericConfig *config = NULL;
    FontGenericRequest greq;
    FontGenericResult gres;
    FontFallbackResult fb;
    FontSelection sel;
    uint32_t cps[1];

    if (OpenShippedPack(&reg, &loader, &pack) != RoboOk) {
        RoboCheckMsg(0, "the shipped system font pack failed to open");
        return;
    }

    FontFallbackPolicyInit(&policy);
    RoboCheckEqInt(FontFallbackResolverCreate(reg, &policy, &fallback), RoboOk);

    /* 'A' is covered by the system face, so the fast path is the primary. */
    cps[0] = 'A';
    FontSelectionInit(&sel);
    FontFallbackResultInit(&fb);
    RoboCheckEqInt(FontSystemPackResolveCodepoints(
                       pack, fallback, RoboFontSystemRoleUIText, 0u,
                       RoboFontStyleNormal, cps, 1u, &sel, &fb),
                   RoboOk);
    RoboCheckEqInt((int)sel.found, 1);
    RoboCheckEqInt((int)fb.found, 1);
    RoboCheckEqInt((int)fb.usedPrimary, 1);
    FontFallbackResultClear(&fb);
    FontSelectionClear(&sel);

    /* The generics bridge: system-ui resolves to the pack's UI Text family. */
    RoboCheckEqInt(FontGenericConfigCreate(&config), RoboOk);
    RoboCheckEqInt(FontSystemPackApplyGenericConfig(pack, config), RoboOk);
    FontGenericResolverOptionsInit(&opts);
    opts.platform = config;
    RoboCheckEqInt(FontGenericResolverCreate(reg, &opts, &generic), RoboOk);
    FontGenericRequestInit(&greq);
    greq.generic = RoboFontGenericSystemUi;
    FontGenericResultInit(&gres);
    RoboCheckEqInt(FontGenericResolve(generic, &greq, &gres), RoboOk);
    RoboCheckEqInt((int)gres.resolved, 1);
    RoboCheckEqInt((int)gres.familyCount, 1);
    if (gres.familyCount == 1u) {
        RoboCheck(strcmp(gres.families[0], "Inter") == 0);
    }
    FontGenericResultClear(&gres);

    FontGenericResolverDestroy(generic);
    FontGenericConfigRelease(config);
    FontFallbackResolverDestroy(fallback);
    CloseShippedPack(reg, loader, pack);
}

static void TestMissingManifestAndFile(void)
{
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontSystemPack *pack = NULL;
    FontSystemPackConfig cfg;
    FontDiscoveryVirtualIo *vio = NULL;
    FontLoaderConfig loaderCfg;
    static const char missingFileManifest[] =
        "system-font-pack 1\nfamily Inter\n  file UI/Inter.ttf\n  role text weight 400\nend\n";

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    FontLoaderConfigInit(&loaderCfg);
    RoboCheckEqInt(FontLoaderCreate(reg, &loaderCfg, &loader), RoboOk);
    RoboCheckEqInt(FontDiscoveryVirtualIoCreate(&vio), RoboOk);
    RoboCheckEqInt(FontDiscoveryVirtualAddDir(vio, "/Fonts"), RoboOk);

    /* A root with no manifest is a hard error, not an empty pack. */
    FontSystemPackConfigInit(&cfg);
    cfg.root = NoSuchRoot;
    cfg.io = SysIo();
    RoboCheckEqInt(FontSystemPackOpen(reg, loader, &cfg, &pack), RoboNotFound);
    RoboCheck(pack == NULL);

    /* A manifest naming a file that is not installed is also a hard error: the
     * pack must never silently ship without the face it promised. */
    RoboCheckEqInt(FontDiscoveryVirtualAddFile(
                       vio, "/Fonts/system-fonts.manifest", missingFileManifest,
                       sizeof(missingFileManifest) - 1u),
                   RoboOk);
    cfg.root = "/Fonts";
    cfg.io = FontDiscoveryVirtualIoOps(vio);
    RoboCheckEqInt(FontSystemPackOpen(reg, loader, &cfg, &pack), RoboNotFound);
    RoboCheck(pack == NULL);
    /* A failed open leaves no registrations behind. */
    RoboCheckEqInt((int)FontRegistryRegistrationCount(reg), 0);

    FontDiscoveryVirtualIoDestroy(vio);
    FontLoaderDestroy(loader);
    FontRegistryDestroy(reg);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterFontSystemPack(void)
{
    TestRegister("font.systempack.manifest_parse", TestManifestParse);
    TestRegister("font.systempack.manifest_reject", TestManifestRejects);
    TestRegister("font.systempack.shipped_open", TestShippedPack);
    TestRegister("font.systempack.resolve_role", TestRoleResolution);
    TestRegister("font.systempack.fallback_generic", TestFallbackAndGeneric);
    TestRegister("font.systempack.missing", TestMissingManifestAndFile);
}
