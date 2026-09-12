# Sockets

Native API (`robo/socket.h`) wraps `SYS_SOCKET` (Phase 19):

`robo_socket`, `robo_bind`, `robo_listen`, `robo_accept`,
`robo_connect`, `robo_send`, `robo_recv`.

Addresses use host byte order (`family`, `port`, `addr`) matching
`RoboSockAddr`. Close sockets with `close()`.

Applications must not include kernel networking headers or touch NIC
structures.
