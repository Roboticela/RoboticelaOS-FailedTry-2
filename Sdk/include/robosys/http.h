#ifndef ROBOSYS_HTTP_H
#define ROBOSYS_HTTP_H

#include "socket.h"

typedef struct RoboHttpResponse {
    int status;
    unsigned bodyLen;
    char body[2048];
} RoboHttpResponse;

int HttpGet(const char *host, uint16_t port, const char *path,
                  RoboHttpResponse *out);
int HttpGetIp(uint32_t ipv4, uint16_t port, const char *host,
                     const char *path, RoboHttpResponse *out);

#endif
