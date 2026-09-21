#ifndef ROBO_USER_TOOLCHAIN_H
#define ROBO_USER_TOOLCHAIN_H

/* Guest multi-version toolchains under /System/toolchains. */

#define RoboTcRoot      "/System/toolchains"
#define RoboTcDefaults  "/System/toolchains/defaults"
#define RoboTcCatalog   "/System/robo/catalog"
#define RoboTcNameMax  32
#define RoboTcVerMax   32
#define RoboTcPathMax  192

int TcReadDefault(const char *name, char *ver, unsigned verMax);
int TcWriteDefault(const char *name, const char *ver);
int TcBinPath(const char *name, const char *ver, const char *bin,
                     char *out, unsigned outMax);
int TcCatalogPath(const char *name, const char *ver, const char *bin,
                         char *out, unsigned outMax);
int TcParseSpec(const char *spec, char *name, unsigned nmax,
                       char *ver, unsigned vmax);

#endif
