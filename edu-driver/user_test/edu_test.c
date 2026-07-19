#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "edu_uapi.h"

#define DEFAULT_DEVICE "/dev/edu_gpu0"

static int open_device(const char* device) {
    int fd;

    printf("[TEST] Opening %s\n", device);

    fd = open(device, O_RDWR);
    if(fd < 0) {
        fprintf(stderr, "[FAIL] open(%s): %s\n", device, strerror(errno));
        return -1;
    }
    printf("[PASS] open success, fd=%d\n", fd);
    return fd;
}

int main(int argc, const char** argv){
    open_device(DEFAULT_DEVICE);
    return 0;
}