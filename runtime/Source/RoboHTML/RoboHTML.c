/*
 * RoboHTML module lifecycle + version (Phase 8.1).
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLVersion.h"

#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>

static int g_initialized;
static robo_html_document_slot_t g_slots[ROBO_HTML_MAX_DOCUMENTS];

const char *robo_html_version_string(void)
{
    return ROBO_HTML_VERSION_STRING;
}

robo_result_t robo_html_get_version(robo_i32 *out_major,
                                    robo_i32 *out_minor,
                                    robo_i32 *out_patch)
{
    if (!out_major || !out_minor || !out_patch) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_major = ROBO_HTML_VERSION_MAJOR;
    *out_minor = ROBO_HTML_VERSION_MINOR;
    *out_patch = ROBO_HTML_VERSION_PATCH;
    return ROBO_OK;
}

robo_html_document_slot_t *robo_html_document_slot_find(
    robo_html_document_id_t id)
{
    size_t i;

    if (id == ROBO_HTML_DOCUMENT_ID_NONE) {
        return NULL;
    }
    for (i = 0u; i < ROBO_HTML_MAX_DOCUMENTS; i++) {
        if (g_slots[i].in_use && g_slots[i].id == id) {
            return &g_slots[i];
        }
    }
    return NULL;
}

robo_html_document_slot_t *robo_html_document_slot_alloc(void)
{
    size_t i;

    for (i = 0u; i < ROBO_HTML_MAX_DOCUMENTS; i++) {
        if (!g_slots[i].in_use) {
            return &g_slots[i];
        }
    }
    return NULL;
}

int robo_html_is_initialized(void)
{
    return g_initialized;
}

size_t robo_html_live_document_count(void)
{
    size_t i;
    size_t n = 0u;

    if (!g_initialized) {
        return 0u;
    }
    for (i = 0u; i < ROBO_HTML_MAX_DOCUMENTS; i++) {
        if (g_slots[i].in_use) {
            n++;
        }
    }
    return n;
}

robo_result_t robo_html_init(void)
{
    if (g_initialized) {
        return ROBO_ALREADY_EXISTS;
    }
    if (!robo_base_is_initialized()) {
        robo_log_error("robo_html: init refused; RoboBase not initialized");
        return ROBO_INIT_ERROR;
    }
    {
        size_t i;
        for (i = 0u; i < ROBO_HTML_MAX_DOCUMENTS; i++) {
            g_slots[i].in_use = 0;
            g_slots[i].id = ROBO_HTML_DOCUMENT_ID_NONE;
        }
    }
    g_initialized = 1;
    robo_log_info("robo_html: init %s", ROBO_HTML_VERSION_STRING);
    return ROBO_OK;
}

void robo_html_shutdown(void)
{
    size_t i;

    if (!g_initialized) {
        return;
    }
    /* Deterministic teardown: destroy live documents oldest-first. */
    for (i = 0u; i < ROBO_HTML_MAX_DOCUMENTS; i++) {
        if (g_slots[i].in_use) {
            (void)robo_html_document_destroy(g_slots[i].id, NULL);
        }
    }
    g_initialized = 0;
    robo_log_info("robo_html: shutdown");
}
