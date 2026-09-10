#include "roboboot.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    rb_probe *out;
    char root[RB_MAX_PATH];
} probe_ctx;

static int file_exists(const char *path)
{
    void *data = NULL;
    size_t len = 0;
    if (!rb_plat || !rb_plat->read_file) return 0;
    if (rb_plat->read_file(path, &data, &len) != RB_OK) return 0;
    rb_free(data);
    return 1;
}

static void on_vol(const char *name, int is_dir, void *user)
{
    probe_ctx *ctx = (probe_ctx *)user;
    rb_volume *v;
    char path[RB_MAX_PATH];
    char probe[RB_MAX_PATH];
    if (!is_dir || !name || name[0] == '.') return;
    if (ctx->out->nvols >= 16) return;
    v = &ctx->out->vols[ctx->out->nvols];
    rb_zero(v, sizeof(*v));
    rb_strcpy(v->name, sizeof(v->name), name);
    rb_join(path, sizeof(path), ctx->root, name);
    rb_strcpy(v->path, sizeof(v->path), path);
    rb_strcpy(v->fs, sizeof(v->fs), "hostfs");
    rb_join(probe, sizeof(probe), path, "kernel.elf");
    if (file_exists(probe)) v->has_kernel = 1;
    rb_join(probe, sizeof(probe), path, "sys/kernel.elf");
    if (file_exists(probe)) v->has_kernel = 1;
    rb_join(probe, sizeof(probe), path, "EFI/Microsoft/Boot/bootmgfw.efi");
    if (file_exists(probe)) v->has_efi = 1;
    ctx->out->nvols++;
}

rb_status rb_probe_volumes(const char *volumes_root, rb_probe *out)
{
    probe_ctx ctx;
    if (!out) return RB_ERR;
    rb_zero(out, sizeof(*out));
    if (!volumes_root || !rb_plat || !rb_plat->list_dir) return RB_OK;
    rb_zero(&ctx, sizeof(ctx));
    ctx.out = out;
    rb_strcpy(ctx.root, sizeof(ctx.root), volumes_root);
    return rb_plat->list_dir(volumes_root, on_vol, &ctx);
}

void rb_probe_apply(rb_catalog *c, const rb_probe *p)
{
    int i, j;
    if (!c || !p) return;
    for (i = 0; i < c->ntargets; i++) {
        rb_target *t = &c->targets[i];
        if (!t->volume[0]) {
            t->present = 1;
            continue;
        }
        t->present = 0;
        for (j = 0; j < p->nvols; j++) {
            if (rb_str_eq(p->vols[j].name, t->volume)) {
                t->present = 1;
                break;
            }
        }
        /* firmware / reboot / halt do not need a volume */
        if (t->kind == RB_KIND_FIRMWARE || t->kind == RB_KIND_REBOOT || t->kind == RB_KIND_HALT)
            t->present = 1;
    }
}
