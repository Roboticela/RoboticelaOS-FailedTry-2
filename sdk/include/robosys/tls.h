#ifndef ROBOSYS_TLS_H
#define ROBOSYS_TLS_H

#include "socket.h"

/*
 * TLS 1.3 is not implemented in this phase. The OS has SHA-256/512 and
 * Ed25519, but no CSPRNG, AES-GCM, ChaCha20-Poly1305, or X25519.
 * These calls fail closed with ROBO_ENOTSUP. Do not treat a success
 * return as a secure session.
 */
typedef struct RoboTLSConnection {
    int socket;
    int state;
} RoboTLSConnection;

int robo_tls_connect(int socket, const char *hostname, RoboTLSConnection *out);
int robo_tls_send(RoboTLSConnection *c, const void *buf, unsigned len);
int robo_tls_recv(RoboTLSConnection *c, void *buf, unsigned len);
int robo_tls_close(RoboTLSConnection *c);

#endif
