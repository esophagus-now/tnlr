#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

int main() {
    int fd = open("pipe", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Could not open pipe");
        return -1;
    }

    char buf[80];
    puts("Issuing read");
    int rc = read(fd, buf, sizeof(buf));
    if (errno != EINPROGRESS) {
        puts("Expected this nonblocking call to start");
        return -1;
    }

    puts("Closing the file descriptor");
    int rc = close(fd);
    if (rc < 0) {
        perror("Close failed");
        return -1;
    } else {
        puts("Close succeeded");
    }

    puts("Blocking until read finishes");
    struct pollfd pfd = {fd, POLLIN};
    rc = poll(&pfd, 1, -1);
    if (rc < 0) {
        
    }
    
}