#ifndef ROBO_USER_TOOLCHAIN_H
#define ROBO_USER_TOOLCHAIN_H

/* Guest multi-version toolchains under /System/toolchains. */

#define ROBO_TC_ROOT      "/System/toolchains"
#define ROBO_TC_DEFAULTS  "/System/toolchains/defaults"
#define ROBO_TC_CATALOG   "/System/robo/catalog"
#define ROBO_TC_NAME_MAX  32
#define ROBO_TC_VER_MAX   32
#define ROBO_TC_PATH_MAX  192

int robo_tc_read_default(const char *name, char *ver, unsigned ver_max);
int robo_tc_write_default(const char *name, const char *ver);
int robo_tc_bin_path(const char *name, const char *ver, const char *bin,
                     char *out, unsigned out_max);
int robo_tc_catalog_path(const char *name, const char *ver, const char *bin,
                         char *out, unsigned out_max);
int robo_tc_parse_spec(const char *spec, char *name, unsigned nmax,
                       char *ver, unsigned vmax);

#endif
