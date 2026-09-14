/*
 * RoboFont registry — registered faces from discovery (Goal 5.12).
 *
 * Distinct from selection (Goal 5.11): the registry stores candidates;
 * callers pass face pointers into robo_font_select.
 *
 * Identity:
 *   registration_id — stable for the lifetime of one registration entry
 *   face_id         — from RoboFontFace (stable while the face object lives)
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

#define ROBO_FONT_REGISTRY_MAX_REGISTRATIONS 8192u
#define ROBO_FONT_REGISTRY_MAX_SOURCES 256u

typedef uint64_t robo_font_registration_id_t;
typedef uint64_t robo_font_discovery_source_id_t;

typedef struct robo_font_registry robo_font_registry_t;

typedef struct robo_font_registration {
    robo_font_registration_id_t id;
    robo_font_discovery_source_id_t source_id;
    uint32_t face_index;
    uint8_t is_collection;
    uint8_t duplicate_of_existing; /* 1 if skipped as content duplicate */
    robo_font_face_id_t face_id;
    robo_font_face_t *face; /* borrowed; valid while registration lives */
    robo_font_style_desc_t style;
    /* Snapshot family UTF-8 (owned by registry entry; may be empty). */
    char family[128];
    char postscript_name[128];
    /**
     * Origin within the discovery source (file path or memory label).
     * With face_index, uniquely identifies a face under one source_id
     * (directories yield many files that all use face_index 0).
     */
    char origin[256];
} robo_font_registration_t;

robo_result_t robo_font_registry_create(robo_font_registry_t **out);
void robo_font_registry_destroy(robo_font_registry_t *reg);

uint32_t robo_font_registry_registration_count(const robo_font_registry_t *reg);
uint32_t robo_font_registry_source_count(const robo_font_registry_t *reg);

/**
 * Borrow registration at index [0, count). Pointer valid until next mutation.
 */
robo_result_t robo_font_registry_registration_at(const robo_font_registry_t *reg,
                                                 uint32_t index,
                                                 const robo_font_registration_t **out);

robo_result_t robo_font_registry_find_by_id(const robo_font_registry_t *reg,
                                            robo_font_registration_id_t id,
                                            const robo_font_registration_t **out);

robo_result_t robo_font_registry_find_by_face_id(const robo_font_registry_t *reg,
                                                 robo_font_face_id_t face_id,
                                                 const robo_font_registration_t **out);

/**
 * Collect borrowed face pointers for selection.
 * @p out_faces is malloc'd; caller robo_free's the array (not the faces).
 * Faces remain owned by the registry until remove/destroy.
 */
robo_result_t robo_font_registry_collect_faces(const robo_font_registry_t *reg,
                                               robo_font_face_t ***out_faces,
                                               uint32_t *out_count);

/**
 * Collect faces whose preferred/legacy/WWS/full/PS family casefold-matches
 * @p family (UTF-8). Same ownership rules as collect_faces.
 */
robo_result_t robo_font_registry_collect_faces_for_family(
    const robo_font_registry_t *reg,
    const char *family,
    robo_font_face_t ***out_faces,
    uint32_t *out_count);

robo_result_t robo_font_registry_remove_registration(robo_font_registry_t *reg,
                                                     robo_font_registration_id_t id);

/** Remove all registrations that came from @p source_id (source may remain). */
robo_result_t robo_font_registry_remove_source_registrations(
    robo_font_registry_t *reg,
    robo_font_discovery_source_id_t source_id);

void robo_font_registry_clear(robo_font_registry_t *reg);

/**
 * Optional invalidate hook (Goal 5.13 loader).
 * Fired when registrations are removed/cleared (source_id and/or reg_id).
 * @p source_id == 0 and @p registration_id == 0 means clear-all.
 * @p registration_id == 0 with non-zero source_id means all faces of that source.
 */
typedef void (*robo_font_registry_invalidate_fn)(
    void *userdata,
    robo_font_discovery_source_id_t source_id,
    robo_font_registration_id_t registration_id);

void robo_font_registry_set_invalidate_hook(robo_font_registry_t *reg,
                                            robo_font_registry_invalidate_fn fn,
                                            void *userdata);

uint64_t robo_font_registry_generation(const robo_font_registry_t *reg);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_REGISTRY_H */
