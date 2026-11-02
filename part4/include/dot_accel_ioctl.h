/* Shared Linux driver/user ABI. Hardware constants remain in dot_accel_regs.h. */
#ifndef DOT_ACCEL_IOCTL_H
#define DOT_ACCEL_IOCTL_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
typedef __u32 dot_u32;
typedef __u64 dot_u64;
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint32_t dot_u32;
typedef uint64_t dot_u64;
#endif
#include "dot_accel_regs.h"

#define DOT_BLOCK_SIZE 32u
#define DOT_CAPACITY_BLOCKS (DOT_XQ_BYTES / DOT_BLOCK_SIZE)
#define DOT_DEVICE_PATH "/dev/dot_accel"

/* All input pointers refer to user memory. Scalars/scales are little endian.
 * Pointers are fixed-width and 8-byte aligned on both 32- and 64-bit ABIs.
 * A RUN owns the device from input loading through result retrieval.
 * flags/reserved must be zero; output fields are overwritten by the driver.
 */
struct dot_run {
    dot_u32 nb;
    dot_u32 flags;
    dot_u64 xq __attribute__((aligned(8)));
    dot_u64 yq __attribute__((aligned(8)));
    dot_u64 xd __attribute__((aligned(8)));
    dot_u64 yd __attribute__((aligned(8)));
    dot_u32 result_bits;
    dot_u32 cycles;
    dot_u32 status; /* captured before CLEAR, including in interrupt mode */
    dot_u32 reserved;
};

struct dot_info {
    dot_u32 id;
    dot_u32 max_blocks;
    dot_u32 block_size;
    dot_u32 irq_enabled;
};

#define DOT_IOC_RUN _IOWR('D', 0x01, struct dot_run)
#define DOT_IOC_INFO _IOR('D', 0x02, struct dot_info)
#endif
