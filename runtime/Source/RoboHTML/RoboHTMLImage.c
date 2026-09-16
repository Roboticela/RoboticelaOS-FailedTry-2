/*
 * RoboHTML Phase 8.12 — image resources.
 *
 * Three layers, each with one job:
 *
 *   attributes   read `src` / `alt` / `width` / `height` off an element and
 *                parse the dimensions safely. No fetching, no decoding.
 *   resource     robo_html_image_t — a state plus, when there is one, a
 *                decoded RoboGraphics image. Failure is a *state*, not an
 *                exception, which is what makes a bad `src` unable to break
 *                a document: every outcome leaves the DOM exactly as it was.
 *   attachment   a per-document table keyed by node identity, so an element
 *                can be asked for its picture later — by a renderer that did
 *                not load the document, and without either side holding a
 *                pointer into the other's memory.
 *
 * The fetch seam is the one place bytes come from, and RoboHTML never
 * provides those bytes itself: a resolver hands them over for the duration of
 * one call and ownership never crosses. The shipped resolver is an in-memory
 * asset table, which is why this phase needs no filesystem, no capability
 * check and no traversal defence — there is nothing to traverse.
 */

#include "RoboHTML/RoboHTMLImage.h"

#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"

#include "RoboBase/RoboLog.h"
#include "RoboGraphics/RoboGraphicsDecode.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* State vocabulary                                                    */
/* ------------------------------------------------------------------ */

int robo_html_image_state_is_known(robo_html_image_state_t state)
{
    switch (state) {
    case ROBO_HTML_IMAGE_STATE_NONE:
    case ROBO_HTML_IMAGE_STATE_PENDING:
    case ROBO_HTML_IMAGE_STATE_DECODED:
    case ROBO_HTML_IMAGE_STATE_FAILED:
    case ROBO_HTML_IMAGE_STATE_UNSUPPORTED:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_image_state_name(robo_html_image_state_t state)
{
    switch (state) {
    case ROBO_HTML_IMAGE_STATE_NONE:
        return "none";
    case ROBO_HTML_IMAGE_STATE_PENDING:
        return "pending";
    case ROBO_HTML_IMAGE_STATE_DECODED:
        return "decoded";
    case ROBO_HTML_IMAGE_STATE_FAILED:
        return "failed";
    case ROBO_HTML_IMAGE_STATE_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Dimension parsing                                                   */
/* ------------------------------------------------------------------ */

static int ascii_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int ascii_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

robo_result_t robo_html_image_parse_dimension(const char *value, size_t length,
                                              int *out_valid,
                                              uint32_t *out_value)
{
    size_t start = 0u;
    size_t end = length;
    uint64_t acc = 0u;
    size_t i;

    if (!out_valid || !out_value) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_valid = 0;
    *out_value = 0u;
    if (!value) {
        /* Absent: valid as "no declared dimension". */
        return ROBO_OK;
    }
    while (start < end && ascii_is_space(value[start])) {
        start++;
    }
    while (end > start && ascii_is_space(value[end - 1u])) {
        end--;
    }
    if (start >= end) {
        /* Present but empty: there is a value and it is not a dimension.
         * Reported as invalid rather than as absent, so `<img width="">`
         * cannot be mistaken for `<img>`. */
        return ROBO_OK;
    }
    for (i = start; i < end; i++) {
        if (!ascii_is_digit(value[i])) {
            /* Rejects "50%", "50px", "-5", "1e3", "0x10" — all of which are
             * either layout's business or simply not dimensions. */
            return ROBO_OK;
        }
        acc = acc * 10u + (uint64_t)(value[i] - '0');
        if (acc > (uint64_t)ROBO_HTML_IMAGE_MAX_DECLARED_DIMENSION) {
            /* Out of range: refuse rather than clamp, so a typo cannot turn
             * into a silent resize. */
            return ROBO_OK;
        }
    }
    *out_valid = 1;
    *out_value = (uint32_t)acc;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Attribute view                                                      */
/* ------------------------------------------------------------------ */

/** Borrowed value of @p name, plus whether the attribute exists at all. */
static const char *attribute_of(const robo_html_node_handle_t *element,
                                const char *name, size_t *out_length,
                                int *out_present)
{
    const char *value = robo_html_element_attribute_value(element, name,
                                                          out_length);
    if (out_present) {
        *out_present = (value != NULL);
    }
    if (!value && out_length) {
        *out_length = 0u;
    }
    return value;
}

/** Length of @p bytes after trimming surrounding ASCII whitespace. */
static size_t trimmed_length(const char *bytes, size_t length)
{
    size_t start = 0u;
    size_t end = length;

    if (!bytes) {
        return 0u;
    }
    while (start < end && ascii_is_space(bytes[start])) {
        start++;
    }
    while (end > start && ascii_is_space(bytes[end - 1u])) {
        end--;
    }
    return end - start;
}

robo_result_t robo_html_element_image_attributes(
    const robo_html_node_handle_t *element, robo_html_image_attributes_t *out,
    robo_html_diag_t *diag)
{
    size_t length = 0u;
    int present = 0;

    if (!element || !out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_INVALID_ARGUMENT, "null argument");
    }
    memset(out, 0, sizeof(*out));
    if (robo_html_element_tag(element) != ROBO_HTML_TAG_IMG) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_NOT_SUPPORTED,
                              "element is not an img");
    }

    out->source = attribute_of(element, "src", &length, &present);
    out->source_length = present ? length : 0u;
    out->source_present = present;
    out->source_usable =
        (present && trimmed_length(out->source, out->source_length) > 0u) ? 1 : 0;

    out->alt = attribute_of(element, "alt", &length, &present);
    out->alt_length = present ? length : 0u;

    out->width_set = 0;
    out->width_valid = 0;
    out->width = 0u;
    out->height_set = 0;
    out->height_valid = 0;
    out->height = 0u;
    {
        const char *value = attribute_of(element, "width", &length, &present);
        int valid = 0;
        uint32_t parsed = 0u;

        if (present) {
            out->width_set = 1;
            (void)robo_html_image_parse_dimension(value, length, &valid, &parsed);
            out->width_valid = valid;
            out->width = parsed;
        }
    }
    {
        const char *value = attribute_of(element, "height", &length, &present);
        int valid = 0;
        uint32_t parsed = 0u;

        if (present) {
            out->height_set = 1;
            (void)robo_html_image_parse_dimension(value, length, &valid,
                                                  &parsed);
            out->height_valid = valid;
            out->height = parsed;
        }
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Resource object                                                     */
/* ------------------------------------------------------------------ */

struct robo_html_image {
    uint32_t refcount;
    robo_html_image_state_t state;
    /** The code behind FAILED/UNSUPPORTED; ROBO_OK otherwise. */
    robo_result_t failure;
    /** Owned copy of the reference, or NULL. Not NUL-terminated by contract. */
    char *source;
    size_t source_length;
    /** Owned decoded pixels, or NULL unless the state is DECODED. */
    robo_gfx_image_t *graphics;
};

static char *dup_bytes(const char *bytes, size_t length)
{
    char *copy;

    if (!bytes || length == 0u) {
        return NULL;
    }
    copy = (char *)robo_malloc(length);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, bytes, length);
    return copy;
}

static robo_html_image_t *image_new(robo_html_image_state_t state,
                                    robo_result_t failure,
                                    const char *source_reference)
{
    robo_html_image_t *image =
        (robo_html_image_t *)robo_calloc(1u, sizeof(robo_html_image_t));

    if (!image) {
        return NULL;
    }
    image->refcount = 1u;
    image->state = state;
    image->failure = failure;
    if (source_reference) {
        image->source_length = strlen(source_reference);
        image->source = dup_bytes(source_reference, image->source_length);
        if (!image->source) {
            image->source_length = 0u; /* recorded name is optional */
        }
    }
    return image;
}

/**
 * Map a decode attempt to a state. The one distinction that matters:
 * "these bytes are not an image this runtime implements" is UNSUPPORTED,
 * while "these bytes claim to be an image and are wrong" is FAILED.
 */
static robo_html_image_state_t state_for_result(robo_result_t result)
{
    if (result == ROBO_OK) {
        return ROBO_HTML_IMAGE_STATE_DECODED;
    }
    if (result == ROBO_NOT_SUPPORTED) {
        return ROBO_HTML_IMAGE_STATE_UNSUPPORTED;
    }
    return ROBO_HTML_IMAGE_STATE_FAILED;
}

robo_result_t robo_html_image_create_decoded(const void *bytes, size_t length,
                                            const char *source_reference,
                                            robo_html_image_t **out,
                                            robo_html_diag_t *diag)
{
    robo_html_image_t *image;
    robo_result_t decode = ROBO_ERROR;

    if (!out || (!bytes && length != 0u)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_INVALID_ARGUMENT, "null bytes or out");
    }
    *out = NULL;
    if (bytes && length > 0u) {
        decode = robo_gfx_image_decode(bytes, length, NULL, NULL);
    }
    image = image_new(state_for_result(decode), decode, source_reference);
    if (!image) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_OUT_OF_MEMORY,
                              "image allocation failed");
    }
    if (decode == ROBO_OK) {
        robo_result_t res = robo_gfx_image_decode(bytes, length, NULL,
                                                 &image->graphics);
        if (res != ROBO_OK) {
            /* The probe said these bytes decode and the decode disagreed;
             * record the failure rather than claiming pixels we do not have. */
            image->state = state_for_result(res);
            image->failure = res;
        }
    }
    *out = image;
    return ROBO_OK;
}

void robo_html_image_destroy(robo_html_image_t *image)
{
    if (!image) {
        return;
    }
    if (image->graphics) {
        robo_gfx_image_release(image->graphics);
    }
    if (image->source) {
        robo_free(image->source);
    }
    robo_free(image);
}

void robo_html_image_retain(robo_html_image_t *image)
{
    if (image) {
        image->refcount++;
    }
}

void robo_html_image_release(robo_html_image_t *image)
{
    if (!image || image->refcount == 0u) {
        return;
    }
    image->refcount--;
    if (image->refcount == 0u) {
        robo_html_image_destroy(image);
    }
}

robo_html_image_state_t robo_html_image_state(const robo_html_image_t *image)
{
    return image ? image->state : ROBO_HTML_IMAGE_STATE_NONE;
}

const char *robo_html_image_source(const robo_html_image_t *image,
                                   size_t *out_length)
{
    if (out_length) {
        *out_length = image ? image->source_length : 0u;
    }
    return image ? image->source : NULL;
}

robo_gfx_image_t *robo_html_image_graphics(const robo_html_image_t *image)
{
    if (!image || image->state != ROBO_HTML_IMAGE_STATE_DECODED) {
        return NULL;
    }
    return image->graphics;
}

int robo_html_image_intrinsic_size(const robo_html_image_t *image,
                                   uint32_t *out_width, uint32_t *out_height)
{
    robo_gfx_image_t *graphics;

    if (!image || !out_width || !out_height) {
        return 0;
    }
    graphics = robo_html_image_graphics(image);
    if (!graphics) {
        return 0;
    }
    *out_width = robo_gfx_image_width(graphics);
    *out_height = robo_gfx_image_height(graphics);
    return 1;
}

robo_result_t robo_html_image_failure(const robo_html_image_t *image)
{
    if (!image) {
        return ROBO_INVALID_ARGUMENT;
    }
    return image->failure;
}

/* ------------------------------------------------------------------ */
/* Debug description                                                   */
/* ------------------------------------------------------------------ */

typedef struct text_builder {
    char *out;
    size_t capacity;
    size_t used;
} text_builder_t;

static void append_text(text_builder_t *b, const char *text)
{
    if (!b->out || b->capacity == 0u) {
        b->used += strlen(text);
        return;
    }
    while (*text != '\0') {
        if (b->used + 1u < b->capacity) {
            b->out[b->used] = *text;
        }
        b->used++;
        text++;
    }
    b->out[(b->used < b->capacity) ? b->used : b->capacity - 1u] = '\0';
}

static void append_bytes(text_builder_t *b, const char *bytes, size_t length)
{
    size_t i;

    for (i = 0u; i < length; i++) {
        char one[2];

        one[0] = bytes[i];
        one[1] = '\0';
        append_text(b, one);
    }
}

static void append_uint(text_builder_t *b, uint32_t value)
{
    char digits[16];
    int at = 0;

    if (value == 0u) {
        append_text(b, "0");
        return;
    }
    while (value > 0u && at < (int)sizeof(digits)) {
        digits[at++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (at > 0) {
        char one[2];

        one[0] = digits[--at];
        one[1] = '\0';
        append_text(b, one);
    }
}

size_t robo_html_image_describe(const robo_html_image_t *image, char *out,
                                size_t capacity)
{
    text_builder_t b;

    b.out = out;
    b.capacity = capacity;
    b.used = 0u;
    if (image && image->source) {
        append_text(&b, "img src=\"");
        append_bytes(&b, image->source, image->source_length);
        append_text(&b, "\" ");
    } else {
        append_text(&b, "img ");
    }
    append_text(&b, "state=");
    append_text(&b, robo_html_image_state_name(robo_html_image_state(image)));
    if (image && image->state == ROBO_HTML_IMAGE_STATE_DECODED) {
        robo_gfx_image_t *graphics = image->graphics;

        append_text(&b, " intrinsic=");
        append_uint(&b, robo_gfx_image_width(graphics));
        append_text(&b, "x");
        append_uint(&b, robo_gfx_image_height(graphics));
    } else if (image && image->failure != ROBO_OK) {
        append_text(&b, " reason=");
        append_uint(&b, (uint32_t)image->failure);
    }
    if (out && capacity > 0u) {
        out[(b.used < capacity) ? b.used : capacity - 1u] = '\0';
    }
    return b.used;
}

/* ------------------------------------------------------------------ */
/* Fetch seam + probes                                                 */
/* ------------------------------------------------------------------ */

void robo_html_image_resolver_init(robo_html_image_resolver_t *resolver)
{
    if (!resolver) {
        return;
    }
    resolver->fetch = NULL;
    resolver->userdata = NULL;
}

int robo_html_image_resolver_is_usable(const robo_html_image_resolver_t *resolver)
{
    return (resolver && resolver->fetch) ? 1 : 0;
}

robo_result_t robo_html_image_bytes_state(const void *bytes, size_t length,
                                          robo_html_image_state_t *out_state)
{
    robo_gfx_decode_info_t info;
    robo_result_t res;

    if (!bytes || !out_state) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_state = ROBO_HTML_IMAGE_STATE_FAILED;
    res = robo_gfx_image_decode_info(bytes, length, NULL, &info);
    if (res == ROBO_OK) {
        *out_state = ROBO_HTML_IMAGE_STATE_DECODED;
    } else if (res == ROBO_NOT_SUPPORTED) {
        *out_state = ROBO_HTML_IMAGE_STATE_UNSUPPORTED;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Local asset resolver                                                */
/* ------------------------------------------------------------------ */

typedef struct asset_entry {
    char *name;
    size_t name_length;
    const uint8_t *bytes;
    size_t length;
} asset_entry_t;

struct robo_html_image_assets {
    asset_entry_t *entries;
    size_t count;
    size_t capacity;
};

static void asset_entry_clear(asset_entry_t *entry)
{
    if (entry->name) {
        robo_free(entry->name);
    }
    memset(entry, 0, sizeof(*entry));
}

static int asset_find(const robo_html_image_assets_t *assets, const char *name,
                      size_t length)
{
    size_t i;

    for (i = 0u; i < assets->count; i++) {
        if (assets->entries[i].name_length == length &&
            memcmp(assets->entries[i].name, name, length) == 0) {
            return (int)i;
        }
    }
    return -1;
}

robo_result_t robo_html_image_assets_add(robo_html_image_assets_t *assets,
                                         const char *name, const void *bytes,
                                         size_t length, robo_html_diag_t *diag)
{
    size_t name_length;
    asset_entry_t entry;

    if (!assets || !name) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_INVALID_ARGUMENT, "null asset argument");
    }
    name_length = strlen(name);
    if (name_length == 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_INVALID_ARGUMENT, "empty asset name");
    }
    if (!bytes && length != 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_INVALID_ARGUMENT, "null asset bytes");
    }
    if (asset_find(assets, name, name_length) >= 0) {
        /* Registering the same name twice is almost always a real mistake,
         * and silently shadowing would make a missing image look present. */
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_ALREADY_EXISTS, "duplicate asset name");
    }
    if (assets->count == assets->capacity) {
        size_t next = assets->capacity ? assets->capacity * 2u : 8u;
        asset_entry_t *grown = (asset_entry_t *)robo_realloc(
            assets->entries, next * sizeof(asset_entry_t));

        if (!grown) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                                  ROBO_OUT_OF_MEMORY, "asset table growth");
        }
        assets->entries = grown;
        assets->capacity = next;
    }
    memset(&entry, 0, sizeof(entry));
    entry.name = (char *)robo_malloc(name_length + 1u);
    if (!entry.name) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_OUT_OF_MEMORY, "asset name copy");
    }
    memcpy(entry.name, name, name_length + 1u);
    entry.name_length = name_length;
    entry.bytes = (const uint8_t *)bytes;
    entry.length = length;
    assets->entries[assets->count++] = entry;
    return ROBO_OK;
}

robo_result_t robo_html_image_assets_create(
    const robo_html_image_asset_t *entries, size_t count,
    robo_html_image_assets_t **out)
{
    robo_html_image_assets_t *assets;
    size_t i;

    if (!out || (!entries && count != 0u)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    assets = (robo_html_image_assets_t *)robo_calloc(
        1u, sizeof(robo_html_image_assets_t));
    if (!assets) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < count; i++) {
        robo_result_t res = robo_html_image_assets_add(
            assets, entries[i].name, entries[i].bytes, entries[i].length, NULL);

        if (res != ROBO_OK) {
            robo_html_image_assets_destroy(assets);
            return res;
        }
    }
    *out = assets;
    return ROBO_OK;
}

const uint8_t *robo_html_image_assets_lookup(
    const robo_html_image_assets_t *assets, const char *name,
    size_t *out_length)
{
    int index;

    if (out_length) {
        *out_length = 0u;
    }
    if (!assets || !name) {
        return NULL;
    }
    index = asset_find(assets, name, strlen(name));
    if (index < 0) {
        return NULL;
    }
    if (out_length) {
        *out_length = assets->entries[index].length;
    }
    return assets->entries[index].bytes;
}

size_t robo_html_image_assets_count(const robo_html_image_assets_t *assets)
{
    return assets ? assets->count : 0u;
}

void robo_html_image_assets_destroy(robo_html_image_assets_t *assets)
{
    size_t i;

    if (!assets) {
        return;
    }
    for (i = 0u; i < assets->count; i++) {
        asset_entry_clear(&assets->entries[i]);
    }
    if (assets->entries) {
        robo_free(assets->entries);
    }
    robo_free(assets);
}

/*
 * A scheme is `ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"` at the very
 * start. Recognising it is the whole policy: anything with a scheme names
 * something this runtime will not fetch (no network, no files, no data URI),
 * and saying so is more useful than "not found", which would blame the
 * document for a limitation of the runtime.
 */
static int reference_has_scheme(const uint8_t *reference, size_t length)
{
    size_t i = 0u;

    if (length == 0u) {
        return 0;
    }
    if (!((reference[0] >= 'a' && reference[0] <= 'z') ||
          (reference[0] >= 'A' && reference[0] <= 'Z'))) {
        return 0;
    }
    for (i = 1u; i < length; i++) {
        uint8_t c = reference[i];

        if (c == ':') {
            return 1;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
            continue;
        }
        return 0; /* the colon, if any, came after a non-scheme character */
    }
    return 0;
}

static robo_result_t assets_fetch(void *userdata,
                                  const robo_html_image_request_t *request,
                                  const uint8_t **out_bytes,
                                  size_t *out_length, robo_html_diag_t *diag)
{
    const robo_html_image_assets_t *assets =
        (const robo_html_image_assets_t *)userdata;
    const asset_entry_t *found = NULL;
    size_t i;

    if (!assets || !request || !out_bytes || !out_length) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_INVALID_ARGUMENT, "null fetch argument");
    }
    *out_bytes = NULL;
    *out_length = 0u;
    if (request->reference_length == 0u || !request->reference) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_NOT_FOUND,
                              "empty reference");
    }
    if (reference_has_scheme((const uint8_t *)request->reference,
                             request->reference_length)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_NOT_SUPPORTED,
                              "scheme references are not fetched");
    }
    /*
     * Exact byte-for-byte match against the registered names. This is the
     * security property, not a shortcut: because nothing is ever opened,
     * "..", an absolute path and a backslash are all just names that are not
     * registered. There is no traversal surface to defend.
     */
    for (i = 0u; i < assets->count; i++) {
        if (assets->entries[i].name_length == request->reference_length &&
            memcmp(assets->entries[i].name, request->reference,
                   request->reference_length) == 0) {
            found = &assets->entries[i];
            break;
        }
    }
    if (!found) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_NOT_FOUND,
                              "asset not registered");
    }
    *out_bytes = found->bytes;
    *out_length = found->length;
    return ROBO_OK;
}

robo_html_image_resolver_t robo_html_image_assets_resolver(
    const robo_html_image_assets_t *assets)
{
    robo_html_image_resolver_t resolver;

    resolver.fetch = assets_fetch;
    resolver.userdata = (void *)assets;
    return resolver;
}

/* ------------------------------------------------------------------ */
/* Document attachment table                                           */
/* ------------------------------------------------------------------ */

static int handle_same_node(const robo_html_node_handle_t *a,
                            const robo_html_node_handle_t *b)
{
    return a->document_id == b->document_id && a->node_id == b->node_id &&
           a->generation == b->generation;
}

static robo_html_image_attachment_t *attachment_find(
    robo_html_document_slot_t *slot, const robo_html_node_handle_t *node)
{
    size_t i;

    for (i = 0u; i < slot->image_count; i++) {
        if (handle_same_node(&slot->images[i].node, node)) {
            return &slot->images[i];
        }
    }
    return NULL;
}

/** Release and remove entry @p index, compacting the table. */
static void attachment_remove(robo_html_document_slot_t *slot, size_t index)
{
    if (slot->images[index].image) {
        robo_html_image_release(slot->images[index].image);
    }
    if (index + 1u < slot->image_count) {
        memmove(&slot->images[index], &slot->images[index + 1u],
                (slot->image_count - index - 1u) * sizeof(slot->images[0]));
    }
    slot->image_count--;
}

static robo_result_t attachment_store(robo_html_document_slot_t *slot,
                                      const robo_html_node_handle_t *node,
                                      robo_html_image_t *image,
                                      robo_html_diag_t *diag)
{
    robo_html_image_attachment_t *existing = attachment_find(slot, node);

    if (existing) {
        if (existing->image != image) {
            if (existing->image) {
                robo_html_image_release(existing->image);
            }
            existing->image = image;
        }
        return ROBO_OK;
    }
    if (slot->image_count == slot->image_capacity) {
        size_t next = slot->image_capacity ? slot->image_capacity * 2u : 4u;
        robo_html_image_attachment_t *grown =
            (robo_html_image_attachment_t *)robo_realloc(
                slot->images, next * sizeof(slot->images[0]));

        if (!grown) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                                  ROBO_OUT_OF_MEMORY, "image table growth");
        }
        slot->images = grown;
        slot->image_capacity = next;
    }
    slot->images[slot->image_count].node = *node;
    slot->images[slot->image_count].image = image;
    slot->image_count++;
    return ROBO_OK;
}

void robo_html_document_images_free(robo_html_document_slot_t *slot)
{
    size_t i;

    if (!slot) {
        return;
    }
    for (i = 0u; i < slot->image_count; i++) {
        if (slot->images[i].image) {
            robo_html_image_release(slot->images[i].image);
        }
    }
    if (slot->images) {
        robo_free(slot->images);
    }
    slot->images = NULL;
    slot->image_count = 0u;
    slot->image_capacity = 0u;
}

size_t robo_html_document_image_count(robo_html_document_id_t document_id)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    return slot ? slot->image_count : 0u;
}

/* ------------------------------------------------------------------ */
/* Element-side API                                                    */
/* ------------------------------------------------------------------ */

/** Shared argument/eligibility gate for every element-side entry point. */
static robo_result_t image_element_gate(const robo_html_node_handle_t *element,
                                        robo_html_diag_t *diag)
{
    if (!element) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_INVALID_ARGUMENT, "null element");
    }
    if (!robo_html_is_initialized()) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_INIT_ERROR,
                              "RoboHTML module not initialized");
    }
    if (robo_html_element_tag(element) != ROBO_HTML_TAG_IMG) {
        if (!robo_html_element_has_attribute(element, "src") &&
            robo_html_element_tag(element) == ROBO_HTML_TAG_UNKNOWN) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                                  ROBO_NOT_FOUND, "element does not resolve");
        }
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_NOT_SUPPORTED, "element is not an img");
    }
    return ROBO_OK;
}

robo_html_image_state_t robo_html_element_image_state(
    const robo_html_node_handle_t *element)
{
    robo_html_document_slot_t *slot;
    robo_html_image_attachment_t *attachment;
    robo_html_image_attributes_t attributes;

    if (!element || !robo_html_is_initialized() ||
        robo_html_element_tag(element) != ROBO_HTML_TAG_IMG) {
        return ROBO_HTML_IMAGE_STATE_NONE;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (slot) {
        attachment = attachment_find(slot, element);
        if (attachment && attachment->image) {
            return robo_html_image_state(attachment->image);
        }
    }
    if (robo_html_element_image_attributes(element, &attributes, NULL) !=
        ROBO_OK) {
        return ROBO_HTML_IMAGE_STATE_NONE;
    }
    return attributes.source_usable ? ROBO_HTML_IMAGE_STATE_PENDING
                                    : ROBO_HTML_IMAGE_STATE_NONE;
}

robo_result_t robo_html_element_image_get(const robo_html_node_handle_t *element,
                                          robo_html_image_t **out,
                                          robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_image_attachment_t *attachment;
    robo_result_t res;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_INVALID_ARGUMENT, "null out");
    }
    *out = NULL;
    res = image_element_gate(element, diag);
    if (res != ROBO_OK) {
        return res;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_NOT_FOUND,
                              "document does not resolve");
    }
    attachment = attachment_find(slot, element);
    *out = attachment ? attachment->image : NULL;
    return ROBO_OK;
}

robo_result_t robo_html_element_image_detach(
    const robo_html_node_handle_t *element, robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    size_t i;
    robo_result_t res = image_element_gate(element, diag);

    if (res != ROBO_OK) {
        return res;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_NOT_FOUND,
                              "document does not resolve");
    }
    for (i = 0u; i < slot->image_count; i++) {
        if (handle_same_node(&slot->images[i].node, element)) {
            attachment_remove(slot, i);
            break;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_html_element_image_resolve(
    const robo_html_node_handle_t *element,
    const robo_html_image_resolver_t *resolver, robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_image_attributes_t attributes;
    robo_html_image_request_t request;
    robo_html_image_t *image = NULL;
    robo_result_t res;
    const uint8_t *bytes = NULL;
    size_t length = 0u;
    robo_result_t fetch;

    if (!resolver || !resolver->fetch) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                              ROBO_INVALID_ARGUMENT,
                              "resolver is null or unusable");
    }
    res = image_element_gate(element, diag);
    if (res != ROBO_OK) {
        return res;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_NOT_FOUND,
                              "document does not resolve");
    }
    res = robo_html_element_image_attributes(element, &attributes, diag);
    if (res != ROBO_OK) {
        return res;
    }

    /*
     * No usable src: the element has no resource, so any earlier one is
     * released. That is what makes `removeAttribute("src")` observable
     * without teaching this function about attributes being mutable.
     */
    if (!attributes.source_usable) {
        robo_result_t detached = robo_html_element_image_detach(element, diag);

        return (detached == ROBO_OK)
                   ? ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_OK,
                                    "no usable source")
                   : detached;
    }

    request.reference = attributes.source;
    request.reference_length = attributes.source_length;
    request.alt = attributes.alt;
    request.alt_length = attributes.alt_length;
    request.document_name = slot->source_name;

    fetch = resolver->fetch(resolver->userdata, &request, &bytes, &length, diag);
    if (fetch == ROBO_BUSY) {
        /* An acquisition is already outstanding: leave the element PENDING
         * rather than inventing a failure for work that has not finished. */
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_BUSY,
                              "image fetch already outstanding");
    }
    if (fetch == ROBO_OK && (!bytes || length == 0u)) {
        /* A resolver that says OK without bytes is broken; treat it as a
         * failed fetch rather than storing an empty resource. */
        fetch = ROBO_ERROR;
    }

    {
        char *reference = (char *)robo_malloc(attributes.source_length + 1u);

        if (reference) {
            memcpy(reference, attributes.source, attributes.source_length);
            reference[attributes.source_length] = '\0';
        }
        if (fetch == ROBO_OK) {
            res = robo_html_image_create_decoded(bytes, length, reference,
                                                 &image, diag);
        } else {
            image = image_new(state_for_result(fetch), fetch, reference);
            res = image ? ROBO_OK : ROBO_OUT_OF_MEMORY;
        }
        if (reference) {
            robo_free(reference);
        }
        if (res != ROBO_OK) {
            return res;
        }
        if (!image) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE,
                                  ROBO_OUT_OF_MEMORY, "image allocation");
        }
    }

    res = attachment_store(slot, element, image, diag);
    if (res != ROBO_OK) {
        robo_html_image_release(image);
        return res;
    }
    /*
     * The table's reference is the one that counts. Resolving twice replaces
     * the resource rather than accumulating, so a mutated `src` costs one
     * decode and no leak.
     */
    robo_html_image_release(image);
    robo_log_debug("robo_html: img resolved src=\"%s\" state=%s",
                   attributes.source ? attributes.source : "",
                   robo_html_image_state_name(robo_html_image_state(
                       attachment_find(slot, element)
                           ? attachment_find(slot, element)->image
                           : NULL)));
    return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_IMAGE, ROBO_OK,
                          robo_html_image_state_name(
                              robo_html_element_image_state(element)));
}
