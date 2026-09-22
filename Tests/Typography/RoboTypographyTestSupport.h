/*
 * Shared RoboTypography test fixtures (Phase 12.3).
 *
 * Every suite under `Tests/Typography` includes this. Like the desktop and paint support headers it is
 * **not** a framework -- the runtime has exactly one of those (`Tests/Framework/RoboTest.h`), and
 * Phase 12.3 adds none -- and it holds the two things the suites would otherwise each copy:
 *
 *   an **IO adapter**   a minimal read-only `FontDiscoveryIo` over `stdio`. The pack's only IO is
 *                       "read this path whole", so the adapter is one function. It exists so the
 *                       *same* discovery / registry / loader / selection / pack code the guest runs
 *                       is what the host suites exercise: a typography suite that built faces by hand
 *                       could pass while the shipped pack was broken, which is the one failure this
 *                       phase's tests exist to catch.
 *
 *   an **open pack**    `TypographyFixtureOpen` opens the shipped system typeface pack (the repository's
 *                       `System/Fonts/`, installed into the image at `/System/Fonts/`) and builds the
 *                       fallback resolver and the typography resolver over it. The scaffolding asserts
 *                       rather than returning statuses, which is the convention the layout, paint,
 *                       render and desktop suites set: a suite that checked each step of its own
 *                       harness would drown its subject in it.
 *
 * A suite that wants *no* face (the "family not installed" case) opens a pack over a virtual
 * filesystem instead, with the real face bytes planted at a path whose manifest omits the role it
 * asks for -- see `RoboTypographyVirtualIo`.
 */

#ifndef ROBO_TYPOGRAPHY_TEST_SUPPORT_H
#define ROBO_TYPOGRAPHY_TEST_SUPPORT_H

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"
#include "RoboTypography/RoboTypography.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(RoboSystemFontRootFromBuild)
#include "robo_system_font_root.h"
#endif
#ifndef RoboSystemFontRoot
#define RoboSystemFontRoot "System/Fonts"
#endif
#if defined(RoboFontAssetPathFromBuild)
#include "robo_font_asset_path.h"
#endif
#ifndef RoboFontAssetPath
#define RoboFontAssetPath "System/Fonts/UI/Inter.ttf"
#endif

/* ------------------------------------------------------------------ */
/* The host IO adapter                                                 */
/* ------------------------------------------------------------------ */

/**
 * Read @p path whole into a `RoboMem` buffer, which the pack releases with `RoboFree`.
 *
 * The contract is `FontDiscoveryIo`'s, and the three answers are its three: `RoboNotFound` for a
 * missing file, `RoboParseError` for a file that is not whole (a size that will not read, a short
 * read), `RoboOutOfMemory` for a buffer that would not allocate. A file larger than @p maxSize is
 * refused rather than truncated -- the pack caps a manifest at its own limit, and a silently
 * truncated manifest would parse as a *different* manifest.
 */
static inline Result TypographyReadFile(void *userdata,
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

/** The one adapter, shared: it holds no state, so a static is the honest shape. */
static inline const FontDiscoveryIo *TypographyHostIo(void)
{
    static FontDiscoveryIo io;

    memset(&io, 0, sizeof(io));
    io.RoboReadFile = TypographyReadFile;
    return &io;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** Everything a typography suite borrows: the pack, its loader and cache, and the two resolvers. */
typedef struct TypographyFixture {
    FontRegistry *registry;
    FontLoader *loader;
    FontSystemPack *pack;
    FontFallbackResolver *fallback;
    TypographyResolver *resolver;
} TypographyFixture;

/** A fixture that holds nothing; the state a failed open leaves behind. */
static inline TypographyFixture TypographyFixtureEmpty(void)
{
    TypographyFixture fixture;

    memset(&fixture, 0, sizeof(fixture));
    return fixture;
}

/**
 * Open the shipped pack at @p root and build the resolvers over it.
 *
 * The scaffolding asserts each step, and the assertion message names the step, because an
 * unopenable shipped pack is the phase's own failure and a reader should not have to work out from
 * four `RoboCheck`s in a row which one gave.
 */
static inline TypographyFixture TypographyFixtureOpen(const char *root)
{
    TypographyFixture fixture = TypographyFixtureEmpty();
    RoboTypographyResolverConfig config;

    RoboCheckMsg(FontRegistryCreate(&fixture.registry) == RoboOk,
                 "the font registry could not be created");
    {
        FontLoaderConfig loaderConfig;

        FontLoaderConfigInit(&loaderConfig);
        RoboCheckMsg(FontLoaderCreate(fixture.registry, &loaderConfig, &fixture.loader) == RoboOk,
                     "the font loader could not be created");
    }
    {
        FontSystemPackConfig packConfig;

        FontSystemPackConfigInit(&packConfig);
        packConfig.root = root ? root : RoboSystemFontRoot;
        packConfig.io = TypographyHostIo();
        RoboCheckMsg(
            FontSystemPackOpen(fixture.registry, fixture.loader, &packConfig, &fixture.pack) ==
                RoboOk,
            "the shipped system typeface pack failed to open");
    }
    if (!fixture.pack) {
        return fixture;
    }
    RoboCheckMsg(FontFallbackResolverCreate(fixture.registry, NULL, &fixture.fallback) == RoboOk,
                 "the fallback resolver could not be created");
    RoboTypographyResolverConfigInit(&config);
    config.pack = fixture.pack;
    config.fallback = fixture.fallback;
    RoboCheckMsg(RoboTypographyResolverCreate(&config, &fixture.resolver, NULL) == RoboOk,
                 "the typography resolver could not be created");
    return fixture;
}

/** Release the fixture, in the reverse of the order it was built. Safe on an empty fixture. */
static inline void TypographyFixtureClose(TypographyFixture *fixture)
{
    if (!fixture) {
        return;
    }
    RoboTypographyResolverDestroy(fixture->resolver);
    fixture->resolver = NULL;
    FontFallbackResolverDestroy(fixture->fallback);
    fixture->fallback = NULL;
    FontSystemPackClose(fixture->pack);
    fixture->pack = NULL;
    FontLoaderDestroy(fixture->loader);
    fixture->loader = NULL;
    FontRegistryDestroy(fixture->registry);
    fixture->registry = NULL;
}

/* ------------------------------------------------------------------ */
/* Virtual IO, for the cases a real filesystem cannot express          */
/* ------------------------------------------------------------------ */

/** A virtual filesystem and the pack opened over it, for a manifest no shipped file declares. */
typedef struct TypographyVirtualFixture {
    FontDiscoveryVirtualIo *files;
    FontRegistry *registry;
    FontLoader *loader;
    FontSystemPack *pack;
    FontFallbackResolver *fallback;
    TypographyResolver *resolver;
} TypographyVirtualFixture;

/**
 * Build a virtual pack whose manifest is @p manifest and whose `UI/Inter.ttf` is the **real face**.
 *
 * The face bytes are read from the repository's own `System/Fonts/UI/Inter.ttf`, so a case that
 * proves "a role the manifest omits resolves without a face" is exercising a genuine parseable face
 * rather than a stub -- and a manifest that declares the role is the same fixture with one line
 * changed.
 */
static inline TypographyVirtualFixture TypographyVirtualPack(const char *manifest)
{
    TypographyVirtualFixture fixture;
    unsigned char *bytes = NULL;
    size_t size = 0u;
    FontLoaderConfig loaderConfig;
    FontSystemPackConfig packConfig;
    RoboTypographyResolverConfig config;
    FILE *file;

    memset(&fixture, 0, sizeof(fixture));
    RoboCheckMsg(FontRegistryCreate(&fixture.registry) == RoboOk,
                 "the virtual fixture's registry could not be created");
    FontLoaderConfigInit(&loaderConfig);
    RoboCheckMsg(FontLoaderCreate(fixture.registry, &loaderConfig, &fixture.loader) == RoboOk,
                 "the virtual fixture's loader could not be created");
    RoboCheckMsg(FontDiscoveryVirtualIoCreate(&fixture.files) == RoboOk,
                 "the virtual filesystem could not be created");
    RoboCheckMsg(FontDiscoveryVirtualAddDir(fixture.files, "/System/Fonts") == RoboOk,
                 "the virtual root could not be added");

    file = fopen(RoboFontAssetPath, "rb");
    RoboCheckMsg(file != NULL, "the system face could not be read for the virtual fixture");
    if (file) {
        long length;

        (void)fseek(file, 0, SEEK_END);
        length = ftell(file);
        (void)fseek(file, 0, SEEK_SET);
        if (length > 0L) {
            bytes = (unsigned char *)malloc((size_t)length);
            if (bytes && fread(bytes, 1u, (size_t)length, file) == (size_t)length) {
                size = (size_t)length;
            }
        }
        fclose(file);
    }
    RoboCheckMsg(size > 0u, "the system face could not be read for the virtual fixture");
    if (size > 0u) {
        RoboCheckMsg(FontDiscoveryVirtualAddFile(fixture.files, "/System/Fonts/UI/Inter.ttf", bytes,
                                                 size) == RoboOk,
                     "the virtual face could not be added");
    }
    free(bytes);
    RoboCheckMsg(FontDiscoveryVirtualAddFile(fixture.files, "/System/Fonts/system-fonts.manifest",
                                             manifest, strlen(manifest)) == RoboOk,
                 "the virtual manifest could not be added");

    FontSystemPackConfigInit(&packConfig);
    packConfig.root = "/System/Fonts";
    packConfig.io = FontDiscoveryVirtualIoOps(fixture.files);
    RoboCheckMsg(FontSystemPackOpen(fixture.registry, fixture.loader, &packConfig, &fixture.pack) ==
                     RoboOk,
                 "the virtual pack failed to open");
    if (fixture.pack) {
        RoboCheckMsg(FontFallbackResolverCreate(fixture.registry, NULL, &fixture.fallback) == RoboOk,
                     "the virtual fixture's fallback resolver could not be created");
        RoboTypographyResolverConfigInit(&config);
        config.pack = fixture.pack;
        config.fallback = fixture.fallback;
        RoboCheckMsg(RoboTypographyResolverCreate(&config, &fixture.resolver, NULL) == RoboOk,
                     "the virtual pack's resolver could not be created");
    }
    return fixture;
}

static inline void TypographyVirtualClose(TypographyVirtualFixture *fixture)
{
    if (!fixture) {
        return;
    }
    RoboTypographyResolverDestroy(fixture->resolver);
    fixture->resolver = NULL;
    FontFallbackResolverDestroy(fixture->fallback);
    fixture->fallback = NULL;
    FontSystemPackClose(fixture->pack);
    fixture->pack = NULL;
    FontDiscoveryVirtualIoDestroy(fixture->files);
    fixture->files = NULL;
    FontLoaderDestroy(fixture->loader);
    fixture->loader = NULL;
    FontRegistryDestroy(fixture->registry);
    fixture->registry = NULL;
}

#endif /* ROBO_TYPOGRAPHY_TEST_SUPPORT_H */
