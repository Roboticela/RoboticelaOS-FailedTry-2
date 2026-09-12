#ifndef ROBO_NET_TCP_H
#define ROBO_NET_TCP_H

#include <stdint.h>

struct RoboSocket;
struct RoboPacket;

#define ROBO_TCP_CLOSED       0u
#define ROBO_TCP_LISTEN       1u
#define ROBO_TCP_SYN_SENT     2u
#define ROBO_TCP_SYN_RECEIVED 3u
#define ROBO_TCP_ESTABLISHED  4u
#define ROBO_TCP_FIN_WAIT_1   5u
#define ROBO_TCP_FIN_WAIT_2   6u
#define ROBO_TCP_CLOSE_WAIT   7u
#define ROBO_TCP_CLOSING      8u
#define ROBO_TCP_LAST_ACK     9u
#define ROBO_TCP_TIME_WAIT    10u

#define ROBO_TCP_EV_PASSIVE_OPEN 0
#define ROBO_TCP_EV_ACTIVE_OPEN  1
#define ROBO_TCP_EV_SYN          2
#define ROBO_TCP_EV_SYNACK       3
#define ROBO_TCP_EV_ACK          4
#define ROBO_TCP_EV_FIN          5
#define ROBO_TCP_EV_RST          6
#define ROBO_TCP_EV_CLOSE        7
#define ROBO_TCP_EV_TIMEOUT      8

#define ROBO_TCPF_FIN 0x01u
#define ROBO_TCPF_SYN 0x02u
#define ROBO_TCPF_RST 0x04u
#define ROBO_TCPF_PSH 0x08u
#define ROBO_TCPF_ACK 0x10u
#define ROBO_TCPF_URG 0x20u

int robo_tcp_sm_apply(uint8_t *state, int event);
int robo_tcp_rx(struct RoboPacket *pkt, uint32_t src, uint32_t dst);
int robo_tcp_connect(struct RoboSocket *s, uint32_t dst, uint16_t port);
int robo_tcp_listen(struct RoboSocket *s, int backlog);
int robo_tcp_accept(struct RoboSocket *s, struct RoboSocket **out);
int robo_tcp_send(struct RoboSocket *s, const void *buf, unsigned len);
int robo_tcp_recv(struct RoboSocket *s, void *buf, unsigned len, int nonblock);
int robo_tcp_close(struct RoboSocket *s);
int robo_tcp_shutdown(struct RoboSocket *s, int how);
void robo_tcp_timer(void);
const char *robo_tcp_state_name(uint8_t st);

#endif
