#include "roboboot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *SkipWs(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    return s;
}

static void rstrip(char *s)
{
    int n = Strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[--n] = 0;
    }
}

static void SplitKv(char *line, char **key, char **val)
{
    char *sp;
    *key = line;
    *val = "";
    sp = line;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;
    if (*sp) {
        *sp = 0;
        *val = SkipWs(sp + 1);
    }
}

Kind KindFromName(const char *s)
{
    if (StrEq(s, "kernel") || StrEq(s, "roboticela")) return RbKindKernel;
    if (StrEq(s, "linux")) return RbKindLinux;
    if (StrEq(s, "multiboot2") || StrEq(s, "mb2")) return RbKindMultiboot2;
    if (StrEq(s, "efi-chain") || StrEq(s, "chain") || StrEq(s, "efi")) return RbKindEfiChain;
    if (StrEq(s, "firmware") || StrEq(s, "setup")) return RbKindFirmware;
    if (StrEq(s, "reboot")) return RbKindReboot;
    if (StrEq(s, "halt")) return RbKindHalt;
    return RbKindKernel;
}

const char *KindName(Kind k)
{
    switch (k) {
    case RbKindKernel: return "kernel";
    case RbKindLinux: return "linux";
    case RbKindMultiboot2: return "multiboot2";
    case RbKindEfiChain: return "efi-chain";
    case RbKindFirmware: return "firmware";
    case RbKindReboot: return "reboot";
    case RbKindHalt: return "halt";
    default: return "kernel";
    }
}

int CatalogFind(const Catalog *c, const char *id)
{
    int i;
    if (!c || !id || !id[0]) return -1;
    for (i = 0; i < c->ntargets; i++) {
        if (StrEq(c->targets[i].id, id)) return i;
    }
    return -1;
}

RoboRbStatus ManifestParse(const char *text, Catalog *out)
{
    char line[RbMaxLine];
    const char *p;
    Target *cur = NULL;
    int inTarget = 0;

    if (!out) return RbErr;
    Zero(out, sizeof(*out));
    out->version = 1;
    out->splashMinMs = RbSplashMinMs;
    out->pickerKey = RbModAlt;
    out->pickerTimeoutS = 0;
    Strcpy(out->pickerHint, sizeof(out->pickerHint), "Choose a startup volume");
    Strcpy(out->splashLogo, sizeof(out->splashLogo), "assets/logo.rbim");
    Strcpy(out->envPath, sizeof(out->envPath), "config/roboboot.env");
    out->saveLast = 1;

    if (!text) return RbErrParse;
    p = text;
    while (*p) {
        int n = 0;
        char *key, *val;
        while (p[n] && p[n] != '\n') n++;
        if (n >= RbMaxLine) n = RbMaxLine - 1;
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        p += n;
        if (*p == '\n') p++;
        rstrip(SkipWs(line));
        {
            char *s = SkipWs(line);
            if (s != line) memmove(line, s, strlen(s) + 1);
        }
        if (line[0] == 0 || line[0] == '#') continue;

        SplitKv(line, &key, &val);

        if (!inTarget) {
            if (StrEq(key, "manifest")) {
                out->version = atoi(val);
            } else if (StrEq(key, "splash.min_ms")) {
                out->splashMinMs = atoi(val);
                if (out->splashMinMs < 0) out->splashMinMs = 0;
            } else if (StrEq(key, "splash.logo")) {
                Strcpy(out->splashLogo, sizeof(out->splashLogo), val);
            } else if (StrEq(key, "picker.key")) {
                out->pickerKey = StrEq(val, "shift") ? RbModShift :
                                  StrEq(val, "ctrl") ? RbModCtrl : RbModAlt;
            } else if (StrEq(key, "picker.timeout_s")) {
                out->pickerTimeoutS = atoi(val);
            } else if (StrEq(key, "picker.hint")) {
                Strcpy(out->pickerHint, sizeof(out->pickerHint), val);
            } else if (StrEq(key, "default")) {
                Strcpy(out->defaultId, sizeof(out->defaultId), val);
            } else if (StrEq(key, "fallback")) {
                Strcpy(out->fallbackId, sizeof(out->fallbackId), val);
            } else if (StrEq(key, "save_last")) {
                out->saveLast = atoi(val) != 0;
            } else if (StrEq(key, "env.path")) {
                Strcpy(out->envPath, sizeof(out->envPath), val);
            } else if (StrEq(key, "target")) {
                if (out->ntargets >= RbMaxTargets) return RbErrParse;
                cur = &out->targets[out->ntargets++];
                Zero(cur, sizeof(*cur));
                Strcpy(cur->id, sizeof(cur->id), val);
                Strcpy(cur->title, sizeof(cur->title), val);
                cur->kind = RbKindKernel;
                cur->present = 1;
                inTarget = 1;
            } else {
                return RbErrParse;
            }
        } else {
            if (StrEq(key, "end")) {
                inTarget = 0;
                cur = NULL;
            } else if (!cur) {
                return RbErrParse;
            } else if (StrEq(key, "title")) {
                Strcpy(cur->title, sizeof(cur->title), val);
            } else if (StrEq(key, "kind")) {
                cur->kind = KindFromName(val);
            } else if (StrEq(key, "icon") || StrEq(key, "pic")) {
                Strcpy(cur->icon, sizeof(cur->icon), val);
            } else if (StrEq(key, "volume")) {
                Strcpy(cur->volume, sizeof(cur->volume), val);
            } else if (StrEq(key, "kernel")) {
                Strcpy(cur->kernel, sizeof(cur->kernel), val);
            } else if (StrEq(key, "ramdisk") || StrEq(key, "initrd")) {
                Strcpy(cur->ramdisk, sizeof(cur->ramdisk), val);
            } else if (StrEq(key, "path") || StrEq(key, "chain")) {
                Strcpy(cur->chain, sizeof(cur->chain), val);
            } else if (StrEq(key, "args")) {
                Strcpy(cur->args, sizeof(cur->args), val);
            } else if (StrEq(key, "module")) {
                if (cur->nmodules < RbMaxModules)
                    Strcpy(cur->modules[cur->nmodules++], RbMaxPath, val);
            } else if (StrEq(key, "hidden")) {
                cur->hidden = atoi(val) != 0;
            } else if (StrEq(key, "lock")) {
                cur->restricted = 1;
                Strcpy(cur->lock, sizeof(cur->lock), val);
            } else if (StrEq(key, "hotkey")) {
                cur->hotkey = val[0] ? (unsigned char)val[0] : 0;
            } else {
                return RbErrParse;
            }
        }
    }
    if (inTarget) return RbErrParse;
    if (out->ntargets == 0) return RbErrParse;
    return RbOk;
}

RoboRbStatus ManifestLoadPath(const char *path, Catalog *out)
{
    void *data = NULL;
    size_t len = 0;
    RoboRbStatus st;
    if (!Plat || !Plat->RoboReadFile) return RbErrIo;
    st = Plat->RoboReadFile(path, &data, &len);
    if (st != RbOk) return st;
    Bootlogf(NULL, "Catalog  read  %zu bytes", len);
    {
        char *text = (char *)Alloc(len + 1);
        if (!text) {
            RoboRbFree(data);
            return RbErrNomem;
        }
        memcpy(text, data, len);
        text[len] = 0;
        RoboRbFree(data);
        st = ManifestParse(text, out);
        RoboRbFree(text);
        return st;
    }
}
