#ifndef ROBO_NET_TCP_H
#define ROBO_NET_TCP_H

#include <stdint.h>

struct RoboSocket;
struct RoboPacket;

#define RoboTcpClosed       0u
#define RoboTcpListen       1u
#define RoboTcpSynSent     2u
#define RoboTcpSynReceived 3u
#define RoboTcpEstablished  4u
#define RoboTcpFinWait1   5u
#define RoboTcpFinWait2   6u
#define RoboTcpCloseWait   7u
#define RoboTcpClosing      8u
#define RoboTcpLastAck     9u
#define RoboTcpTimeWait    10u

#define RoboTcpEvPassiveOpen 0
#define RoboTcpEvActiveOpen  1
#define RoboTcpEvSyn          2
#define RoboTcpEvSynack       3
#define RoboTcpEvAck          4
#define RoboTcpEvFin          5
#define RoboTcpEvRst          6
#define RoboTcpEvClose        7
#define RoboTcpEvTimeout      8

#define RoboTcpfFin 0x01u
#define RoboTcpfSyn 0x02u
#define RoboTcpfRst 0x04u
#define RoboTcpfPsh 0x08u
#define RoboTcpfAck 0x10u
#define RoboTcpfUrg 0x20u

int TcpSmApply(uint8_t *state, int event);
int TcpRx(struct RoboPacket *pkt, uint32_t src, uint32_t dst);
int TcpConnect(struct RoboSocket *s, uint32_t dst, uint16_t port);
int TcpListen(struct RoboSocket *s, int backlog);
int TcpAccept(struct RoboSocket *s, struct RoboSocket **out);
int TcpSend(struct RoboSocket *s, const void *buf, unsigned len);
int TcpRecv(struct RoboSocket *s, void *buf, unsigned len, int nonblock);
int TcpClose(struct RoboSocket *s);
int TcpShutdown(struct RoboSocket *s, int how);
void TcpTimer(void);
const char *TcpStateName(uint8_t st);

#endif
