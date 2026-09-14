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

#define ROBO_FONT_DISCOVERY_MAX_PATH 4096u
#define ROBO_FONT_DISCOVERY_MAX_FILE_SIZE ROBO_FONT_SFNT_MAX_SOURCE_SIZE
#define ROBO_FONT_DISCOVERY_MAX_FILES_PER_SCAN 4096u
#define ROBO_FONT_DISCOVERY_MAX_FACES_PER_COLLECTION 256u
#define ROBO_FONT_DISCOVERY_MAX_DIR_DEPTH 8u
#define ROBO_FONT_DISCOVERY_MIN_CANDIDATE_BYTES 12u

typedef enum robo_font_discovery_source_kind {
    ROBO_FONT_DISCOVERY_SOURCE_MEMORY = 1,
    ROBO_FONT_DISCOVERY_SOURCE_FILE = 2,
    ROBO_FONT_DISCOVERY_SOURCE_DIRECTORY = 3
} robo_font_discovery_source_kind_t;

typedef enum robo_font_candidate_format {
    ROBO_FONT_CANDIDATE_UNKNOWN = 0,
    ROBO_FONT_CANDIDATE_TTF = 1, /* scaler 00010000 / 'true' / 'typ1' */
    ROBO_FONT_CANDIDATE_OTF = 2, /* 'OTTO' */
    ROBO_FONT_CANDIDATE_COLLECTION = 3, /* 'ttcf' (.ttc / .otc) */
    ROBO_FONT_CANDIDATE_UNSUPPORTED = 4
} robo_font_candidate_format_t;

typedef enum robo_font_discovery_scan_flags {
    ROBO_FONT_DISCOVERY_SCAN_NONE = 0,
    /** Fail the whole scan on the first hard error (default: continue). */
    ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST = 1,
    /** Skip content-level duplicate detection (still dedupe source+index). */
    ROBO_FONT_DISCOVERY_SCAN_ALLOW_CONTENT_DUP = 2
} robo_font_discovery_scan_flags_t;

typedef struct robo_font_discovery_result {
    uint32_t files_seen;
    uint32_t candidates;
    uint32_t registered;
    uint32_t skipped_duplicate;
    uint32_t skipped_unsupported;
    uint32_t skipped_filter;
    uint32_t failed;
    robo_result_t last_error; /* ROBO_OK if none */
} robo_font_discovery_result_t;

/**
 * Filesystem-facing IO for file/directory sources.
 * Guest core does not embed OS APIs — platform or tests supply ops.
 * Virtual IO is provided for in-memory trees.
 */
typedef struct robo_font_discovery_io {
    void *userdata;
    /**
     * Read entire file into a robo_malloc buffer (caller frees with robo_free).
     * Cap at @p max_size. Missing → ROBO_NOT_FOUND. Too large → ROBO_PARSE_ERROR.
     */
    robo_result_t (*read_file)(void *userdata,
                               const char *path,
                               size_t max_size,
                               uint8_t **out_bytes,
                               size_t *out_size);
    /**
     * Enumerate directory entries (non-recursive). Calls @p visit for each
     * name (basename only). visit returns non-zero to stop.
     */
    robo_result_t (*list_dir)(void *userdata,
                              const char *path,
                              int (*visit)(void *visit_ud,
                                           const char *name,
                                           int is_directory),
                              void *visit_ud);
    /** 1 if path exists and is a directory. */
    int (*is_directory)(void *userdata, const char *path);
    /** 1 if path exists as a regular file. */
    int (*is_file)(void *userdata, const char *path);
} robo_font_discovery_io_t;

void robo_font_discovery_result_init(robo_font_discovery_result_t *r);

/** Classify by SFNT signature (first bytes). */
robo_font_candidate_format_t robo_font_discovery_classify_bytes(const uint8_t *data,
                                                               size_t size);

/** Extension filter: .ttf .otf .ttc .otc (ASCII case-insensitive). */
int robo_font_discovery_extension_is_font(const char *path_or_name);

/** Stub IO: all ops return ROBO_NOT_SUPPORTED / 0. */
const robo_font_discovery_io_t *robo_font_discovery_io_stub(void);

/* ---- Virtual in-memory tree (tests / bundled fonts) ---- */

typedef struct robo_font_discovery_virtual_io robo_font_discovery_virtual_io_t;

robo_result_t robo_font_discovery_virtual_io_create(
    robo_font_discovery_virtual_io_t **out);
void robo_font_discovery_virtual_io_destroy(robo_font_discovery_virtual_io_t *vio);

/** Expose as discovery IO (owned by vio; do not free separately). */
const robo_font_discovery_io_t *robo_font_discovery_virtual_io_ops(
    robo_font_discovery_virtual_io_t *vio);

/**
 * Mount a file at absolute-style UTF-8 path (e.g. "/Fonts/Probe.ttf").
 * Bytes are copied. Parent directories are created as needed.
 */
robo_result_t robo_font_discovery_virtual_add_file(
    robo_font_discovery_virtual_io_t *vio,
    const char *path,
    const void *bytes,
    size_t size);

/** Ensure a directory node exists. */
robo_result_t robo_font_discovery_virtual_add_dir(
    robo_font_discovery_virtual_io_t *vio,
    const char *path);

/* ---- Registry binding ---- */

void robo_font_registry_set_io(robo_font_registry_t *reg,
                               const robo_font_discovery_io_t *io);

/**
 * Add an in-memory font blob. Copies bytes into a RoboFontSource.
 * @p label is a diagnostic path-like identity (copied, max path len).
 */
robo_result_t robo_font_registry_add_memory_source(robo_font_registry_t *reg,
                                                   const char *label,
                                                   const void *bytes,
                                                   size_t size,
                                                   robo_font_discovery_source_id_t *out_id);

/** Add a single file path (read via registry IO on scan). */
robo_result_t robo_font_registry_add_file_source(robo_font_registry_t *reg,
                                                 const char *path,
                                                 robo_font_discovery_source_id_t *out_id);

/**
 * Add a directory. @p recursive enables depth-limited recursion on scan.
 */
robo_result_t robo_font_registry_add_directory_source(
    robo_font_registry_t *reg,
    const char *path,
    int recursive,
    robo_font_discovery_source_id_t *out_id);

robo_result_t robo_font_registry_remove_discovery_source(
    robo_font_registry_t *reg,
    robo_font_discovery_source_id_t source_id);

robo_result_t robo_font_registry_scan_source(robo_font_registry_t *reg,
                                             robo_font_discovery_source_id_t source_id,
                                             uint32_t flags,
                                             robo_font_discovery_result_t *out_result);

robo_result_t robo_font_registry_scan_all(robo_font_registry_t *reg,
                                          uint32_t flags,
                                          robo_font_discovery_result_t *out_result);

/**
 * Drop registrations for @p source_id then scan again.
 */
robo_result_t robo_font_registry_refresh_source(robo_font_registry_t *reg,
                                                robo_font_discovery_source_id_t source_id,
                                                uint32_t flags,
                                                robo_font_discovery_result_t *out_result);

/** Fuzz entry: never crashes. */
robo_result_t robo_font_discovery_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_DISCOVERY_H */
