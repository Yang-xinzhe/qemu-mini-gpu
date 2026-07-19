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
#define EDU_REG_IDENTIFICATION 0x00u
#define EDU_REG_LIVENESS       0x04u
#define EDU_IDENTIFICATION     0x010000edu
#define EDU_IOCTL_INVALID      _IO(EDU_IOCTL_MAGIC, 0x7f)

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s open [device]\n"
            "  %s read <offset> [device]\n"
            "  %s write <offset> <value> [device]\n"
            "  %s invalid [device]\n"
            "  %s all <offset> <value> [device]\n"
            "\n"
            "Examples:\n"
            "  %s open\n"
            "  %s read 0x00\n"
            "  %s write 0x04 0x12345678\n"
            "  %s invalid\n"
            "  %s all 0x04 0x12345678\n",
            program, program, program, program, program,
            program, program, program, program, program);
}

static int open_device(const char *device)
{
    int fd;

    printf("[TEST] Opening %s\n", device);

    fd = open(device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[FAIL] open(%s): %s\n", device, strerror(errno));
        return -1;
    }
    printf("[PASS] open success, fd=%d\n", fd);
    return fd;
}

static int close_device(int fd)
{
    // printf("[TEST] closing fd=%d\n", fd);

    if (close(fd) < 0) {
        fprintf(stderr,
                "[FAIL] close: %s\n",
                strerror(errno));
        return -1;
    }

    // printf("[PASS] close succeeded\n");
    return 0;
}

static int test_reg_read(int fd, uint32_t offset, uint32_t *value)
{
    struct edu_reg_io reg = {
        .offset = offset,
        .value = 0,
    };

    printf("[TEST] ioctl REG_READ offset=0x%08x\n", offset);

    if (ioctl(fd, EDU_IOCTL_REG_READ, &reg) < 0) {
        fprintf(stderr,
                "[FAIL] EDU_IOCTL_REG_READ: %s "
                "(errno=%d)\n",
                strerror(errno), errno);
        return -1;
    }

    printf("[PASS] BAR0[0x%08x] = 0x%08x\n",
           reg.offset, reg.value);

    if (value)
        *value = reg.value;

    return 0;
}

static int test_reg_write(int fd, uint32_t offset, uint32_t value)
{
    struct edu_reg_io reg = {
        .offset = offset,
        .value = value,
    };

    printf("[TEST] ioctl REG_WRITE "
           "offset=0x%08x value=0x%08x\n",
           offset, value);

    if (ioctl(fd, EDU_IOCTL_REG_WRITE, &reg) < 0) {
        fprintf(stderr,
                "[FAIL] EDU_IOCTL_REG_WRITE: %s "
                "(errno=%d)\n",
                strerror(errno), errno);
        return -1;
    }

    printf("[PASS] write ioctl succeeded\n");
    return 0;
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
        fprintf(stderr, "[FAIL] invalid 32-bit integer: %s\n", text);
        return -1;
    }

    *value = (uint32_t)parsed;
    return 0;
}

static int test_identification(int fd)
{
    uint32_t value;

    if (test_reg_read(fd, EDU_REG_IDENTIFICATION, &value) < 0)
        return -1;

    if (value != EDU_IDENTIFICATION) {
        fprintf(stderr,
                "[FAIL] identification: expected 0x%08x, got 0x%08x\n",
                EDU_IDENTIFICATION, value);
        return -1;
    }

    printf("[PASS] identification matches QEMU edu device\n");
    return 0;
}

static int test_invalid_ioctl(int fd)
{
    printf("[TEST] unsupported ioctl command\n");

    errno = 0;
    if (ioctl(fd, EDU_IOCTL_INVALID) == 0) {
        fprintf(stderr, "[FAIL] unsupported ioctl unexpectedly succeeded\n");
        return -1;
    }

    if (errno != ENOTTY) {
        fprintf(stderr,
                "[FAIL] unsupported ioctl: expected ENOTTY, got %s "
                "(errno=%d)\n",
                strerror(errno), errno);
        return -1;
    }

    printf("[PASS] unsupported ioctl returned ENOTTY\n");
    return 0;
}

static int test_write_read(int fd, uint32_t offset, uint32_t value)
{
    uint32_t result;

    if (test_reg_write(fd, offset, value) < 0 ||
        test_reg_read(fd, offset, &result) < 0)
        return -1;

    if (offset == EDU_REG_LIVENESS && result != ~value) {
        fprintf(stderr,
                "[FAIL] liveness: expected 0x%08x, got 0x%08x\n",
                ~value, result);
        return -1;
    }

    if (offset == EDU_REG_LIVENESS)
        printf("[PASS] liveness register returned the complemented value\n");

    return 0;
}

static int finish_test(int fd, int status)
{
    if (close_device(fd) < 0)
        status = -1;

    printf("\n%s\n", status == 0 ? "[PASS] test completed" :
                                   "[FAIL] test failed");
    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, const char **argv)
{
    const char *command;
    const char *device = DEFAULT_DEVICE;
    uint32_t offset = 0;
    uint32_t value = 0;
    int fd;
    int status = 0;

    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    command = argv[1];

    if (strcmp(command, "open") == 0 || strcmp(command, "invalid") == 0) {
        if (argc > 3) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (argc == 3)
            device = argv[2];
    } else if (strcmp(command, "read") == 0) {
        if (argc < 3 || argc > 4 || parse_u32(argv[2], &offset) < 0) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (argc == 4)
            device = argv[3];
    } else if (strcmp(command, "write") == 0 || strcmp(command, "all") == 0) {
        if (argc < 4 || argc > 5 ||
            parse_u32(argv[2], &offset) < 0 ||
            parse_u32(argv[3], &value) < 0) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (argc == 5)
            device = argv[4];
    } else {
        fprintf(stderr, "[FAIL] unknown command: %s\n", command);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    fd = open_device(device);
    if (fd < 0)
        return EXIT_FAILURE;

    if (strcmp(command, "read") == 0)
        status = test_reg_read(fd, offset, NULL);
    else if (strcmp(command, "write") == 0)
        status = test_reg_write(fd, offset, value);
    else if (strcmp(command, "invalid") == 0)
        status = test_invalid_ioctl(fd);
    else if (strcmp(command, "all") == 0) {
        if (test_identification(fd) < 0 ||
            test_invalid_ioctl(fd) < 0 ||
            test_write_read(fd, offset, value) < 0)
            status = -1;
    }

    return finish_test(fd, status);
}
