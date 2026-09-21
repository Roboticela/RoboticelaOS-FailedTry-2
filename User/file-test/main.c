#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int main(void)
{
    int fd;
    char buf[16];
    struct stat st;
    const char *msg = "file-test\n";

    fd = open("/file-test.txt", O_CREAT | O_RDWR | O_TRUNC, 0);
    if (fd < 0)
        return 1;
    if (write(fd, msg, 10) != 10) {
        close(fd);
        return 2;
    }
    close(fd);

    fd = open("/file-test.txt", O_RDONLY);
    if (fd < 0)
        return 3;
    if (read(fd, buf, 10) != 10) {
        close(fd);
        return 4;
    }
    close(fd);

    fd = open("/file-test.txt", O_RDWR);
    if (fd < 0)
        return 5;
    if (lseek(fd, 0, SEEK_SET) != 0) {
        close(fd);
        return 6;
    }
    close(fd);

    if (rename("/file-test.txt", "/file-test2.txt") != 0)
        return 7;
    if (stat("/file-test2.txt", &st) != 0 || st.stSize != 10)
        return 8;
    if (unlink("/file-test2.txt") != 0)
        return 9;
    printf("[ OK ] file-test\n");
    return 0;
}
