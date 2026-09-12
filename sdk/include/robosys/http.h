#ifndef ROBOSYS_HTTP_H
#define ROBOSYS_HTTP_H

#include "socket.h"

typedef struct RoboHttpResponse {
    int status;
    unsigned body_len;
    char body[2048];
} RoboHttpResponse;

int robo_http_get(const char *host, uint16_t port, const char *path,
                  RoboHttpResponse *out);
int robo_http_get_ip(uint32_t ipv4, uint16_t port, const char *host,
                     const char *path, RoboHttpResponse *out);

#endif
