/*
 * RoboFont discovery + registry implementation (Goal 5.12).
 */

#include "RoboFont/RoboFontDiscovery.h"
#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ---- Limits (internal virtual tree) ---- */
#define VIO_MAX_NODES 512u
#define VIO_MAX_NAME 256u

typedef struct disc_source {
    robo_font_discovery_source_id_t id;
    robo_font_discovery_source_kind_t kind;
    char path[ROBO_FONT_DISCOVERY_MAX_PATH];
    int recursive;
    /* MEMORY only */
    robo_font_source_t *mem_source;
    uint8_t active;
} disc_source_t;

struct robo_font_registry {
    uint64_t next_reg_id;
    uint64_t next_src_id;
    uint32_t generation;

    disc_source_t sources[ROBO_FONT_REGISTRY_MAX_SOURCES];
    uint32_t source_count;

    robo_font_registration_t *regs;
    uint32_t reg_count;
    uint32_t reg_cap;

    const robo_font_discovery_io_t *io;

    robo_font_registry_invalidate_fn invalidate_fn;
    void *invalidate_ud;
};

/* ---- Helpers ---- */

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (!dst || cap == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0u; i + 1u < cap && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int path_len_ok(const char *path)
{
    size_t n;
    if (!path || !path[0]) {
        return 0;
    }
    n = strlen(path);
    return n > 0u && n < ROBO_FONT_DISCOVERY_MAX_PATH;
}

static int utf8_casefold_eq(const char *a, const char *b)
{
    int cmp = 1;
    size_t al, bl;
    if (!a || !b) {
        return 0;
    }
    al = strlen(a);
    bl = strlen(b);
    if (robo_unicode_casefold_compare_utf8(a, al, b, bl, ROBO_UNICODE_REPLACE, &cmp) !=
        ROBO_OK) {
        return 0;
    }
    return cmp == 0;
}

static void snapshot_field(char *dst, size_t cap, const robo_font_meta_field_t *f)
{
    dst[0] = '\0';
    if (!f || !f->utf8 || f->utf8_len == 0u) {
        return;
    }
    if (f->avail != ROBO_FONT_NAME_PRESENT && f->avail != ROBO_FONT_NAME_FALLBACK) {
        return;
    }
    if (f->utf8_len >= cap) {
        memcpy(dst, f->utf8, cap - 1u);
        dst[cap - 1u] = '\0';
    } else {
        memcpy(dst, f->utf8, f->utf8_len);
        dst[f->utf8_len] = '\0';
    }
}

void robo_font_discovery_result_init(robo_font_discovery_result_t *r)
{
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->last_error = ROBO_OK;
}

robo_font_candidate_format_t robo_font_discovery_classify_bytes(const uint8_t *data,
                                                               size_t size)
{
    uint32_t tag;
    if (!data || size < 4u) {
        return ROBO_FONT_CANDIDATE_UNKNOWN;
    }
    tag = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
          ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    if (tag == ROBO_FONT_SFNT_VERSION_TTCF) {
        return ROBO_FONT_CANDIDATE_COLLECTION;
    }
    if (tag == ROBO_FONT_SFNT_VERSION_OTTO) {
        return ROBO_FONT_CANDIDATE_OTF;
    }
    if (tag == ROBO_FONT_SFNT_VERSION_TRUE || tag == ROBO_FONT_SFNT_VERSION_TRUE_TAG ||
        tag == ROBO_FONT_SFNT_VERSION_TYP1) {
        return ROBO_FONT_CANDIDATE_TTF;
    }
    return ROBO_FONT_CANDIDATE_UNSUPPORTED;
}

int robo_font_discovery_extension_is_font(const char *path_or_name)
{
    const char *dot;
    char ext[8];
    size_t i, n;
    if (!path_or_name) {
        return 0;
    }
    dot = strrchr(path_or_name, '.');
    if (!dot || !dot[1]) {
        return 0;
    }
    n = strlen(dot + 1);
    if (n == 0u || n >= sizeof(ext)) {
        return 0;
    }
    for (i = 0u; i < n; i++) {
        char c = dot[1 + i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        ext[i] = c;
    }
    ext[n] = '\0';
    return strcmp(ext, "ttf") == 0 || strcmp(ext, "otf") == 0 ||
           strcmp(ext, "ttc") == 0 || strcmp(ext, "otc") == 0;
}

/* ---- Stub IO ---- */

static robo_result_t stub_read(void *ud, const char *path, size_t max_size,
                               uint8_t **out_bytes, size_t *out_size)
{
    (void)ud;
    (void)path;
    (void)max_size;
    if (out_bytes) {
        *out_bytes = NULL;
    }
    if (out_size) {
        *out_size = 0u;
    }
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t stub_list(void *ud, const char *path,
                               int (*visit)(void *, const char *, int), void *visit_ud)
{
    (void)ud;
    (void)path;
    (void)visit;
    (void)visit_ud;
    return ROBO_NOT_SUPPORTED;
}

static int stub_is_dir(void *ud, const char *path)
{
    (void)ud;
    (void)path;
    return 0;
}

static int stub_is_file(void *ud, const char *path)
{
    (void)ud;
    (void)path;
    return 0;
}

static const robo_font_discovery_io_t g_stub_io = {
    NULL, stub_read, stub_list, stub_is_dir, stub_is_file};

const robo_font_discovery_io_t *robo_font_discovery_io_stub(void)
{
    return &g_stub_io;
}

/* ---- Virtual IO ---- */

typedef struct vio_node {
    char name[VIO_MAX_NAME];
    int is_dir;
    uint8_t *bytes; /* file only; owned */
    size_t size;
    uint32_t parent; /* index; 0 = root */
    uint8_t used;
} vio_node_t;

struct robo_font_discovery_virtual_io {
    vio_node_t nodes[VIO_MAX_NODES];
    uint32_t node_count; /* includes root at 0 */
    robo_font_discovery_io_t ops;
};

static uint32_t vio_find_child(const robo_font_discovery_virtual_io_t *vio,
                               uint32_t parent,
                               const char *name)
{
    uint32_t i;
    for (i = 1u; i < vio->node_count; i++) {
        if (vio->nodes[i].used && vio->nodes[i].parent == parent &&
            strcmp(vio->nodes[i].name, name) == 0) {
            return i;
        }
    }
    return 0u;
}

static robo_result_t vio_ensure_path(robo_font_discovery_virtual_io_t *vio,
                                     const char *path,
                                     int as_dir,
                                     uint32_t *out_node)
{
    uint32_t cur = 0u;
    const char *p;
    char part[VIO_MAX_NAME];
    size_t pi;

    if (!vio || !path || path[0] != '/') {
        return ROBO_INVALID_ARGUMENT;
    }
    p = path + 1;
    if (*p == '\0') {
        if (out_node) {
            *out_node = 0u;
        }
        return ROBO_OK;
    }
    while (*p) {
        int last;
        pi = 0u;
        while (*p && *p != '/') {
            if (pi + 1u >= sizeof(part)) {
                return ROBO_INVALID_ARGUMENT;
            }
            part[pi++] = *p++;
        }
        part[pi] = '\0';
        if (pi == 0u) {
            return ROBO_INVALID_ARGUMENT;
        }
        last = (*p == '\0');
        if (*p == '/') {
            p++;
        }
        {
            uint32_t child = vio_find_child(vio, cur, part);
            if (child == 0u) {
                if (vio->node_count >= VIO_MAX_NODES) {
                    return ROBO_OUT_OF_MEMORY;
                }
                child = vio->node_count++;
                memset(&vio->nodes[child], 0, sizeof(vio->nodes[child]));
                vio->nodes[child].used = 1u;
                vio->nodes[child].parent = cur;
                copy_str(vio->nodes[child].name, sizeof(vio->nodes[child].name), part);
                vio->nodes[child].is_dir = (!last || as_dir) ? 1 : 0;
            }
            if (last && !as_dir) {
                if (vio->nodes[child].is_dir && vio->nodes[child].bytes == NULL) {
                    /* converting empty dir to file is ok if no children — skip check */
                }
                vio->nodes[child].is_dir = 0;
            }
            if (!last && !vio->nodes[child].is_dir) {
                return ROBO_INVALID_ARGUMENT;
            }
            cur = child;
        }
    }
    if (out_node) {
        *out_node = cur;
    }
    return ROBO_OK;
}

static int vio_lookup(const robo_font_discovery_virtual_io_t *vio,
                      const char *path,
                      uint32_t *out_node)
{
    uint32_t cur = 0u;
    const char *p;
    char part[VIO_MAX_NAME];
    size_t pi;

    if (!vio || !path || path[0] != '/' || !out_node) {
        return 0;
    }
    *out_node = 0u;
    p = path + 1;
    if (*p == '\0') {
        *out_node = 0u;
        return 1;
    }
    while (*p) {
        uint32_t child;
        pi = 0u;
        while (*p && *p != '/') {
            if (pi + 1u >= sizeof(part)) {
                return 0;
            }
            part[pi++] = *p++;
        }
        part[pi] = '\0';
        if (pi == 0u) {
            return 0;
        }
        if (*p == '/') {
            p++;
        }
        child = vio_find_child(vio, cur, part);
        if (child == 0u) {
            return 0;
        }
        cur = child;
    }
    *out_node = cur;
    return 1;
}

static robo_result_t vio_read(void *userdata, const char *path, size_t max_size,
                              uint8_t **out_bytes, size_t *out_size)
{
    robo_font_discovery_virtual_io_t *vio = (robo_font_discovery_virtual_io_t *)userdata;
    uint32_t node = 0u;
    uint8_t *copy;

    if (!vio || !out_bytes || !out_size) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_bytes = NULL;
    *out_size = 0u;
    if (!vio_lookup(vio, path, &node) || node == 0u || vio->nodes[node].is_dir ||
        !vio->nodes[node].bytes) {
        return ROBO_NOT_FOUND;
    }
    if (vio->nodes[node].size > max_size) {
        return ROBO_PARSE_ERROR;
    }
    copy = (uint8_t *)robo_malloc(vio->nodes[node].size ? vio->nodes[node].size : 1u);
    if (!copy) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (vio->nodes[node].size > 0u) {
        memcpy(copy, vio->nodes[node].bytes, vio->nodes[node].size);
    }
    *out_bytes = copy;
    *out_size = vio->nodes[node].size;
    return ROBO_OK;
}

static robo_result_t vio_list(void *userdata, const char *path,
                              int (*visit)(void *, const char *, int), void *visit_ud)
{
    robo_font_discovery_virtual_io_t *vio = (robo_font_discovery_virtual_io_t *)userdata;
    uint32_t parent = 0u;
    uint32_t i;

    if (!vio || !visit) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!path || strcmp(path, "/") == 0) {
        parent = 0u;
    } else if (!vio_lookup(vio, path, &parent) || !vio->nodes[parent].is_dir) {
        return ROBO_NOT_FOUND;
    }
    for (i = 1u; i < vio->node_count; i++) {
        if (vio->nodes[i].used && vio->nodes[i].parent == parent) {
            if (visit(visit_ud, vio->nodes[i].name, vio->nodes[i].is_dir)) {
                break;
            }
        }
    }
    return ROBO_OK;
}

static int vio_is_dir(void *userdata, const char *path)
{
    robo_font_discovery_virtual_io_t *vio = (robo_font_discovery_virtual_io_t *)userdata;
    uint32_t node = 0u;
    if (!vio || !path) {
        return 0;
    }
    if (!vio_lookup(vio, path, &node)) {
        return 0;
    }
    return node == 0u || vio->nodes[node].is_dir;
}

static int vio_is_file(void *userdata, const char *path)
{
    robo_font_discovery_virtual_io_t *vio = (robo_font_discovery_virtual_io_t *)userdata;
    uint32_t node = 0u;
    if (!vio || !path) {
        return 0;
    }
    if (!vio_lookup(vio, path, &node) || node == 0u) {
        return 0;
    }
    return !vio->nodes[node].is_dir && vio->nodes[node].bytes != NULL;
}

robo_result_t robo_font_discovery_virtual_io_create(
    robo_font_discovery_virtual_io_t **out)
{
    robo_font_discovery_virtual_io_t *vio;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    vio = (robo_font_discovery_virtual_io_t *)robo_calloc(1, sizeof(*vio));
    if (!vio) {
        return ROBO_OUT_OF_MEMORY;
    }
    vio->node_count = 1u;
    vio->nodes[0].used = 1u;
    vio->nodes[0].is_dir = 1;
    vio->nodes[0].name[0] = '\0';
    vio->ops.userdata = vio;
    vio->ops.read_file = vio_read;
    vio->ops.list_dir = vio_list;
    vio->ops.is_directory = vio_is_dir;
    vio->ops.is_file = vio_is_file;
    *out = vio;
    return ROBO_OK;
}

void robo_font_discovery_virtual_io_destroy(robo_font_discovery_virtual_io_t *vio)
{
    uint32_t i;
    if (!vio) {
        return;
    }
    for (i = 0u; i < vio->node_count; i++) {
        if (vio->nodes[i].bytes) {
            robo_free(vio->nodes[i].bytes);
            vio->nodes[i].bytes = NULL;
        }
    }
    robo_free(vio);
}

const robo_font_discovery_io_t *robo_font_discovery_virtual_io_ops(
    robo_font_discovery_virtual_io_t *vio)
{
    return vio ? &vio->ops : NULL;
}

robo_result_t robo_font_discovery_virtual_add_file(
    robo_font_discovery_virtual_io_t *vio,
    const char *path,
    const void *bytes,
    size_t size)
{
    uint32_t node = 0u;
    uint8_t *copy;
    robo_result_t res;

    if (!vio || !path || !bytes || size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (size > ROBO_FONT_DISCOVERY_MAX_FILE_SIZE) {
        return ROBO_PARSE_ERROR;
    }
    res = vio_ensure_path(vio, path, 0, &node);
    if (res != ROBO_OK) {
        return res;
    }
    if (node == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    copy = (uint8_t *)robo_malloc(size);
    if (!copy) {
        return ROBO_OUT_OF_MEMORY;
    }
    memcpy(copy, bytes, size);
    if (vio->nodes[node].bytes) {
        robo_free(vio->nodes[node].bytes);
    }
    vio->nodes[node].bytes = copy;
    vio->nodes[node].size = size;
    vio->nodes[node].is_dir = 0;
    return ROBO_OK;
}

robo_result_t robo_font_discovery_virtual_add_dir(
    robo_font_discovery_virtual_io_t *vio,
    const char *path)
{
    uint32_t node = 0u;
    if (!vio || !path) {
        return ROBO_INVALID_ARGUMENT;
    }
    return vio_ensure_path(vio, path, 1, &node);
}

/* ---- Registry core ---- */

static void notify_invalidate(robo_font_registry_t *reg,
                              robo_font_discovery_source_id_t source_id,
                              robo_font_registration_id_t registration_id)
{
    if (reg && reg->invalidate_fn) {
        reg->invalidate_fn(reg->invalidate_ud, source_id, registration_id);
    }
}

void robo_font_registry_set_invalidate_hook(robo_font_registry_t *reg,
                                            robo_font_registry_invalidate_fn fn,
                                            void *userdata)
{
    if (!reg) {
        return;
    }
    reg->invalidate_fn = fn;
    reg->invalidate_ud = userdata;
}

uint64_t robo_font_registry_generation(const robo_font_registry_t *reg)
{
    return reg ? reg->generation : 0u;
}

robo_result_t robo_font_registry_create(robo_font_registry_t **out)
{
    robo_font_registry_t *reg;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    reg = (robo_font_registry_t *)robo_calloc(1, sizeof(*reg));
    if (!reg) {
        return ROBO_OUT_OF_MEMORY;
    }
    reg->next_reg_id = 1u;
    reg->next_src_id = 1u;
    reg->io = robo_font_discovery_io_stub();
    *out = reg;
    return ROBO_OK;
}

static void free_registration_at(robo_font_registry_t *reg, uint32_t i)
{
    if (reg->regs[i].face) {
        robo_font_face_release(reg->regs[i].face);
        reg->regs[i].face = NULL;
    }
}

void robo_font_registry_clear(robo_font_registry_t *reg)
{
    uint32_t i;
    if (!reg) {
        return;
    }
    for (i = 0u; i < reg->reg_count; i++) {
        free_registration_at(reg, i);
    }
    reg->reg_count = 0u;
    reg->generation++;
    notify_invalidate(reg, 0u, 0u);
}

void robo_font_registry_destroy(robo_font_registry_t *reg)
{
    uint32_t i;
    if (!reg) {
        return;
    }
    robo_font_registry_clear(reg);
    for (i = 0u; i < reg->source_count; i++) {
        if (reg->sources[i].mem_source) {
            robo_font_source_release(reg->sources[i].mem_source);
            reg->sources[i].mem_source = NULL;
        }
    }
    if (reg->regs) {
        robo_free(reg->regs);
    }
    robo_free(reg);
}

uint32_t robo_font_registry_registration_count(const robo_font_registry_t *reg)
{
    return reg ? reg->reg_count : 0u;
}

uint32_t robo_font_registry_source_count(const robo_font_registry_t *reg)
{
    uint32_t i, n = 0u;
    if (!reg) {
        return 0u;
    }
    for (i = 0u; i < reg->source_count; i++) {
        if (reg->sources[i].active) {
            n++;
        }
    }
    return n;
}

void robo_font_registry_set_io(robo_font_registry_t *reg,
                               const robo_font_discovery_io_t *io)
{
    if (!reg) {
        return;
    }
    reg->io = io ? io : robo_font_discovery_io_stub();
}

robo_result_t robo_font_registry_registration_at(const robo_font_registry_t *reg,
                                                 uint32_t index,
                                                 const robo_font_registration_t **out)
{
    if (!reg || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= reg->reg_count) {
        return ROBO_NOT_FOUND;
    }
    *out = &reg->regs[index];
    return ROBO_OK;
}

robo_result_t robo_font_registry_find_by_id(const robo_font_registry_t *reg,
                                            robo_font_registration_id_t id,
                                            const robo_font_registration_t **out)
{
    uint32_t i;
    if (!reg || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    for (i = 0u; i < reg->reg_count; i++) {
        if (reg->regs[i].id == id) {
            *out = &reg->regs[i];
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_font_registry_find_by_face_id(const robo_font_registry_t *reg,
                                                 robo_font_face_id_t face_id,
                                                 const robo_font_registration_t **out)
{
    uint32_t i;
    if (!reg || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    for (i = 0u; i < reg->reg_count; i++) {
        if (reg->regs[i].face_id == face_id) {
            *out = &reg->regs[i];
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

static robo_result_t ensure_reg_cap(robo_font_registry_t *reg, uint32_t need)
{
    robo_font_registration_t *nregs;
    uint32_t ncap;
    if (need <= reg->reg_cap) {
        return ROBO_OK;
    }
    if (need > ROBO_FONT_REGISTRY_MAX_REGISTRATIONS) {
        return ROBO_OUT_OF_MEMORY;
    }
    ncap = reg->reg_cap == 0u ? 16u : reg->reg_cap * 2u;
    while (ncap < need) {
        ncap *= 2u;
    }
    if (ncap > ROBO_FONT_REGISTRY_MAX_REGISTRATIONS) {
        ncap = ROBO_FONT_REGISTRY_MAX_REGISTRATIONS;
    }
    nregs = (robo_font_registration_t *)robo_calloc(ncap, sizeof(*nregs));
    if (!nregs) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (reg->regs && reg->reg_count > 0u) {
        memcpy(nregs, reg->regs, (size_t)reg->reg_count * sizeof(*nregs));
        robo_free(reg->regs);
    }
    reg->regs = nregs;
    reg->reg_cap = ncap;
    return ROBO_OK;
}

static int content_duplicate(const robo_font_registry_t *reg,
                             const char *ps_name,
                             const robo_font_style_desc_t *style)
{
    uint32_t i;
    if (!ps_name || !ps_name[0] || !style) {
        return 0;
    }
    for (i = 0u; i < reg->reg_count; i++) {
        if (reg->regs[i].postscript_name[0] &&
            utf8_casefold_eq(reg->regs[i].postscript_name, ps_name) &&
            reg->regs[i].style.weight == style->weight &&
            reg->regs[i].style.stretch == style->stretch &&
            reg->regs[i].style.style == style->style) {
            return 1;
        }
    }
    return 0;
}

static int source_index_registered(const robo_font_registry_t *reg,
                                   robo_font_discovery_source_id_t source_id,
                                   uint32_t face_index,
                                   const char *origin)
{
    uint32_t i;
    const char *o = origin ? origin : "";
    for (i = 0u; i < reg->reg_count; i++) {
        if (reg->regs[i].source_id == source_id &&
            reg->regs[i].face_index == face_index &&
            strcmp(reg->regs[i].origin, o) == 0) {
            return 1;
        }
    }
    return 0;
}

static robo_result_t register_face(robo_font_registry_t *reg,
                                   robo_font_discovery_source_id_t source_id,
                                   uint32_t face_index,
                                   uint8_t is_collection,
                                   robo_font_face_t *face,
                                   uint32_t flags,
                                   const char *origin,
                                   int *out_dup)
{
    const robo_font_metadata_t *meta;
    const robo_font_classification_t *cls;
    robo_font_registration_t *slot;
    robo_result_t res;
    char family[128];
    char ps[128];

    if (out_dup) {
        *out_dup = 0;
    }
    if (!reg || !face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (source_index_registered(reg, source_id, face_index, origin)) {
        if (out_dup) {
            *out_dup = 1;
        }
        return ROBO_ALREADY_EXISTS;
    }

    res = robo_font_face_parse_metadata(face);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_face_parse_classification(face);
    if (res != ROBO_OK) {
        return res;
    }
    meta = robo_font_face_metadata(face);
    cls = robo_font_face_classification(face);
    if (!cls) {
        return ROBO_PARSE_ERROR;
    }

    family[0] = '\0';
    ps[0] = '\0';
    if (meta) {
        robo_font_meta_field_t pref_field;
        if (robo_font_metadata_preferred_family(meta, &pref_field) == ROBO_OK) {
            snapshot_field(family, sizeof(family), &pref_field);
        }
        if (!family[0]) {
            snapshot_field(family, sizeof(family), &meta->family_name);
        }
        snapshot_field(ps, sizeof(ps), &meta->postscript_name);
    }

    if ((flags & ROBO_FONT_DISCOVERY_SCAN_ALLOW_CONTENT_DUP) == 0u &&
        content_duplicate(reg, ps, &cls->style_desc)) {
        if (out_dup) {
            *out_dup = 1;
        }
        return ROBO_ALREADY_EXISTS;
    }

    res = ensure_reg_cap(reg, reg->reg_count + 1u);
    if (res != ROBO_OK) {
        return res;
    }
    slot = &reg->regs[reg->reg_count];
    memset(slot, 0, sizeof(*slot));
    slot->id = reg->next_reg_id++;
    if (slot->id == 0u) {
        slot->id = reg->next_reg_id++;
    }
    slot->source_id = source_id;
    slot->face_index = face_index;
    slot->is_collection = is_collection;
    slot->face_id = robo_font_face_id(face);
    robo_font_face_retain(face);
    slot->face = face;
    slot->style = cls->style_desc;
    copy_str(slot->family, sizeof(slot->family), family);
    copy_str(slot->postscript_name, sizeof(slot->postscript_name), ps);
    copy_str(slot->origin, sizeof(slot->origin), origin ? origin : "");
    reg->reg_count++;
    reg->generation++;
    return ROBO_OK;
}

robo_result_t robo_font_registry_collect_faces(const robo_font_registry_t *reg,
                                               robo_font_face_t ***out_faces,
                                               uint32_t *out_count)
{
    robo_font_face_t **arr;
    uint32_t i;
    if (!reg || !out_faces || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_faces = NULL;
    *out_count = 0u;
    if (reg->reg_count == 0u) {
        return ROBO_OK;
    }
    arr = (robo_font_face_t **)robo_calloc(reg->reg_count, sizeof(*arr));
    if (!arr) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < reg->reg_count; i++) {
        arr[i] = reg->regs[i].face;
    }
    *out_faces = arr;
    *out_count = reg->reg_count;
    return ROBO_OK;
}

robo_result_t robo_font_registry_collect_faces_for_family(
    const robo_font_registry_t *reg,
    const char *family,
    robo_font_face_t ***out_faces,
    uint32_t *out_count)
{
    robo_font_face_t **arr;
    uint32_t i, n = 0u;
    if (!reg || !family || !out_faces || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_faces = NULL;
    *out_count = 0u;
    for (i = 0u; i < reg->reg_count; i++) {
        if (reg->regs[i].family[0] && utf8_casefold_eq(reg->regs[i].family, family)) {
            n++;
        }
    }
    if (n == 0u) {
        return ROBO_OK;
    }
    arr = (robo_font_face_t **)robo_calloc(n, sizeof(*arr));
    if (!arr) {
        return ROBO_OUT_OF_MEMORY;
    }
    n = 0u;
    for (i = 0u; i < reg->reg_count; i++) {
        if (reg->regs[i].family[0] && utf8_casefold_eq(reg->regs[i].family, family)) {
            arr[n++] = reg->regs[i].face;
        }
    }
    *out_faces = arr;
    *out_count = n;
    return ROBO_OK;
}

robo_result_t robo_font_registry_remove_registration(robo_font_registry_t *reg,
                                                     robo_font_registration_id_t id)
{
    uint32_t i;
    if (!reg) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < reg->reg_count; i++) {
        if (reg->regs[i].id == id) {
            free_registration_at(reg, i);
            if (i + 1u < reg->reg_count) {
                memmove(&reg->regs[i], &reg->regs[i + 1u],
                        (size_t)(reg->reg_count - i - 1u) * sizeof(reg->regs[0]));
            }
            reg->reg_count--;
            reg->generation++;
            notify_invalidate(reg, 0u, id);
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_font_registry_remove_source_registrations(
    robo_font_registry_t *reg,
    robo_font_discovery_source_id_t source_id)
{
    uint32_t i = 0u;
    if (!reg) {
        return ROBO_INVALID_ARGUMENT;
    }
    while (i < reg->reg_count) {
        if (reg->regs[i].source_id == source_id) {
            free_registration_at(reg, i);
            if (i + 1u < reg->reg_count) {
                memmove(&reg->regs[i], &reg->regs[i + 1u],
                        (size_t)(reg->reg_count - i - 1u) * sizeof(reg->regs[0]));
            }
            reg->reg_count--;
        } else {
            i++;
        }
    }
    reg->generation++;
    notify_invalidate(reg, source_id, 0u);
    return ROBO_OK;
}

static disc_source_t *find_source(robo_font_registry_t *reg,
                                  robo_font_discovery_source_id_t id)
{
    uint32_t i;
    for (i = 0u; i < reg->source_count; i++) {
        if (reg->sources[i].active && reg->sources[i].id == id) {
            return &reg->sources[i];
        }
    }
    return NULL;
}

static robo_result_t alloc_source_slot(robo_font_registry_t *reg, disc_source_t **out)
{
    uint32_t i;
    if (reg->source_count >= ROBO_FONT_REGISTRY_MAX_SOURCES) {
        /* try reuse inactive */
        for (i = 0u; i < reg->source_count; i++) {
            if (!reg->sources[i].active) {
                *out = &reg->sources[i];
                memset(*out, 0, sizeof(**out));
                return ROBO_OK;
            }
        }
        return ROBO_OUT_OF_MEMORY;
    }
    *out = &reg->sources[reg->source_count++];
    memset(*out, 0, sizeof(**out));
    return ROBO_OK;
}

robo_result_t robo_font_registry_add_memory_source(robo_font_registry_t *reg,
                                                   const char *label,
                                                   const void *bytes,
                                                   size_t size,
                                                   robo_font_discovery_source_id_t *out_id)
{
    disc_source_t *s;
    robo_result_t res;
    if (!reg || !bytes || size == 0u || !out_id) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (size > ROBO_FONT_DISCOVERY_MAX_FILE_SIZE) {
        return ROBO_PARSE_ERROR;
    }
    *out_id = 0u;
    res = alloc_source_slot(reg, &s);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_source_create_memory_copy(bytes, size, &s->mem_source);
    if (res != ROBO_OK) {
        return res;
    }
    s->id = reg->next_src_id++;
    if (s->id == 0u) {
        s->id = reg->next_src_id++;
    }
    s->kind = ROBO_FONT_DISCOVERY_SOURCE_MEMORY;
    copy_str(s->path, sizeof(s->path), label ? label : "memory");
    s->active = 1u;
    *out_id = s->id;
    return ROBO_OK;
}

robo_result_t robo_font_registry_add_file_source(robo_font_registry_t *reg,
                                                 const char *path,
                                                 robo_font_discovery_source_id_t *out_id)
{
    disc_source_t *s;
    robo_result_t res;
    if (!reg || !path_len_ok(path) || !out_id) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_id = 0u;
    res = alloc_source_slot(reg, &s);
    if (res != ROBO_OK) {
        return res;
    }
    s->id = reg->next_src_id++;
    if (s->id == 0u) {
        s->id = reg->next_src_id++;
    }
    s->kind = ROBO_FONT_DISCOVERY_SOURCE_FILE;
    copy_str(s->path, sizeof(s->path), path);
    s->active = 1u;
    *out_id = s->id;
    return ROBO_OK;
}

robo_result_t robo_font_registry_add_directory_source(
    robo_font_registry_t *reg,
    const char *path,
    int recursive,
    robo_font_discovery_source_id_t *out_id)
{
    disc_source_t *s;
    robo_result_t res;
    if (!reg || !path_len_ok(path) || !out_id) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_id = 0u;
    res = alloc_source_slot(reg, &s);
    if (res != ROBO_OK) {
        return res;
    }
    s->id = reg->next_src_id++;
    if (s->id == 0u) {
        s->id = reg->next_src_id++;
    }
    s->kind = ROBO_FONT_DISCOVERY_SOURCE_DIRECTORY;
    copy_str(s->path, sizeof(s->path), path);
    s->recursive = recursive ? 1 : 0;
    s->active = 1u;
    *out_id = s->id;
    return ROBO_OK;
}

robo_result_t robo_font_registry_remove_discovery_source(
    robo_font_registry_t *reg,
    robo_font_discovery_source_id_t source_id)
{
    disc_source_t *s;
    if (!reg) {
        return ROBO_INVALID_ARGUMENT;
    }
    s = find_source(reg, source_id);
    if (!s) {
        return ROBO_NOT_FOUND;
    }
    (void)robo_font_registry_remove_source_registrations(reg, source_id);
    if (s->mem_source) {
        robo_font_source_release(s->mem_source);
        s->mem_source = NULL;
    }
    s->active = 0u;
    return ROBO_OK;
}

/* ---- Scan ---- */

typedef struct scan_ctx {
    robo_font_registry_t *reg;
    disc_source_t *src;
    uint32_t flags;
    robo_font_discovery_result_t *result;
    uint32_t files_budget;
} scan_ctx_t;

static void note_err(scan_ctx_t *ctx, robo_result_t err)
{
    ctx->result->failed++;
    ctx->result->last_error = err;
}

static robo_result_t ingest_bytes(scan_ctx_t *ctx,
                                  const char *diag_path,
                                  const uint8_t *bytes,
                                  size_t size,
                                  robo_font_source_t *existing_source)
{
    robo_font_candidate_format_t fmt;
    robo_font_source_t *src = existing_source;
    uint32_t face_count = 0u;
    uint32_t fi;
    uint8_t is_coll;
    robo_result_t res;
    int own_src = 0;

    ctx->result->candidates++;
    if (size < ROBO_FONT_DISCOVERY_MIN_CANDIDATE_BYTES) {
        ctx->result->skipped_unsupported++;
        return ROBO_OK;
    }
    fmt = robo_font_discovery_classify_bytes(bytes, size);
    if (fmt == ROBO_FONT_CANDIDATE_UNKNOWN || fmt == ROBO_FONT_CANDIDATE_UNSUPPORTED) {
        ctx->result->skipped_unsupported++;
        return ROBO_OK;
    }
    res = robo_font_sfnt_probe_face_count(bytes, size, &face_count);
    if (res != ROBO_OK) {
        ctx->result->skipped_unsupported++;
        if (ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) {
            return res;
        }
        return ROBO_OK;
    }
    if (face_count > ROBO_FONT_DISCOVERY_MAX_FACES_PER_COLLECTION) {
        note_err(ctx, ROBO_PARSE_ERROR);
        if (ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) {
            return ROBO_PARSE_ERROR;
        }
        return ROBO_OK;
    }
    is_coll = (fmt == ROBO_FONT_CANDIDATE_COLLECTION) ? 1u : 0u;
    if (!src) {
        res = robo_font_source_create_memory_copy(bytes, size, &src);
        if (res != ROBO_OK) {
            note_err(ctx, res);
            return (ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) ? res : ROBO_OK;
        }
        own_src = 1;
    }

    for (fi = 0u; fi < face_count; fi++) {
        robo_font_face_t *face = NULL;
        int dup = 0;
        res = robo_font_face_create(src, fi, &face);
        if (res != ROBO_OK) {
            note_err(ctx, res);
            if (ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) {
                if (own_src) {
                    robo_font_source_release(src);
                }
                return res;
            }
            continue;
        }
        res = register_face(ctx->reg, ctx->src->id, fi, is_coll, face, ctx->flags,
                            diag_path, &dup);
        robo_font_face_release(face);
        if (res == ROBO_ALREADY_EXISTS || dup) {
            ctx->result->skipped_duplicate++;
            continue;
        }
        if (res != ROBO_OK) {
            note_err(ctx, res);
            if (ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) {
                if (own_src) {
                    robo_font_source_release(src);
                }
                return res;
            }
            continue;
        }
        ctx->result->registered++;
    }
    if (own_src) {
        robo_font_source_release(src);
    }
    return ROBO_OK;
}

static robo_result_t scan_one_file(scan_ctx_t *ctx, const char *path)
{
    uint8_t *bytes = NULL;
    size_t size = 0u;
    robo_result_t res;

    if (ctx->files_budget == 0u) {
        note_err(ctx, ROBO_OUT_OF_MEMORY);
        return ROBO_OUT_OF_MEMORY;
    }
    ctx->files_budget--;
    ctx->result->files_seen++;

    if (!robo_font_discovery_extension_is_font(path)) {
        ctx->result->skipped_filter++;
        return ROBO_OK;
    }
    if (!ctx->reg->io || !ctx->reg->io->read_file) {
        return ROBO_NOT_SUPPORTED;
    }
    res = ctx->reg->io->read_file(ctx->reg->io->userdata, path,
                                  ROBO_FONT_DISCOVERY_MAX_FILE_SIZE, &bytes, &size);
    if (res == ROBO_NOT_FOUND || res == ROBO_PERMISSION_DENIED || res == ROBO_IO_ERROR) {
        note_err(ctx, res);
        return (ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) ? res : ROBO_OK;
    }
    if (res != ROBO_OK) {
        note_err(ctx, res);
        return (ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) ? res : ROBO_OK;
    }
    res = ingest_bytes(ctx, path, bytes, size, NULL);
    robo_free(bytes);
    return res;
}

typedef struct dir_visit_state {
    scan_ctx_t *ctx;
    char base[ROBO_FONT_DISCOVERY_MAX_PATH];
    uint32_t depth;
    robo_result_t stop_err;
} dir_visit_state_t;

static int join_path(char *out, size_t cap, const char *base, const char *name)
{
    size_t bl = strlen(base);
    size_t nl = strlen(name);
    int need_slash = (bl > 0u && base[bl - 1u] != '/');
    size_t need = bl + (need_slash ? 1u : 0u) + nl + 1u;
    if (need > cap) {
        return 0;
    }
    memcpy(out, base, bl);
    if (need_slash) {
        out[bl++] = '/';
    }
    memcpy(out + bl, name, nl);
    out[bl + nl] = '\0';
    return 1;
}

static robo_result_t scan_directory_rec(scan_ctx_t *ctx, const char *path, uint32_t depth);

static int dir_visit(void *ud, const char *name, int is_directory)
{
    dir_visit_state_t *st = (dir_visit_state_t *)ud;
    char child[ROBO_FONT_DISCOVERY_MAX_PATH];
    robo_result_t res;

    if (!name || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    if (!join_path(child, sizeof(child), st->base, name)) {
        note_err(st->ctx, ROBO_INVALID_ARGUMENT);
        return (st->ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) ? 1 : 0;
    }
    if (is_directory) {
        if (!st->ctx->src->recursive) {
            return 0;
        }
        if (st->depth + 1u > ROBO_FONT_DISCOVERY_MAX_DIR_DEPTH) {
            note_err(st->ctx, ROBO_PARSE_ERROR);
            return (st->ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) ? 1 : 0;
        }
        res = scan_directory_rec(st->ctx, child, st->depth + 1u);
        if (res != ROBO_OK && (st->ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST)) {
            st->stop_err = res;
            return 1;
        }
        return 0;
    }
    res = scan_one_file(st->ctx, child);
    if (res != ROBO_OK && (st->ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST)) {
        st->stop_err = res;
        return 1;
    }
    return 0;
}

static robo_result_t scan_directory_rec(scan_ctx_t *ctx, const char *path, uint32_t depth)
{
    dir_visit_state_t st;
    robo_result_t res;

    if (!ctx->reg->io || !ctx->reg->io->list_dir) {
        return ROBO_NOT_SUPPORTED;
    }
    if (ctx->reg->io->is_directory && !ctx->reg->io->is_directory(ctx->reg->io->userdata, path)) {
        note_err(ctx, ROBO_NOT_FOUND);
        return (ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) ? ROBO_NOT_FOUND : ROBO_OK;
    }
    memset(&st, 0, sizeof(st));
    st.ctx = ctx;
    copy_str(st.base, sizeof(st.base), path);
    st.depth = depth;
    st.stop_err = ROBO_OK;
    res = ctx->reg->io->list_dir(ctx->reg->io->userdata, path, dir_visit, &st);
    if (res != ROBO_OK) {
        note_err(ctx, res);
        return (ctx->flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST) ? res : ROBO_OK;
    }
    if (st.stop_err != ROBO_OK) {
        return st.stop_err;
    }
    return ROBO_OK;
}

robo_result_t robo_font_registry_scan_source(robo_font_registry_t *reg,
                                             robo_font_discovery_source_id_t source_id,
                                             uint32_t flags,
                                             robo_font_discovery_result_t *out_result)
{
    disc_source_t *src;
    scan_ctx_t ctx;
    robo_font_discovery_result_t local;
    robo_result_t res = ROBO_OK;

    if (!reg) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!out_result) {
        out_result = &local;
    }
    robo_font_discovery_result_init(out_result);
    src = find_source(reg, source_id);
    if (!src) {
        return ROBO_NOT_FOUND;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.reg = reg;
    ctx.src = src;
    ctx.flags = flags;
    ctx.result = out_result;
    ctx.files_budget = ROBO_FONT_DISCOVERY_MAX_FILES_PER_SCAN;

    if (src->kind == ROBO_FONT_DISCOVERY_SOURCE_MEMORY) {
        const uint8_t *bytes;
        size_t size;
        if (!src->mem_source) {
            return ROBO_INIT_ERROR;
        }
        bytes = robo_font_source_bytes(src->mem_source);
        size = robo_font_source_size(src->mem_source);
        out_result->files_seen = 1u;
        res = ingest_bytes(&ctx, src->path, bytes, size, src->mem_source);
    } else if (src->kind == ROBO_FONT_DISCOVERY_SOURCE_FILE) {
        res = scan_one_file(&ctx, src->path);
    } else if (src->kind == ROBO_FONT_DISCOVERY_SOURCE_DIRECTORY) {
        res = scan_directory_rec(&ctx, src->path, 0u);
    } else {
        return ROBO_INVALID_ARGUMENT;
    }
    return res;
}

robo_result_t robo_font_registry_scan_all(robo_font_registry_t *reg,
                                          uint32_t flags,
                                          robo_font_discovery_result_t *out_result)
{
    uint32_t i;
    robo_font_discovery_result_t acc;
    robo_font_discovery_result_t one;

    if (!reg) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_discovery_result_init(&acc);
    for (i = 0u; i < reg->source_count; i++) {
        robo_result_t res;
        if (!reg->sources[i].active) {
            continue;
        }
        res = robo_font_registry_scan_source(reg, reg->sources[i].id, flags, &one);
        acc.files_seen += one.files_seen;
        acc.candidates += one.candidates;
        acc.registered += one.registered;
        acc.skipped_duplicate += one.skipped_duplicate;
        acc.skipped_unsupported += one.skipped_unsupported;
        acc.skipped_filter += one.skipped_filter;
        acc.failed += one.failed;
        if (one.last_error != ROBO_OK) {
            acc.last_error = one.last_error;
        }
        if (res != ROBO_OK && (flags & ROBO_FONT_DISCOVERY_SCAN_FAIL_FAST)) {
            if (out_result) {
                *out_result = acc;
            }
            return res;
        }
    }
    if (out_result) {
        *out_result = acc;
    }
    return ROBO_OK;
}

robo_result_t robo_font_registry_refresh_source(robo_font_registry_t *reg,
                                                robo_font_discovery_source_id_t source_id,
                                                uint32_t flags,
                                                robo_font_discovery_result_t *out_result)
{
    robo_result_t res;
    if (!reg) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!find_source(reg, source_id)) {
        return ROBO_NOT_FOUND;
    }
    res = robo_font_registry_remove_source_registrations(reg, source_id);
    if (res != ROBO_OK) {
        return res;
    }
    return robo_font_registry_scan_source(reg, source_id, flags, out_result);
}

robo_result_t robo_font_discovery_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_registry_t *reg = NULL;
    robo_font_discovery_virtual_io_t *vio = NULL;
    robo_font_discovery_source_id_t sid = 0u;
    robo_font_discovery_result_t result;
    uint8_t tiny[16];
    size_t n;
    size_t i;

    (void)robo_font_discovery_classify_bytes(data, size);
    (void)robo_font_discovery_extension_is_font("x.ttf");
    (void)robo_font_discovery_extension_is_font("x.zzz");
    {
        uint32_t fc = 0u;
        (void)robo_font_sfnt_probe_face_count(data, size, &fc);
    }

    if (robo_font_registry_create(&reg) != ROBO_OK) {
        return ROBO_OK;
    }
    if (robo_font_discovery_virtual_io_create(&vio) != ROBO_OK) {
        robo_font_registry_destroy(reg);
        return ROBO_OK;
    }
    robo_font_registry_set_io(reg, robo_font_discovery_virtual_io_ops(vio));

    n = size < sizeof(tiny) ? size : sizeof(tiny);
    for (i = 0u; i < n; i++) {
        tiny[i] = data ? data[i] : 0u;
    }
    if (n >= 4u) {
        (void)robo_font_discovery_virtual_add_file(vio, "/Fuzz/a.ttf", tiny, n);
        (void)robo_font_registry_add_file_source(reg, "/Fuzz/a.ttf", &sid);
        (void)robo_font_registry_scan_source(reg, sid, 0u, &result);
    }
    if (data && size > 0u && size <= ROBO_FONT_DISCOVERY_MAX_FILE_SIZE) {
        (void)robo_font_registry_add_memory_source(reg, "fuzz", data, size, &sid);
        (void)robo_font_registry_scan_source(reg, sid, 0u, &result);
    }
    robo_font_discovery_virtual_io_destroy(vio);
    robo_font_registry_destroy(reg);
    return ROBO_OK;
}
