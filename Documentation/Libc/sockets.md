# Sockets

Native API (`robo/socket.h`) wraps `SYS_SOCKET` (Phase 19):

`Socket`, `Bind`, `Listen`, `Accept`,
`Connect`, `Send`, `Recv`.

Addresses use host byte order (`family`, `port`, `addr`) matching
`RoboSockAddr`. Close sockets with `close()`.

Applications must not include kernel networking headers or touch NIC
structures.
