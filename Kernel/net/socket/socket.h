#ifndef ROBO_NET_SOCKET_H
#define ROBO_NET_SOCKET_H

#include <stdint.h>
#include "net/core/packet.h"
#include "net/core/net_util.h"
#include "net/tcp/tcp.h"

struct RoboProcess;
struct RoboFile;
struct RoboThread;
struct RoboSyscallContext;

#define RoboSockUnused    0
#define RoboSockOpen      1
#define RoboSockBound     2
#define RoboSockListening 3
#define RoboSockConnecting 4
#define RoboSockConnected 5
#define RoboSockClosed    6

typedef struct RoboSocket {
    int used;
    int kind;
    int proto;
    int sstate;
    uint32_t lip;
    uint32_t rip;
    uint16_t lport;
    uint16_t rport;
    int shutdownRx;
    int shutdownTx;
    int err;
    int nonblock;
    struct RoboProcess *owner;
    struct RoboFile *file;

    struct RoboThread *rxWaiter;
    struct RoboThread *txWaiter;
    struct RoboThread *connWaiter;
    int rxPosted;
    int txPosted;
    int connPosted;

    RoboPacket *udpQ[RoboNetUdpQ];
    unsigned udpQn;

    uint8_t tcpState;
    uint32_t sndUna;
    uint32_t sndNxt;
    uint32_t sndWnd;
    uint32_t iss;
    uint32_t rcvNxt;
    uint32_t rcvWnd;
    uint32_t irs;
    uint8_t *sndBuf;
    uint8_t *rcvBuf;
    unsigned sndLen;
    unsigned sndCap;
    unsigned rcvLen;
    unsigned rcvCap;
    unsigned sndSent;
    uint64_t rtoDeadline;
    unsigned retries;
    uint64_t twDeadline;
    int backlog;
    struct RoboSocket *listenParent;
    struct RoboSocket *acceptQ[RoboNetAcceptQ];
    unsigned acceptN;
    int finSent;
    int finRcvd;
} RoboSocket;

int SockInit(void);
RoboSocket *SockAlloc(int kind, int proto, struct RoboProcess *owner);
void SockFree(RoboSocket *s);
RoboSocket *SockFromFd(struct RoboProcess *p, int fd);
int SockBind(RoboSocket *s, uint32_t ip, uint16_t port);
uint16_t SockEphemeral(int proto, uint32_t ip);
RoboSocket *SockFindUdp(uint32_t ip, uint16_t port);
RoboSocket *SockFindTcp(uint32_t lip, uint16_t lport,
                               uint32_t rip, uint16_t rport);
RoboSocket *SockFindListen(uint32_t ip, uint16_t port);
unsigned SockCount(void);
RoboSocket *SockAt(unsigned i);
void SockReport(void);

uint64_t SysSocket(struct RoboSyscallContext *ctx);

#endif
