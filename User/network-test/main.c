#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <robo/socket.h>

int main(void)
{
    int s;
    Sockaddr sa;
    char buf[64];
    const char *hello = "Hello from Roboticela OS\n";
    ssize_t n;

    s = Socket(RoboAfInet, RoboSockStream, 0);
    if (s < 0)
        return 1;
    sa.family = RoboAfInet;
    sa.port = 18019;
    sa.addr = 0x0A000204u;
    sa.zero = 0;
    if (Connect(s, &sa) != 0) {
        close(s);
        return 2;
    }
    if (Send(s, hello, 25) < 0) {
        close(s);
        return 3;
    }
    n = Recv(s, buf, 63);
    close(s);
    if (n < 15 || memcmp(buf, "Hello from host", 15) != 0)
        return 4;
    printf("[ OK ] network-test\n");
    return 0;
}
