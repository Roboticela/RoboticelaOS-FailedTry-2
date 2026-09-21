/*
 * RoboFont registry — registered faces from discovery (Goal 5.12).
 *
 * Distinct from selection (Goal 5.11): the registry stores candidates;
 * callers pass face pointers into FontSelect.
 *
 * Identity:
 *   registrationId — stable for the lifetime of one registration entry
 *   faceId         — from RoboFontFace (stable while the face object lives)
 * Neither is a persistent cross-process / cross-machine ID.
 *
 * Thread safety: mutations (add/scan/remove/clear/destroy) are single-owner.
 * Concurrent readers are safe only while no mutation runs (external sync).
 * Refcounts on faces are not atomic (same as Goal 5.1).
 */

#ifndef ROBO_FONT_REGISTRY_H
#define ROBO_FONT_REGISTRY_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontClassification.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboFontRegistryMaxRegistrations 8192u
#define RoboFontRegistryMaxSources 256u

typedef uint64_t FontRegistrationId;
typedef uint64_t FontDiscoverySourceId;

typedef struct FontRegistry FontRegistry;

typedef struct FontRegistration {
    FontRegistrationId id;
    FontDiscoverySourceId sourceId;
    uint32_t faceIndex;
    uint8_t isCollection;
    uint8_t duplicateOfExisting; /* 1 if skipped as content duplicate */
    FontFaceId faceId;
    FontFace *face; /* borrowed; valid while registration lives */
    FontStyleDesc style;
    /* Snapshot family UTF-8 (owned by registry entry; may be empty). */
    char family[128];
    char postscriptName[128];
    /**
     * Origin within the discovery source (file path or memory label).
     * With faceIndex, uniquely identifies a face under one sourceId
     * (directories yield many files that all use faceIndex 0).
     */
    char origin[256];
} FontRegistration;

Result FontRegistryCreate(FontRegistry **out);
void FontRegistryDestroy(FontRegistry *reg);

uint32_t FontRegistryRegistrationCount(const FontRegistry *reg);
uint32_t FontRegistrySourceCount(const FontRegistry *reg);

/**
 * Borrow registration at index [0, count). Pointer valid until next mutation.
 */
Result FontRegistryRegistrationAt(const FontRegistry *reg,
                                                 uint32_t index,
                                                 const FontRegistration **out);

Result FontRegistryFindById(const FontRegistry *reg,
                                            FontRegistrationId id,
                                            const FontRegistration **out);

Result FontRegistryFindByFaceId(const FontRegistry *reg,
                                                 FontFaceId faceId,
                                                 const FontRegistration **out);

/**
 * Collect borrowed face pointers for selection.
 * @p outFaces is malloc'd; caller RoboFree's the array (not the faces).
 * Faces remain owned by the registry until remove/destroy.
 */
Result FontRegistryCollectFaces(const FontRegistry *reg,
                                               FontFace ***outFaces,
                                               uint32_t *outCount);

/**
 * Collect faces whose preferred/legacy/WWS/full/PS family casefold-matches
 * @p family (UTF-8). Same ownership rules as collect_faces.
 */
Result FontRegistryCollectFacesForFamily(
    const FontRegistry *reg,
    const char *family,
    FontFace ***outFaces,
    uint32_t *outCount);

Result FontRegistryRemoveRegistration(FontRegistry *reg,
                                                     FontRegistrationId id);

/** Remove all registrations that came from @p sourceId (source may remain). */
Result FontRegistryRemoveSourceRegistrations(
    FontRegistry *reg,
    FontDiscoverySourceId sourceId);

void FontRegistryClear(FontRegistry *reg);

/**
 * Optional invalidate hook (Goal 5.13 loader).
 * Fired when registrations are removed/cleared (sourceId and/or reg_id).
 * @p sourceId == 0 and @p registrationId == 0 means clear-all.
 * @p registrationId == 0 with non-zero sourceId means all faces of that source.
 */
typedef void (*FontRegistryInvalidateFn)(
    void *userdata,
    FontDiscoverySourceId sourceId,
    FontRegistrationId registrationId);

void FontRegistrySetInvalidateHook(FontRegistry *reg,
                                            FontRegistryInvalidateFn fn,
                                            void *userdata);

uint64_t FontRegistryGeneration(const FontRegistry *reg);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_REGISTRY_H */
