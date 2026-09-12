#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define ENV_MAX 32

static char *g_env[ENV_MAX + 1];
static int g_nenv;

void robo_env_init(char **envp)
{
    int i;
    g_nenv = 0;
    if (!envp)
        return;
    for (i = 0; envp[i] && g_nenv < ENV_MAX; i++) {
        size_t n = strlen(envp[i]);
        g_env[g_nenv] = (char *)malloc(n + 1);
        if (!g_env[g_nenv])
            break;
        memcpy(g_env[g_nenv], envp[i], n + 1);
        g_nenv++;
    }
    g_env[g_nenv] = 0;
}

static int env_index(const char *name)
{
    size_t n;
    int i;
    if (!name)
        return -1;
    n = strlen(name);
    for (i = 0; i < g_nenv; i++) {
        if (strncmp(g_env[i], name, n) == 0 && g_env[i][n] == '=')
            return i;
    }
    return -1;
}

char *getenv(const char *name)
{
    int i = env_index(name);
    if (i < 0)
        return 0;
    return g_env[i] + strlen(name) + 1;
}

int setenv(const char *name, const char *value, int overwrite)
{
    int i;
    size_t n, v, t;
    char *s;
    if (!name || !value || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    i = env_index(name);
    if (i >= 0 && !overwrite)
        return 0;
    n = strlen(name);
    v = strlen(value);
    t = n + 1 + v + 1;
    s = (char *)malloc(t);
    if (!s)
        return -1;
    memcpy(s, name, n);
    s[n] = '=';
    memcpy(s + n + 1, value, v + 1);
    if (i >= 0) {
        free(g_env[i]);
        g_env[i] = s;
        return 0;
    }
    if (g_nenv >= ENV_MAX) {
        free(s);
        errno = ENOMEM;
        return -1;
    }
    g_env[g_nenv++] = s;
    g_env[g_nenv] = 0;
    return 0;
}

int unsetenv(const char *name)
{
    int i = env_index(name);
    if (i < 0)
        return 0;
    free(g_env[i]);
    g_env[i] = g_env[g_nenv - 1];
    g_nenv--;
    g_env[g_nenv] = 0;
    return 0;
}
