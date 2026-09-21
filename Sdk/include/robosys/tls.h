#ifndef ROBOSYS_TLS_H
#define ROBOSYS_TLS_H

#include "socket.h"

/*
 * TLS 1.3 is not implemented in this phase. The OS has SHA-256/512 and
 * Ed25519, but no CSPRNG, AES-GCM, ChaCha20-Poly1305, or X25519.
 * These calls fail closed with RoboEnotsup. Do not treat a success
 * return as a secure session.
 */
typedef struct RoboTLSConnection {
    int socket;
    int state;
} RoboTLSConnection;

int TlsConnect(int socket, const char *hostname, RoboTLSConnection *out);
int TlsSend(RoboTLSConnection *c, const void *buf, unsigned len);
int TlsRecv(RoboTLSConnection *c, void *buf, unsigned len);
int TlsClose(RoboTLSConnection *c);

#endif
