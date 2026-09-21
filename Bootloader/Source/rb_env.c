#include "roboboot.h"

#include <stdio.h>
#include <string.h>

RoboRbStatus EnvParse(const char *text, Env *out)
{
    const char *p;
    if (!out) return RbErr;
    Zero(out, sizeof(*out));
    if (!text) return RbOk;
    if (Strncmp(text, "ROBOENV1", 8) == 0) {
        text += 8;
        if (*text == '\n' || *text == '\r') {
            if (*text == '\r' && text[1] == '\n') text += 2;
            else text++;
        }
    }
    p = text;
    while (*p) {
        char line[256];
        int n = 0;
        char *eq;
        while (p[n] && p[n] != '\n' && p[n] != '\r') n++;
        if (n > 255) n = 255;
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        p += n;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
        if (line[0] == 0 || line[0] == '#') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        {
            char *v = eq + 1;
            int vn = Strlen(v);
            while (vn > 0 && (v[vn - 1] == ' ' || v[vn - 1] == '\t' || v[vn - 1] == '\r'))
                v[--vn] = 0;
        }
        EnvSet(out, line, eq + 1);
    }
    return RbOk;
}

void EnvSet(Env *env, const char *key, const char *val)
{
    int i;
    if (!env || !key) return;
    for (i = 0; i < env->n; i++) {
        if (StrEq(env->keys[i], key)) {
            Strcpy(env->vals[i], sizeof(env->vals[i]), val ? val : "");
            return;
        }
    }
    if (env->n >= RbMaxEnv) return;
    Strcpy(env->keys[env->n], sizeof(env->keys[0]), key);
    Strcpy(env->vals[env->n], sizeof(env->vals[0]), val ? val : "");
    env->n++;
}

const char *EnvGet(const Env *env, const char *key)
{
    int i;
    if (!env || !key) return NULL;
    for (i = 0; i < env->n; i++) {
        if (StrEq(env->keys[i], key)) return env->vals[i];
    }
    return NULL;
}

int RoboRbDebugEnabled(const Env *env)
{
    const char *v = EnvGet(env, "debug");
    if (!v || !v[0]) return 0;
    if (v[0] == '0' && v[1] == 0) return 0;
    if (StrEq(v, "off") || StrEq(v, "false") || StrEq(v, "no")) return 0;
    return 1;
}

void DebugSet(Env *env, int on)
{
    EnvSet(env, "debug", on ? "1" : "0");
}

RoboRbStatus EnvLoad(const char *path, Env *out)
{
    void *data = NULL;
    size_t len = 0;
    RoboRbStatus st;
    char *text;
    if (!out) return RbErr;
    Zero(out, sizeof(*out));
    Strcpy(out->path, sizeof(out->path), path ? path : "");
    if (!path || !path[0] || !Plat || !Plat->RoboReadFile) return RbOk;
    st = Plat->RoboReadFile(path, &data, &len);
    if (st != RbOk) {
        Bootlogf(NULL, "Env  missing  %s", path);
        return RbOk; /* missing env is fine */
    }
    Bootlogf(NULL, "Env  read  %s  %zu bytes", path, len);
    text = (char *)Alloc(len + 1);
    if (!text) {
        RoboRbFree(data);
        return RbErrNomem;
    }
    memcpy(text, data, len);
    text[len] = 0;
    RoboRbFree(data);
    st = EnvParse(text, out);
    Strcpy(out->path, sizeof(out->path), path);
    RoboRbFree(text);
    return st;
}

RoboRbStatus EnvSave(const Env *env)
{
    char block[RbEnvBlock];
    int i, n = 0;
    RoboRbStatus st;
    if (!env || !env->path[0] || !Plat || !Plat->RoboWriteFile) return RbErrIo;
    memset(block, '#', sizeof(block));
    n = snprintf(block, sizeof(block), "ROBOENV1\n");
    for (i = 0; i < env->n; i++) {
        n += snprintf(block + n, sizeof(block) - (size_t)n, "%s=%s\n", env->keys[i], env->vals[i]);
        if (n >= (int)sizeof(block) - 2) break;
    }
    if (n < (int)sizeof(block)) block[n] = '#';
    block[RbEnvBlock - 1] = '#';
    st = Plat->RoboWriteFile(env->path, block, RbEnvBlock);
    if (st != RbOk)
        Bootlogf(NULL, "Env  write failed  (%d)  %s", st, env->path);
    else
        Bootlogf(NULL, "Env  write  %s  %d bytes", env->path, RbEnvBlock);
    return st;
}
