/*
 * Shared RoboText test fixtures (Phase 12.4).
 *
 * Every suite under `Tests/Text` includes this. Like the typography, desktop and paint support headers it
 * is **not** a framework — the runtime has exactly one of those (`Tests/Framework/RoboTest.h`) — and it
 * holds the one thing the suites would otherwise each copy: **a font context bound to a face the shipped
 * system typeface pack installed**.
 *
 * It opens the pack through **RoboFont alone**, deliberately — no typography resolver, no roles layer. The
 * module's declared dependencies are `RoboBase`, `RoboMem`, `RoboUnicode` and `RoboFont`, and its suite
 * honours the same list: a fixture that linked the layer above would let a text change that broke the
 * boundary compile and pass, which is the failure the boundary scan exists to prevent. What the pack gives
 * here is a `RoboFontSystemRole` resolved to a `FontSelection`, and what this header adds is the binding of
 * that retained face into a `RoboTextFontContext` at a size.
 *
 * That chain is the reason a Text suite uses a real face at all. A text layer tested against a synthetic
 * face could pass while every advance it produced was wrong: the numbers here come from `hmtx` through
 * RoboFont, so `text.glyphs.*` and `text.shape.*` are measured against the *shipped* pack, and a broken
 * pack or a broken scaling door fails them.
 *
 * A suite that wants *no* face calls `TextFixtureNoFace`, which is a supported context in this build (a
 * run's script and direction are properties of the text, not the font) and is what the refusal cases
 * assert against.
 */

#ifndef ROBO_TEXT_TEST_SUPPORT_H
#define ROBO_TEXT_TEST_SUPPORT_H

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"
#include "RoboText/RoboText.h"

#include <stdio.h>
#include <string.h>

#if defined(RoboSystemFontRootFromBuild)
#include "robo_system_font_root.h"
#endif
#ifndef RoboSystemFontRoot
#define RoboSystemFontRoot "System/Fonts"
#endif

/* ------------------------------------------------------------------ */
/* The host IO adapter, and the pack it opens                          */
/* ------------------------------------------------------------------ */

/**
 * Read @p path whole into a `RoboMem` buffer, which the pack releases with `RoboFree`.
 *
 * The contract is `FontDiscoveryIo`'s and the three answers are its three: `RoboNotFound` for a missing
 * file, `RoboParseError` for a file that is not whole, `RoboOutOfMemory` for a buffer that would not
 * allocate. A file larger than @p maxSize is refused rather than truncated — the pack caps a manifest at
 * its own limit, and a silently truncated manifest would parse as a *different* manifest.
 */
static inline Result TextReadFile(void *userdata,
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
static inline const FontDiscoveryIo *TextHostIo(void)
{
    static FontDiscoveryIo io;

    memset(&io, 0, sizeof(io));
    io.RoboReadFile = TextReadFile;
    return &io;
}

/* ------------------------------------------------------------------ */
/* A face from the shipped pack, bound at a size                       */
/* ------------------------------------------------------------------ */

/** The pack, one resolved and retained face, and the text context bound to it. */
typedef struct TextFixture {
    FontRegistry *registry;
    FontLoader *loader;
    FontSystemPack *pack;
    FontSelection selection;
    RoboTextFontContext font;
    /** The face the pack's role selected, borrowed from @ref selection. */
    FontFace *face;
} TextFixture;

/** A fixture holding nothing: no pack, no face, a context at the CSS initial setting. */
static inline TextFixture TextFixtureEmpty(void)
{
    TextFixture fixture;

    memset(&fixture, 0, sizeof(fixture));
    RoboTextFontContextInit(&fixture.font);
    return fixture;
}

/** Open the shipped pack at @p root. The scaffolding asserts, as every other suite's fixture does. */
static inline TextFixture TextFixtureOpenPack(const char *root)
{
    TextFixture fixture = TextFixtureEmpty();
    FontLoaderConfig loaderConfig;
    FontSystemPackConfig packConfig;

    RoboCheckMsg(FontRegistryCreate(&fixture.registry) == RoboOk,
                 "the font registry could not be created");
    FontLoaderConfigInit(&loaderConfig);
    RoboCheckMsg(FontLoaderCreate(fixture.registry, &loaderConfig, &fixture.loader) == RoboOk,
                 "the font loader could not be created");
    FontSystemPackConfigInit(&packConfig);
    packConfig.root = root ? root : RoboSystemFontRoot;
    packConfig.io = TextHostIo();
    RoboCheckMsg(FontSystemPackOpen(fixture.registry, fixture.loader, &packConfig, &fixture.pack) ==
                     RoboOk,
                 "the shipped system typeface pack failed to open");
    RoboCheckMsg(fixture.pack != NULL, "the shipped system typeface pack is absent");
    return fixture;
}

/**
 * Resolve @p role at @p weight on the *shipped* pack and bind it at @p sizePx.
 *
 * The scaffolding asserts rather than returning a status, the convention the layout, paint, render and
 * desktop suites set: a suite that checked each step of its own harness would drown its subject in it. A
 * role the pack cannot resolve is the phase's own failure and the message names it.
 */
static inline TextFixture TextFixtureOpenRoleAt(const char *root,
                                                RoboFontSystemRole role,
                                                FontWeight weight,
                                                float sizePx)
{
    TextFixture fixture = TextFixtureOpenPack(root);
    RoboTextFontStyle style;

    FontSelectionInit(&fixture.selection);
    RoboCheckMsg(FontSystemPackResolveRole(fixture.pack, role, weight, RoboFontStretchNormal,
                                           RoboFontStyleNormal, &fixture.selection) == RoboOk,
                 "the pack could not resolve the role");
    RoboCheckMsg(fixture.selection.found != 0 && fixture.selection.face != NULL,
                 "the pack resolved a role with no face");
    fixture.face = fixture.selection.face;

    RoboTextFontStyleInit(&style);
    style.sizePx = sizePx;
    style.weight = weight;
    RoboCheckMsg(RoboTextFontContextSet(&fixture.font, fixture.face, &style) == RoboOk,
                 "the text font context would not take the resolved face");
    return fixture;
}

/** The UI Text role at 400, at the CSS initial size: what most suites want. */
static inline TextFixture TextFixtureOpen(void)
{
    return TextFixtureOpenRoleAt(NULL, RoboFontSystemRoleUIText, (FontWeight)400u, 16.0f);
}

/** The UI Text role at a chosen size, for the cases that need to move the pen. */
static inline TextFixture TextFixtureOpenRole(RoboFontSystemRole role, FontWeight weight, float sizePx)
{
    return TextFixtureOpenRoleAt(NULL, role, weight, sizePx);
}

/** A context with no face and a valid setting. */
static inline TextFixture TextFixtureNoFace(void)
{
    TextFixture fixture = TextFixtureEmpty();
    RoboTextFontStyle style;

    RoboTextFontStyleInit(&style);
    RoboCheckMsg(RoboTextFontContextSet(&fixture.font, NULL, &style) == RoboOk,
                 "a context with no face would not take the initial setting");
    return fixture;
}

/** Release the fixture. The context first (it borrows the face), then the face, then the pack. */
static inline void TextFixtureClose(TextFixture *fixture)
{
    if (!fixture) {
        return;
    }
    RoboTextFontContextInit(&fixture->font);
    FontSelectionClear(&fixture->selection);
    fixture->face = NULL;
    FontSystemPackClose(fixture->pack);
    fixture->pack = NULL;
    FontLoaderDestroy(fixture->loader);
    fixture->loader = NULL;
    FontRegistryDestroy(fixture->registry);
    fixture->registry = NULL;
}

/* ------------------------------------------------------------------ */
/* The test strings the phase names                                    */
/* ------------------------------------------------------------------ */

/** `"Hello RoboticelaOS"` — the phase's first required string. */
#define TextSampleHello "Hello RoboticelaOS"
/** `"Typography"`. */
#define TextSampleTypography "Typography"
/** The pangram, which a test uses to prove every ASCII letter and space advances. */
#define TextSamplePangram "The quick brown fox jumps over the lazy dog."
/** Upper case, lower case and digits, the three ranges a Latin UI face must cover. */
#define TextSampleUpper "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define TextSampleLower "abcdefghijklmnopqrstuvwxyz"
#define TextSampleDigits "0123456789"

/**
 * The Unicode samples the phase names, as UTF-8.
 *
 * `é` (2 bytes), `Å` (2), `Ω` (2), `中` (3) and `🙂` (4) — chosen so that the *encoding* varies as much as
 * the script. The purpose at this stage is safe representation and a fallback boundary, not shaping, so
 * the suites check that they decode to the right scalar counts, measure without crashing, and are counted
 * honestly when the face has no glyph — never that they shape.
 */
#define TextSampleUnicode "\xC3\xA9 \xC3\x85 \xCE\xA9 \xE4\xB8\xAD \xF0\x9F\x99\x82"

/**
 * A scalar **no conforming font can have a glyph for**: `U+FDD0`, a Unicode noncharacter.
 *
 * The one sample whose absence from the face is guaranteed rather than merely likely, which is what lets a
 * suite assert on `missingGlyphs` (the count that keeps a plausible width for undrawable text from passing
 * as a measurement) without depending on which glyphs this build's face happens to carry.
 */
#define TextSampleNoncharacter "\xEF\xB7\x90"

#endif /* ROBO_TEXT_TEST_SUPPORT_H */
