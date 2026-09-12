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

#define ROBO_SOCK_UNUSED    0
#define ROBO_SOCK_OPEN      1
#define ROBO_SOCK_BOUND     2
#define ROBO_SOCK_LISTENING 3
#define ROBO_SOCK_CONNECTING 4
#define ROBO_SOCK_CONNECTED 5
#define ROBO_SOCK_CLOSED    6

typedef struct RoboSocket {
    int used;
    int kind;
    int proto;
    int sstate;
    uint32_t lip;
    uint32_t rip;
    uint16_t lport;
    uint16_t rport;
    int shutdown_rx;
    int shutdown_tx;
    int err;
    int nonblock;
    struct RoboProcess *owner;
    struct RoboFile *file;

    struct RoboThread *rx_waiter;
    struct RoboThread *tx_waiter;
    struct RoboThread *conn_waiter;
    int rx_posted;
    int tx_posted;
    int conn_posted;

    RoboPacket *udp_q[ROBO_NET_UDP_Q];
    unsigned udp_qn;

    uint8_t tcp_state;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t snd_wnd;
    uint32_t iss;
    uint32_t rcv_nxt;
    uint32_t rcv_wnd;
    uint32_t irs;
    uint8_t *snd_buf;
    uint8_t *rcv_buf;
    unsigned snd_len;
    unsigned snd_cap;
    unsigned rcv_len;
    unsigned rcv_cap;
    unsigned snd_sent;
    uint64_t rto_deadline;
    unsigned retries;
    uint64_t tw_deadline;
    int backlog;
    struct RoboSocket *listen_parent;
    struct RoboSocket *accept_q[ROBO_NET_ACCEPT_Q];
    unsigned accept_n;
    int fin_sent;
    int fin_rcvd;
} RoboSocket;

int robo_sock_init(void);
RoboSocket *robo_sock_alloc(int kind, int proto, struct RoboProcess *owner);
void robo_sock_free(RoboSocket *s);
RoboSocket *robo_sock_from_fd(struct RoboProcess *p, int fd);
int robo_sock_bind(RoboSocket *s, uint32_t ip, uint16_t port);
uint16_t robo_sock_ephemeral(int proto, uint32_t ip);
RoboSocket *robo_sock_find_udp(uint32_t ip, uint16_t port);
RoboSocket *robo_sock_find_tcp(uint32_t lip, uint16_t lport,
                               uint32_t rip, uint16_t rport);
RoboSocket *robo_sock_find_listen(uint32_t ip, uint16_t port);
unsigned robo_sock_count(void);
RoboSocket *robo_sock_at(unsigned i);
void robo_sock_report(void);

uint64_t robo_sys_socket(struct RoboSyscallContext *ctx);

#endif
