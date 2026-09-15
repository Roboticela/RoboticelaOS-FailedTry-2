/*
 * RoboHTML encoding boundary (Phase 8.1).
 * Labels + state transitions only; no decoding in this phase.
 */

#include "RoboHTML/RoboHTMLEncoding.h"

#include <stddef.h>
#include <string.h>

typedef struct robo_html_encoding_entry {
    robo_html_encoding_t id;
    const char *label;
} robo_html_encoding_entry_t;

/* Static label table. Aliases map to the same token. */
static const robo_html_encoding_entry_t g_encodings[] = {
    { ROBO_HTML_ENCODING_UTF_8, "UTF-8" },
    { ROBO_HTML_ENCODING_UTF_8, "utf8" },
    { ROBO_HTML_ENCODING_UTF_16LE, "UTF-16LE" },
    { ROBO_HTML_ENCODING_UTF_16BE, "UTF-16BE" },
    { ROBO_HTML_ENCODING_UTF_16, "UTF-16" },
    { ROBO_HTML_ENCODING_WINDOWS_1252, "windows-1252" },
    { ROBO_HTML_ENCODING_WINDOWS_1252, "cp1252" },
    { ROBO_HTML_ENCODING_SHIFT_JIS, "Shift_JIS" },
    { ROBO_HTML_ENCODING_UNKNOWN, NULL }
};

static int ascii_ci_equal(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int robo_html_encoding_is_known(robo_html_encoding_t enc)
{
    switch (enc) {
    case ROBO_HTML_ENCODING_UNKNOWN:
    case ROBO_HTML_ENCODING_UTF_8:
    case ROBO_HTML_ENCODING_UTF_16LE:
    case ROBO_HTML_ENCODING_UTF_16BE:
    case ROBO_HTML_ENCODING_UTF_16:
    case ROBO_HTML_ENCODING_WINDOWS_1252:
    case ROBO_HTML_ENCODING_SHIFT_JIS:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_encoding_label(robo_html_encoding_t enc)
{
    switch (enc) {
    case ROBO_HTML_ENCODING_UTF_8:
        return "UTF-8";
    case ROBO_HTML_ENCODING_UTF_16LE:
        return "UTF-16LE";
    case ROBO_HTML_ENCODING_UTF_16BE:
        return "UTF-16BE";
    case ROBO_HTML_ENCODING_UTF_16:
        return "UTF-16";
    case ROBO_HTML_ENCODING_WINDOWS_1252:
        return "windows-1252";
    case ROBO_HTML_ENCODING_SHIFT_JIS:
        return "Shift_JIS";
    case ROBO_HTML_ENCODING_UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

robo_html_encoding_t robo_html_encoding_from_label(const char *label)
{
    size_t i;

    if (!label) {
        return ROBO_HTML_ENCODING_UNKNOWN;
    }
    for (i = 0u; g_encodings[i].label != NULL; i++) {
        if (ascii_ci_equal(g_encodings[i].label, label)) {
            return g_encodings[i].id;
        }
    }
    return ROBO_HTML_ENCODING_UNKNOWN;
}

int robo_html_encoding_status_is_known(robo_html_encoding_status_t status)
{
    switch (status) {
    case ROBO_HTML_ENCODING_STATUS_OK:
    case ROBO_HTML_ENCODING_STATUS_UNSUPPORTED_LABEL:
    case ROBO_HTML_ENCODING_STATUS_INVALID_LABEL:
    case ROBO_HTML_ENCODING_STATUS_DECODE_MISMATCH:
    case ROBO_HTML_ENCODING_STATUS_MALFORMED:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_encoding_status_name(robo_html_encoding_status_t status)
{
    switch (status) {
    case ROBO_HTML_ENCODING_STATUS_OK:
        return "ok";
    case ROBO_HTML_ENCODING_STATUS_UNSUPPORTED_LABEL:
        return "unsupported-label";
    case ROBO_HTML_ENCODING_STATUS_INVALID_LABEL:
        return "invalid-label";
    case ROBO_HTML_ENCODING_STATUS_DECODE_MISMATCH:
        return "decode-mismatch";
    case ROBO_HTML_ENCODING_STATUS_MALFORMED:
        return "malformed";
    default:
        return "unknown";
    }
}

void robo_html_encoding_state_init(robo_html_encoding_state_t *st)
{
    if (!st) {
        return;
    }
    st->declared = ROBO_HTML_ENCODING_UNKNOWN;
    st->actual = ROBO_HTML_ENCODING_UNKNOWN;
    st->status = ROBO_HTML_ENCODING_STATUS_OK;
}

robo_result_t robo_html_encoding_declare(robo_html_encoding_state_t *st,
                                         const char *label,
                                         robo_html_diag_t *diag)
{
    robo_html_encoding_t id;
    size_t n;
    size_t i;

    if (!st) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ENCODING,
                              ROBO_INVALID_ARGUMENT, "state is NULL");
    }
    if (!label) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ENCODING,
                              ROBO_INVALID_ARGUMENT, "label is NULL");
    }
    n = strlen(label);
    if (n == 0u || n >= ROBO_HTML_ENCODING_LABEL_MAX) {
        st->status = ROBO_HTML_ENCODING_STATUS_INVALID_LABEL;
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ENCODING,
                              ROBO_PARSE_ERROR,
                              "encoding label empty or too long");
    }
    for (i = 0u; i < n; i++) {
        unsigned char c = (unsigned char)label[i];
        if (c < 0x20u || c >= 0x7Fu) {
            st->status = ROBO_HTML_ENCODING_STATUS_INVALID_LABEL;
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ENCODING,
                                  ROBO_PARSE_ERROR,
                                  "encoding label contains non-ASCII");
        }
    }
    id = robo_html_encoding_from_label(label);
    if (id == ROBO_HTML_ENCODING_UNKNOWN) {
        st->status = ROBO_HTML_ENCODING_STATUS_UNSUPPORTED_LABEL;
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ENCODING,
                              ROBO_NOT_SUPPORTED,
                              "encoding label not admitted by the runtime");
    }
    st->declared = id;
    /* Boundary decision for 8.1: UTF-8 is the runtime-native encoding. */
    if (st->actual == ROBO_HTML_ENCODING_UNKNOWN) {
        st->actual = ROBO_HTML_ENCODING_UTF_8;
    }
    st->status = ROBO_HTML_ENCODING_STATUS_OK;
    return ROBO_OK;
}
