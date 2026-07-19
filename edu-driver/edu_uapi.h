#pragma once

#include <linux/ioctl.h>
#include <linux/types.h>

#define EDU_IOCTL_MAGIC 'E'

struct edu_reg_io {
    __u32 offset;
    __u32 value;
};

struct edu_factorial {
    __u32 input;
    __u32 result;
    __u32 timeout_ms;
};

#define EDU_IOCTL_REG_READ \
    _IOWR(EDU_IOCTL_MAGIC, 0x00, struct edu_reg_io)

#define EDU_IOCTL_REG_WRITE \
    _IOW(EDU_IOCTL_MAGIC, 0x01, struct edu_reg_io)

#define EDU_IOCTL_FACTORIAL \
    _IOWR(EDU_IOCTL_MAGIC, 0x02, struct edu_factorial)
