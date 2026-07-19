#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
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
#define MAX_STRESS_THREADS     256u
#define INVALID_IOCTL_INTERVAL 100u

struct stress_worker {
    const char *device;
    int fd;
    uint32_t iterations;
    uint64_t successful_ioctls;
    uint64_t expected_failures;
    uint64_t unexpected_failures;
    uint64_t invalid_values;
};

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s open [device]\n"
            "  %s read <offset> [device]\n"
            "  %s write <offset> <value> [device]\n"
            "  %s factorial <input> [device]\n"
            "  %s invalid [device]\n"
            "  %s all <offset> <value> [device]\n"
            "  %s stress <threads> <iterations> [device]\n"
            "  %s open-stress <threads> <iterations> [device]\n"
            "\n"
            "Examples:\n"
            "  %s open\n"
            "  %s read 0x00\n"
            "  %s write 0x04 0x12345678\n"
            "  %s factorial 5\n"
            "  %s invalid\n"
            "  %s all 0x04 0x12345678\n"
            "  %s stress 8 10000\n"
            "  %s open-stress 8 1000\n",
            program, program, program, program, program, program, program,
            program,
            program, program, program, program, program, program, program,
            program);
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
    if (close(fd) < 0) {
        fprintf(stderr,
                "[FAIL] close: %s\n",
                strerror(errno));
        return -1;
    }

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

static int test_factorial(int fd, uint32_t input)
{
    struct edu_factorial factorial = {
        .input = input,
        .result = 0,
        .timeout_ms = 1000,
    };
    uint32_t expected = 1;
    uint32_t i;

    if (input > 12) {
        fprintf(stderr,
                "[FAIL] factorial input must be between 0 and 12\n");
        return -1;
    }

    printf("[TEST] ioctl FACTORIAL input=%u timeout=%u ms\n",
            factorial.input,
            factorial.timeout_ms);

    if (ioctl(fd, EDU_IOCTL_FACTORIAL, &factorial) < 0) {
        fprintf(stderr,
                "[FAIL] EDU_IOCTL_FACTORIAL: %s (errno=%d)\n",
                strerror(errno),
                errno);
        return -1;
    }

    for (i = 2; i <= input; i++)
        expected *= i;

    if (factorial.result != expected) {
        fprintf(stderr,
                "[FAIL] %u!: expected %u, got %u\n",
                factorial.input, expected, factorial.result);
        return -1;
    }

    printf("[PASS] %u! = %u\n",
            factorial.input,
            factorial.result);

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

static int parse_stress_args(const char *threads_text,
                             const char *iterations_text,
                             uint32_t *thread_count,
                             uint32_t *iterations)
{
    if (parse_u32(threads_text, thread_count) < 0 ||
        parse_u32(iterations_text, iterations) < 0)
        return -1;

    if (*thread_count == 0 || *thread_count > MAX_STRESS_THREADS) {
        fprintf(stderr, "[FAIL] threads must be between 1 and %u\n",
                MAX_STRESS_THREADS);
        return -1;
    }

    if (*iterations == 0) {
        fprintf(stderr, "[FAIL] iterations must be greater than zero\n");
        return -1;
    }

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

static int read_identification_quiet(int fd, uint32_t *value)
{
    struct edu_reg_io reg = {
        .offset = EDU_REG_IDENTIFICATION,
        .value = 0,
    };

    if (ioctl(fd, EDU_IOCTL_REG_READ, &reg) < 0)
        return -1;

    *value = reg.value;
    return 0;
}

static void *shared_fd_worker(void *argument)
{
    struct stress_worker *worker = argument;
    uint64_t i;

    for (i = 0; i < worker->iterations; i++) {
        uint32_t value;

        if (read_identification_quiet(worker->fd, &value) < 0) {
            worker->unexpected_failures++;
        } else if (value != EDU_IDENTIFICATION) {
            worker->invalid_values++;
        } else {
            worker->successful_ioctls++;
        }

        if ((i + 1) % INVALID_IOCTL_INTERVAL == 0) {
            errno = 0;
            if (ioctl(worker->fd, EDU_IOCTL_INVALID) < 0 && errno == ENOTTY)
                worker->expected_failures++;
            else
                worker->unexpected_failures++;
        }
    }

    return NULL;
}

static void *open_close_worker(void *argument)
{
    struct stress_worker *worker = argument;
    uint64_t i;

    for (i = 0; i < worker->iterations; i++) {
        uint32_t value;
        int fd = open(worker->device, O_RDWR);

        if (fd < 0) {
            worker->unexpected_failures++;
            continue;
        }

        if (read_identification_quiet(fd, &value) < 0) {
            worker->unexpected_failures++;
        } else if (value != EDU_IDENTIFICATION) {
            worker->invalid_values++;
        } else {
            worker->successful_ioctls++;
        }

        if (close(fd) < 0)
            worker->unexpected_failures++;
    }

    return NULL;
}

static int run_stress(const char *name, const char *device, int fd,
                      uint32_t thread_count, uint32_t iterations,
                      void *(*worker_fn)(void *))
{
    struct stress_worker *workers;
    pthread_t *threads;
    uint64_t successful_ioctls = 0;
    uint64_t expected_failures = 0;
    uint64_t unexpected_failures = 0;
    uint64_t invalid_values = 0;
    uint32_t created = 0;
    uint32_t i;
    int status = 0;

    workers = calloc(thread_count, sizeof(*workers));
    threads = calloc(thread_count, sizeof(*threads));
    if (!workers || !threads) {
        fprintf(stderr, "[FAIL] unable to allocate stress workers\n");
        free(threads);
        free(workers);
        return -1;
    }

    printf("[TEST] %s threads=%u iterations=%u device=%s\n",
           name, thread_count, iterations, device);

    for (i = 0; i < thread_count; i++) {
        int error;

        workers[i].device = device;
        workers[i].fd = fd;
        workers[i].iterations = iterations;

        error = pthread_create(&threads[i], NULL, worker_fn, &workers[i]);
        if (error != 0) {
            fprintf(stderr, "[FAIL] pthread_create: %s\n", strerror(error));
            status = -1;
            break;
        }
        created++;
    }

    for (i = 0; i < created; i++) {
        int error = pthread_join(threads[i], NULL);

        if (error != 0) {
            fprintf(stderr, "[FAIL] pthread_join: %s\n", strerror(error));
            status = -1;
        }

        successful_ioctls += workers[i].successful_ioctls;
        expected_failures += workers[i].expected_failures;
        unexpected_failures += workers[i].unexpected_failures;
        invalid_values += workers[i].invalid_values;
    }

    printf("[RESULT] successful ioctls:   %" PRIu64 "\n",
           successful_ioctls);
    printf("[RESULT] expected failures:   %" PRIu64 "\n",
           expected_failures);
    printf("[RESULT] unexpected failures: %" PRIu64 "\n",
           unexpected_failures);
    printf("[RESULT] invalid values:      %" PRIu64 "\n",
           invalid_values);

    if (unexpected_failures != 0 || invalid_values != 0)
        status = -1;

    free(threads);
    free(workers);
    return status;
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
    uint32_t thread_count = 0;
    uint32_t iterations = 0;
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
    } else if (strcmp(command, "read") == 0 || strcmp(command, "factorial") == 0) {
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
    } else if (strcmp(command, "stress") == 0 ||
               strcmp(command, "open-stress") == 0) {
        if (argc < 4 || argc > 5 ||
            parse_stress_args(argv[2], argv[3],
                              &thread_count, &iterations) < 0) {
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

    if (strcmp(command, "open-stress") == 0) {
        status = run_stress("open/close stress", device, -1,
                            thread_count, iterations, open_close_worker);
        printf("\n%s\n", status == 0 ? "[PASS] test completed" :
                                       "[FAIL] test failed");
        return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    fd = open_device(device);
    if (fd < 0)
        return EXIT_FAILURE;

    if (strcmp(command, "read") == 0)
        status = test_reg_read(fd, offset, NULL);
    else if (strcmp(command, "write") == 0)
        status = test_reg_write(fd, offset, value);
    else if (strcmp(command, "factorial") == 0)
        status = test_factorial(fd, offset);
    else if (strcmp(command, "invalid") == 0)
        status = test_invalid_ioctl(fd);
    else if (strcmp(command, "all") == 0) {
        if (test_identification(fd) < 0 ||
            test_invalid_ioctl(fd) < 0 ||
            test_write_read(fd, offset, value) < 0)
            status = -1;
    } else if (strcmp(command, "stress") == 0) {
        status = run_stress("shared-fd ioctl stress", device, fd,
                            thread_count, iterations, shared_fd_worker);
    }

    return finish_test(fd, status);
}
