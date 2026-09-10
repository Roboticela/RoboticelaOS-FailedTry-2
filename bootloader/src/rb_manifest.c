#include "roboboot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *skip_ws(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    return s;
}

static void rstrip(char *s)
{
    int n = rb_strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[--n] = 0;
    }
}

static void split_kv(char *line, char **key, char **val)
{
    char *sp;
    *key = line;
    *val = "";
    sp = line;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;
    if (*sp) {
        *sp = 0;
        *val = skip_ws(sp + 1);
    }
}

rb_kind rb_kind_from_name(const char *s)
{
    if (rb_str_eq(s, "kernel") || rb_str_eq(s, "roboticela")) return RB_KIND_KERNEL;
    if (rb_str_eq(s, "linux")) return RB_KIND_LINUX;
    if (rb_str_eq(s, "multiboot2") || rb_str_eq(s, "mb2")) return RB_KIND_MULTIBOOT2;
    if (rb_str_eq(s, "efi-chain") || rb_str_eq(s, "chain") || rb_str_eq(s, "efi")) return RB_KIND_EFI_CHAIN;
    if (rb_str_eq(s, "firmware") || rb_str_eq(s, "setup")) return RB_KIND_FIRMWARE;
    if (rb_str_eq(s, "reboot")) return RB_KIND_REBOOT;
    if (rb_str_eq(s, "halt")) return RB_KIND_HALT;
    return RB_KIND_KERNEL;
}

const char *rb_kind_name(rb_kind k)
{
    switch (k) {
    case RB_KIND_KERNEL: return "kernel";
    case RB_KIND_LINUX: return "linux";
    case RB_KIND_MULTIBOOT2: return "multiboot2";
    case RB_KIND_EFI_CHAIN: return "efi-chain";
    case RB_KIND_FIRMWARE: return "firmware";
    case RB_KIND_REBOOT: return "reboot";
    case RB_KIND_HALT: return "halt";
    default: return "kernel";
    }
}

int rb_catalog_find(const rb_catalog *c, const char *id)
{
    int i;
    if (!c || !id || !id[0]) return -1;
    for (i = 0; i < c->ntargets; i++) {
        if (rb_str_eq(c->targets[i].id, id)) return i;
    }
    return -1;
}

rb_status rb_manifest_parse(const char *text, rb_catalog *out)
{
    char line[RB_MAX_LINE];
    const char *p;
    rb_target *cur = NULL;
    int in_target = 0;

    if (!out) return RB_ERR;
    rb_zero(out, sizeof(*out));
    out->version = 1;
    out->splash_min_ms = RB_SPLASH_MIN_MS;
    out->picker_key = RB_MOD_ALT;
    out->picker_timeout_s = 0;
    rb_strcpy(out->picker_hint, sizeof(out->picker_hint), "Choose a startup volume");
    rb_strcpy(out->splash_logo, sizeof(out->splash_logo), "assets/logo.rbim");
    rb_strcpy(out->env_path, sizeof(out->env_path), "config/roboboot.env");
    out->save_last = 1;

    if (!text) return RB_ERR_PARSE;
    p = text;
    while (*p) {
        int n = 0;
        char *key, *val;
        while (p[n] && p[n] != '\n') n++;
        if (n >= RB_MAX_LINE) n = RB_MAX_LINE - 1;
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        p += n;
        if (*p == '\n') p++;
        rstrip(skip_ws(line));
        {
            char *s = skip_ws(line);
            if (s != line) memmove(line, s, strlen(s) + 1);
        }
        if (line[0] == 0 || line[0] == '#') continue;

        split_kv(line, &key, &val);

        if (!in_target) {
            if (rb_str_eq(key, "manifest")) {
                out->version = atoi(val);
            } else if (rb_str_eq(key, "splash.min_ms")) {
                out->splash_min_ms = atoi(val);
                if (out->splash_min_ms < 0) out->splash_min_ms = 0;
            } else if (rb_str_eq(key, "splash.logo")) {
                rb_strcpy(out->splash_logo, sizeof(out->splash_logo), val);
            } else if (rb_str_eq(key, "picker.key")) {
                out->picker_key = rb_str_eq(val, "shift") ? RB_MOD_SHIFT :
                                  rb_str_eq(val, "ctrl") ? RB_MOD_CTRL : RB_MOD_ALT;
            } else if (rb_str_eq(key, "picker.timeout_s")) {
                out->picker_timeout_s = atoi(val);
            } else if (rb_str_eq(key, "picker.hint")) {
                rb_strcpy(out->picker_hint, sizeof(out->picker_hint), val);
            } else if (rb_str_eq(key, "default")) {
                rb_strcpy(out->default_id, sizeof(out->default_id), val);
            } else if (rb_str_eq(key, "fallback")) {
                rb_strcpy(out->fallback_id, sizeof(out->fallback_id), val);
            } else if (rb_str_eq(key, "save_last")) {
                out->save_last = atoi(val) != 0;
            } else if (rb_str_eq(key, "env.path")) {
                rb_strcpy(out->env_path, sizeof(out->env_path), val);
            } else if (rb_str_eq(key, "target")) {
                if (out->ntargets >= RB_MAX_TARGETS) return RB_ERR_PARSE;
                cur = &out->targets[out->ntargets++];
                rb_zero(cur, sizeof(*cur));
                rb_strcpy(cur->id, sizeof(cur->id), val);
                rb_strcpy(cur->title, sizeof(cur->title), val);
                cur->kind = RB_KIND_KERNEL;
                cur->present = 1;
                in_target = 1;
            } else {
                return RB_ERR_PARSE;
            }
        } else {
            if (rb_str_eq(key, "end")) {
                in_target = 0;
                cur = NULL;
            } else if (!cur) {
                return RB_ERR_PARSE;
            } else if (rb_str_eq(key, "title")) {
                rb_strcpy(cur->title, sizeof(cur->title), val);
            } else if (rb_str_eq(key, "kind")) {
                cur->kind = rb_kind_from_name(val);
            } else if (rb_str_eq(key, "icon") || rb_str_eq(key, "pic")) {
                rb_strcpy(cur->icon, sizeof(cur->icon), val);
            } else if (rb_str_eq(key, "volume")) {
                rb_strcpy(cur->volume, sizeof(cur->volume), val);
            } else if (rb_str_eq(key, "kernel")) {
                rb_strcpy(cur->kernel, sizeof(cur->kernel), val);
            } else if (rb_str_eq(key, "ramdisk") || rb_str_eq(key, "initrd")) {
                rb_strcpy(cur->ramdisk, sizeof(cur->ramdisk), val);
            } else if (rb_str_eq(key, "path") || rb_str_eq(key, "chain")) {
                rb_strcpy(cur->chain, sizeof(cur->chain), val);
            } else if (rb_str_eq(key, "args")) {
                rb_strcpy(cur->args, sizeof(cur->args), val);
            } else if (rb_str_eq(key, "module")) {
                if (cur->nmodules < RB_MAX_MODULES)
                    rb_strcpy(cur->modules[cur->nmodules++], RB_MAX_PATH, val);
            } else if (rb_str_eq(key, "hidden")) {
                cur->hidden = atoi(val) != 0;
            } else if (rb_str_eq(key, "lock")) {
                cur->restricted = 1;
                rb_strcpy(cur->lock, sizeof(cur->lock), val);
            } else if (rb_str_eq(key, "hotkey")) {
                cur->hotkey = val[0] ? (unsigned char)val[0] : 0;
            } else {
                return RB_ERR_PARSE;
            }
        }
    }
    if (in_target) return RB_ERR_PARSE;
    if (out->ntargets == 0) return RB_ERR_PARSE;
    return RB_OK;
}

rb_status rb_manifest_load_path(const char *path, rb_catalog *out)
{
    void *data = NULL;
    size_t len = 0;
    rb_status st;
    if (!rb_plat || !rb_plat->read_file) return RB_ERR_IO;
    st = rb_plat->read_file(path, &data, &len);
    if (st != RB_OK) return st;
    {
        char *text = (char *)rb_alloc(len + 1);
        if (!text) {
            rb_free(data);
            return RB_ERR_NOMEM;
        }
        memcpy(text, data, len);
        text[len] = 0;
        rb_free(data);
        st = rb_manifest_parse(text, out);
        rb_free(text);
        return st;
    }
}
