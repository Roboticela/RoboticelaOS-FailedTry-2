/*
 * RoboFont discovery — locate fonts from explicit sources (Goal 5.12).
 *
 * Flow:
 *   discovery source → filter → load bytes → SFNT/TTC probe → face create →
 *   metadata + classification (lazy outlines/cmap) → registry
 *
 * No OS-wide system font scanning. Platform code supplies paths / IO later.
 * Core library ships a virtual (in-memory) IO for tests and app-bundled fonts.
 *
 * Forbidden: Chromium / Skia / OS font managers.
 */

#ifndef ROBO_FONT_DISCOVERY_H
#define ROBO_FONT_DISCOVERY_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontRegistry.h"
#include "RoboFont/RoboFontSfnt.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboFontDiscoveryMaxPath 4096u
#define RoboFontDiscoveryMaxFileSize RoboFontSfntMaxSourceSize
#define RoboFontDiscoveryMaxFilesPerScan 4096u
#define RoboFontDiscoveryMaxFacesPerCollection 256u
#define RoboFontDiscoveryMaxDirDepth 8u
#define RoboFontDiscoveryMinCandidateBytes 12u

typedef enum FontDiscoverySourceKind {
    RoboFontDiscoverySourceMemory = 1,
    RoboFontDiscoverySourceFile = 2,
    RoboFontDiscoverySourceDirectory = 3
} FontDiscoverySourceKind;

typedef enum FontCandidateFormat {
    RoboFontCandidateUnknown = 0,
    RoboFontCandidateTtf = 1, /* scaler 00010000 / 'true' / 'typ1' */
    RoboFontCandidateOtf = 2, /* 'OTTO' */
    RoboFontCandidateCollection = 3, /* 'ttcf' (.ttc / .otc) */
    RoboFontCandidateUnsupported = 4
} FontCandidateFormat;

typedef enum FontDiscoveryScanFlags {
    RoboFontDiscoveryScanNone = 0,
    /** Fail the whole scan on the first hard error (default: continue). */
    RoboFontDiscoveryScanFailFast = 1,
    /** Skip content-level duplicate detection (still dedupe source+index). */
    RoboFontDiscoveryScanAllowContentDup = 2
} FontDiscoveryScanFlags;

typedef struct FontDiscoveryResult {
    uint32_t filesSeen;
    uint32_t candidates;
    uint32_t registered;
    uint32_t skippedDuplicate;
    uint32_t skippedUnsupported;
    uint32_t skippedFilter;
    uint32_t failed;
    Result lastError; /* RoboOk if none */
} FontDiscoveryResult;

/**
 * Filesystem-facing IO for file/directory sources.
 * Guest core does not embed OS APIs — platform or tests supply ops.
 * Virtual IO is provided for in-memory trees.
 */
typedef struct FontDiscoveryIo {
    void *userdata;
    /**
     * Read entire file into a Malloc buffer (caller frees with RoboFree).
     * Cap at @p maxSize. Missing → RoboNotFound. Too large → RoboParseError.
     */
    Result (*RoboReadFile)(void *userdata,
                               const char *path,
                               size_t maxSize,
                               uint8_t **outBytes,
                               size_t *outSize);
    /**
     * Enumerate directory entries (non-recursive). Calls @p visit for each
     * name (basename only). visit returns non-zero to stop.
     */
    Result (*ListDir)(void *userdata,
                              const char *path,
                              int (*visit)(void *visitUd,
                                           const char *name,
                                           int IsDirectory),
                              void *visitUd);
    /** 1 if path exists and is a directory. */
    int (*IsDirectory)(void *userdata, const char *path);
    /** 1 if path exists as a regular file. */
    int (*isFile)(void *userdata, const char *path);
} FontDiscoveryIo;

void FontDiscoveryResultInit(FontDiscoveryResult *r);

/** Classify by SFNT signature (first bytes). */
FontCandidateFormat FontDiscoveryClassifyBytes(const uint8_t *data,
                                                               size_t size);

/** Extension filter: .ttf .otf .ttc .otc (ASCII case-insensitive). */
int FontDiscoveryExtensionIsFont(const char *pathOrName);

/** Stub IO: all ops return RoboNotSupported / 0. */
const FontDiscoveryIo *FontDiscoveryIoStub(void);

/* ---- Virtual in-memory tree (tests / bundled fonts) ---- */

typedef struct FontDiscoveryVirtualIo FontDiscoveryVirtualIo;

Result FontDiscoveryVirtualIoCreate(
    FontDiscoveryVirtualIo **out);
void FontDiscoveryVirtualIoDestroy(FontDiscoveryVirtualIo *vio);

/** Expose as discovery IO (owned by vio; do not free separately). */
const FontDiscoveryIo *FontDiscoveryVirtualIoOps(
    FontDiscoveryVirtualIo *vio);

/**
 * Mount a file at absolute-style UTF-8 path (e.g. "/Fonts/Probe.ttf").
 * Bytes are copied. Parent directories are created as needed.
 */
Result FontDiscoveryVirtualAddFile(
    FontDiscoveryVirtualIo *vio,
    const char *path,
    const void *bytes,
    size_t size);

/** Ensure a directory node exists. */
Result FontDiscoveryVirtualAddDir(
    FontDiscoveryVirtualIo *vio,
    const char *path);

/* ---- Registry binding ---- */

void FontRegistrySetIo(FontRegistry *reg,
                               const FontDiscoveryIo *io);

/**
 * Add an in-memory font blob. Copies bytes into a RoboFontSource.
 * @p label is a diagnostic path-like identity (copied, max path len).
 */
Result FontRegistryAddMemorySource(FontRegistry *reg,
                                                   const char *label,
                                                   const void *bytes,
                                                   size_t size,
                                                   FontDiscoverySourceId *outId);

/** Add a single file path (read via registry IO on scan). */
Result FontRegistryAddFileSource(FontRegistry *reg,
                                                 const char *path,
                                                 FontDiscoverySourceId *outId);

/**
 * Add a directory. @p recursive enables depth-limited recursion on scan.
 */
Result FontRegistryAddDirectorySource(
    FontRegistry *reg,
    const char *path,
    int recursive,
    FontDiscoverySourceId *outId);

Result FontRegistryRemoveDiscoverySource(
    FontRegistry *reg,
    FontDiscoverySourceId sourceId);

Result FontRegistryScanSource(FontRegistry *reg,
                                             FontDiscoverySourceId sourceId,
                                             uint32_t flags,
                                             FontDiscoveryResult *outResult);

Result FontRegistryScanAll(FontRegistry *reg,
                                          uint32_t flags,
                                          FontDiscoveryResult *outResult);

/**
 * Drop registrations for @p sourceId then scan again.
 */
Result FontRegistryRefreshSource(FontRegistry *reg,
                                                FontDiscoverySourceId sourceId,
                                                uint32_t flags,
                                                FontDiscoveryResult *outResult);

/** Fuzz entry: never crashes. */
Result FontDiscoveryFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_DISCOVERY_H */
