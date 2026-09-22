/*
 * RoboFontSystemPack — the RoboticelaOS system typeface pack (Phase 12.2).
 *
 * Ties the existing Phase 5 pipeline to one authoritative location:
 *
 *   /System/Fonts/system-fonts.manifest
 *        ↓ parse (declared roles, weights, axes, licences)
 *   FontRegistry + FontDiscoveryIo      ← the same discovery/registry code as 5.12
 *        ↓ scan + load (FontLoader, the same cache as 5.13)
 *   FontSelection                       ← the same matcher as 5.11
 *        ↓
 *   a face for a *semantic role* (UI Display / UI Text / Monospace)
 *
 * What this module is **not**:
 *   - it does not parse SFNT, cmap, fvar or outlines: the face does all of that;
 *   - it does not rasterize: Phase 12.1's RoboGraphicsGlyph owns that and is
 *     untouched by this phase;
 *   - it does not shape: RoboText is Phase 12.4+.
 *
 * A caller asks for a role, never a filename. The manifest's concrete family
 * and file names stay an installation detail.
 *
 * Thread safety: like the registry and loader, a pack is single-owner for
 * mutation and safe for concurrent readers while no mutation runs.
 * Ownership: a pack borrows its registry and optional loader; both must outlive
 * it. The pack owns its manifest copy and the per-family candidate arrays.
 */

#ifndef ROBO_FONT_SYSTEM_PACK_H
#define ROBO_FONT_SYSTEM_PACK_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontDiscovery.h"
#include "RoboFont/RoboFontFallback.h"
#include "RoboFont/RoboFontGenericFamily.h"
#include "RoboFont/RoboFontLoader.h"
#include "RoboFont/RoboFontRegistry.h"
#include "RoboFont/RoboFontSelection.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontVariation.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Canonical runtime root. Every system face lives under here. */
#define RoboFontSystemDefaultRoot "/System/Fonts"
/** The manifest's file name, relative to the root. */
#define RoboFontSystemManifestName "system-fonts.manifest"

#define RoboFontSystemMaxFamilies 32u
#define RoboFontSystemMaxRolesPerFamily 8u
#define RoboFontSystemMaxAxesPerFamily 16u
#define RoboFontSystemMaxNameBytes 128u
#define RoboFontSystemMaxPathBytes 256u
#define RoboFontSystemMaxTextBytes 256u
#define RoboFontSystemManifestMaxBytes (256u * 1024u)

/* ------------------------------------------------------------------ */
/* Semantic roles                                                      */
/* ------------------------------------------------------------------ */

/**
 * Logical typographic roles. These are Roboticela concepts, not font names:
 * the manifest decides which concrete family and weight each one resolves to.
 *
 *   UIDisplay   prominent titles and large labels
 *   UIText      normal interface text: menus, controls, document body
 *   Monospace   code, paths, tabular data (a face if one is packaged)
 */
typedef enum RoboFontSystemRole {
    RoboFontSystemRoleNone = 0,
    RoboFontSystemRoleUIDisplay = 1,
    RoboFontSystemRoleUIText = 2,
    RoboFontSystemRoleMonospace = 3
} RoboFontSystemRole;

/** Stable lower-case name (`"display"`, `"text"`, `"monospace"`); NULL if none. */
const char *RoboFontSystemRoleName(RoboFontSystemRole role);

/** Parse a role name (ASCII, case-insensitive). Unknown → RoboNotFound. */
Result RoboFontSystemRoleFromName(const char *name,
                                                  size_t nameLen,
                                                  RoboFontSystemRole *out);

int RoboFontSystemRoleIsValid(RoboFontSystemRole role);

/* ------------------------------------------------------------------ */
/* Manifest (a pure parse — no IO, no registry)                        */
/* ------------------------------------------------------------------ */

/** One declared variable-font axis (user-space coordinates). */
typedef struct FontSystemAxis {
    FontTag tag;
    float minValue;
    float defaultValue;
    float maxValue;
} FontSystemAxis;

/** role → weight the role resolves to when a caller does not name one. */
typedef struct FontSystemRoleBinding {
    RoboFontSystemRole role;
    FontWeight weight;
} FontSystemRoleBinding;

/**
 * One declared family. Fields above the "bound at open" line come from the
 * manifest; the ones after it are filled only by a successful pack open and are
 * never invented by the parser.
 */
typedef struct FontSystemFamily {
    char family[RoboFontSystemMaxNameBytes];
    char file[RoboFontSystemMaxPathBytes]; /* relative to the pack root */
    char licenseId[RoboFontSystemMaxNameBytes];
    char source[RoboFontSystemMaxTextBytes];
    char version[64];
    FontWeight weight;   /* declared default-instance weight (0 = unspecified) */
    FontStretch stretch; /* declared stretch (0 = unspecified) */
    FontStyle style;
    uint8_t variable; /* manifest says the file carries fvar */
    FontSystemRoleBinding roles[RoboFontSystemMaxRolesPerFamily];
    uint8_t roleCount;
    FontSystemAxis axes[RoboFontSystemMaxAxesPerFamily];
    uint8_t axisCount;

    /* --- bound at open (0 after a bare parse) --- */
    uint32_t faceCount;      /* registered faces for this family */
    uint8_t resolvedVariable;/* the loaded face actually reports fvar */
    uint8_t hasWghtAxis;     /* the loaded face exposes a wght axis */
    float wghtMin;
    float wghtDefault;
    float wghtMax;
} FontSystemFamily;

typedef struct FontSystemManifest {
    uint32_t version; /* `system-font-pack <n>`; 0 if the header was absent */
    char root[RoboFontSystemMaxPathBytes];
    FontSystemFamily families[RoboFontSystemMaxFamilies];
    uint32_t familyCount;
} FontSystemManifest;

/** Zero a manifest. Safe on NULL. */
void FontSystemManifestInit(FontSystemManifest *manifest);

/**
 * Parse manifest text. @p text need not be NUL-terminated.
 *
 * Errors: RoboInvalidArgument (NULL / empty / bad argument), RoboParseError
 * (unrecognized directive, malformed number, missing family/file, duplicate
 * family name, unterminated `family`, too many entries), RoboOutOfMemory.
 * A parse never touches a filesystem and never invents metadata.
 */
Result FontSystemManifestParse(const char *text,
                                              size_t length,
                                              FontSystemManifest *out);

/** Borrow a family by casefold name; NULL when absent. NULL manifest OK. */
const FontSystemFamily *FontSystemManifestFamily(
    const FontSystemManifest *manifest,
    const char *family);

/* ------------------------------------------------------------------ */
/* Pack                                                                */
/* ------------------------------------------------------------------ */

typedef struct FontSystemPack FontSystemPack;

typedef struct FontSystemPackConfig {
    /** Runtime root; NULL → RoboFontSystemDefaultRoot. */
    const char *root;
    /** Manifest file name; NULL → RoboFontSystemManifestName. */
    const char *manifestName;
    /** Required: how manifest and face bytes are read. */
    const FontDiscoveryIo *io;
    /** Loader policy used when warming the cache; 0 → STANDARD. */
    FontLoadPolicy loadPolicy;
    /** Reserved; pass 0. */
    uint32_t flags;
} FontSystemPackConfig;

void FontSystemPackConfigInit(FontSystemPackConfig *config);

/**
 * Open the pack under @p config->root against @p registry.
 *
 * Flow: read the manifest → parse → register + scan each declared file →
 * (when @p loader is non-NULL) load each registration to warm the cache →
 * bind roles and verify the declared variable axes against the loaded faces.
 *
 * A missing manifest, a missing face, or a face whose declared family does not
 * resolve is a **hard error** — the pack is what makes "the image shipped its
 * system font" checkable, so a silent partial pack is not an option.
 *
 * @param registry Borrowed; must outlive the pack. Its IO is set from config.
 * @param loader   Borrowed and optional (NULL skips cache warming).
 * @param out      Receives the pack; untouched on failure.
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound (manifest/family face
 *         absent); RoboParseError (malformed manifest); RoboOutOfMemory.
 */
Result FontSystemPackOpen(FontRegistry *registry,
                                         FontLoader *loader,
                                         const FontSystemPackConfig *config,
                                         FontSystemPack **out);

/** Close @p pack. Safe on NULL. Does not touch the borrowed registry/loader. */
void FontSystemPackClose(FontSystemPack *pack);

const char *FontSystemPackRoot(const FontSystemPack *pack);
const FontSystemManifest *FontSystemPackManifest(const FontSystemPack *pack);

uint32_t FontSystemPackFamilyCount(const FontSystemPack *pack);
const FontSystemFamily *FontSystemPackFamilyAt(const FontSystemPack *pack,
                                                              uint32_t index);

/** Total registered faces across the pack. */
uint32_t FontSystemPackRegisteredFaces(const FontSystemPack *pack);
/** Registrations the loader refused during cache warming, if a loader was given. */
uint32_t FontSystemPackLoadFailures(const FontSystemPack *pack);

/**
 * Bind a role to a family + weight. First manifest entry that names the role
 * wins. @p outWeight receives the role's default weight (0 if unspecified).
 * @return RoboOk; RoboNotFound when no family declares the role.
 */
Result FontSystemPackRoleBinding(const FontSystemPack *pack,
                                                RoboFontSystemRole role,
                                                const FontSystemFamily **outFamily,
                                                FontWeight *outWeight);

/**
 * Resolve a role to a concrete face via the Phase 5 selector.
 *
 * @param weight  0 → the role's declared weight.
 * @param stretch 0 → normal.
 * @param style   requested slope.
 *
 * When the bound family is variable and exposes a matching `wght` axis, the
 * requested weight is carried as an explicit axis setting so the selector can
 * prefer the variable face over its nearest static neighbour. Nothing is
 * synthesized: a weight outside the axis range is clamped by selection rules,
 * never by an emboldening transform.
 *
 * @return RoboOk and out->found == 0 when no eligible face exists; RoboNotFound
 *         when no family declares the role; RoboInvalidArgument on bad args.
 */
Result FontSystemPackResolveRole(const FontSystemPack *pack,
                                               RoboFontSystemRole role,
                                               FontWeight weight,
                                               FontStretch stretch,
                                               FontStyle style,
                                               FontSelection *out);

/**
 * Resolve a role, then resolve a font that *covers* @p codepoints through the
 * Phase 5 fallback chain with the pack's family as the preferred family.
 * No shaping: this only chooses a face.
 *
 * @p selectionOut (optional) receives the role's primary selection and must be
 * initialized with FontSelectionInit before the call; @p fallbackOut is
 * required and receives the coverage decision.
 */
Result FontSystemPackResolveCodepoints(
    const FontSystemPack *pack,
    FontFallbackResolver *fallback,
    RoboFontSystemRole role,
    FontWeight weight,
    FontStyle style,
    const uint32_t *codepoints,
    uint32_t codepointCount,
    FontSelection *selectionOut,      /* optional */
    FontFallbackResult *fallbackOut); /* required */

/**
 * Point the `system-ui`, `ui-sans-serif` and `sans-serif` generic families at
 * the pack's UI Text family (and `ui-monospace`/`monospace` at the packaged
 * monospace family when one exists). This is the smallest CSS-facing bridge
 * Phase 12.2 owns; full CSS font matching is Phase 18.
 */
Result FontSystemPackApplyGenericConfig(const FontSystemPack *pack,
                                                      FontGenericConfig *config);

/** Fuzz entry: arbitrary manifest-ish bytes. Never crashes. */
Result FontSystemPackFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_SYSTEM_PACK_H */
