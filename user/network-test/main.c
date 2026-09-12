#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <robo/socket.h>

int main(void)
{
    int s;
    robo_sockaddr_t sa;
    char buf[64];
    const char *hello = "Hello from Roboticela OS\n";
    ssize_t n;

    s = robo_socket(ROBO_AF_INET, ROBO_SOCK_STREAM, 0);
    if (s < 0)
        return 1;
    sa.family = ROBO_AF_INET;
    sa.port = 18019;
    sa.addr = 0x0A000204u;
    sa.zero = 0;
    if (robo_connect(s, &sa) != 0) {
        close(s);
        return 2;
    }
    if (robo_send(s, hello, 25) < 0) {
        close(s);
        return 3;
    }
    n = robo_recv(s, buf, 63);
    close(s);
    if (n < 15 || memcmp(buf, "Hello from host", 15) != 0)
        return 4;
    printf("[ OK ] network-test\n");
    return 0;
}
