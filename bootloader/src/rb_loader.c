#include "roboboot.h"

#include <string.h>

rb_status rb_elf64_info(const uint8_t *data, size_t len, uint64_t *entry, uint64_t *phoff, int *phnum)
{
    uint16_t type, machine, ehsize, phentsize, phn;
    uint32_t version;
    uint64_t e_entry, e_phoff;
    if (!data || len < 64) return RB_ERR_FORMAT;
    if (data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') return RB_ERR_FORMAT;
    if (data[4] != 2 || data[5] != 1) return RB_ERR_FORMAT; /* ELF64 LE */
    memcpy(&type, data + 16, 2);
    memcpy(&machine, data + 18, 2);
    memcpy(&version, data + 20, 4);
    memcpy(&e_entry, data + 24, 8);
    memcpy(&e_phoff, data + 32, 8);
    memcpy(&ehsize, data + 52, 2);
    memcpy(&phentsize, data + 54, 2);
    memcpy(&phn, data + 56, 2);
    if (type != 2 && type != 3) return RB_ERR_FORMAT; /* EXEC or DYN */
    if (machine != 62) return RB_ERR_FORMAT;          /* x86_64 */
    if (version != 1 || ehsize != 64 || phentsize != 56 || phn == 0) return RB_ERR_FORMAT;
    if (e_phoff + (uint64_t)phn * 56ull > len) return RB_ERR_FORMAT;
    if (entry) *entry = e_entry;
    if (phoff) *phoff = e_phoff;
    if (phnum) *phnum = (int)phn;
    return RB_OK;
}

static void resolve_payload(const char *root, const rb_target *t, const char *rel, char *out, size_t cap)
{
    if (!rel || !rel[0]) {
        out[0] = 0;
        return;
    }
    if (rel[0] && rel[1] == ':') {
        rb_strcpy(out, cap, rel);
        return;
    }
    if (t && t->volume[0]) {
        char base[RB_MAX_PATH];
        rb_join(base, sizeof(base), root, "volumes");
        rb_join(base, sizeof(base), base, t->volume);
        rb_join(out, cap, base, rel[0] == '/' ? rel + 1 : rel);
        return;
    }
    rb_join(out, cap, root, rel[0] == '/' ? rel + 1 : rel);
}

static rb_status load_blob(const char *root, const rb_target *t, const char *rel, rb_image_blob *blob)
{
    char path[RB_MAX_PATH];
    void *data = NULL;
    size_t len = 0;
    rb_status st;
    rb_zero(blob, sizeof(*blob));
    if (!rel || !rel[0]) return RB_OK;
    resolve_payload(root, t, rel, path, sizeof(path));
    if (!rb_plat || !rb_plat->read_file) return RB_ERR_IO;
    st = rb_plat->read_file(path, &data, &len);
    if (st != RB_OK) return st;
    blob->bytes = (uint8_t *)data;
    blob->size = len;
    blob->valid = 1;
    return RB_OK;
}

void rb_handoff_free(rb_handoff *h)
{
    int i;
    if (!h) return;
    rb_free(h->kernel.bytes);
    rb_free(h->ramdisk.bytes);
    for (i = 0; i < h->nextras; i++) rb_free(h->extras[i].bytes);
    rb_zero(h, sizeof(*h));
}

rb_status rb_loader_prepare(const rb_target *t, const char *root, rb_handoff *h)
{
    rb_status st;
    int i;
    if (!t || !h) return RB_ERR;
    rb_handoff_free(h);
    h->protocol = t->kind;
    rb_strcpy(h->cmdline, sizeof(h->cmdline), t->args);

    if (t->kind == RB_KIND_FIRMWARE || t->kind == RB_KIND_REBOOT || t->kind == RB_KIND_HALT)
        return RB_OK;

    if (t->kind == RB_KIND_EFI_CHAIN) {
        st = load_blob(root, t, t->chain[0] ? t->chain : t->kernel, &h->kernel);
        return st;
    }

    st = load_blob(root, t, t->kernel, &h->kernel);
    if (st != RB_OK) return st;

    if (t->kind == RB_KIND_KERNEL || t->kind == RB_KIND_MULTIBOOT2) {
        uint64_t entry = 0, phoff = 0;
        int phnum = 0;
        st = rb_elf64_info(h->kernel.bytes, h->kernel.size, &entry, &phoff, &phnum);
        if (st != RB_OK) return st;
        h->kernel.entry = entry;
        h->kernel.phys = 0x100000;
    }

    if (t->ramdisk[0]) {
        st = load_blob(root, t, t->ramdisk, &h->ramdisk);
        if (st != RB_OK) return st;
    }
    for (i = 0; i < t->nmodules && h->nextras < RB_MAX_MODULES; i++) {
        st = load_blob(root, t, t->modules[i], &h->extras[h->nextras]);
        if (st != RB_OK) return st;
        h->nextras++;
    }
    return RB_OK;
}
