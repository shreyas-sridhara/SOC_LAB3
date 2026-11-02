// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/jiffies.h>
#include <linux/version.h>
#include "dot_accel_ioctl.h"

static bool use_irq;
module_param(use_irq, bool, 0444);
MODULE_PARM_DESC(use_irq, "Use device-tree IRQ (default: polling bring-up)");
static bool manual;
module_param(manual, bool, 0444);
MODULE_PARM_DESC(manual, "Register header-defined resources without a DT node; polling only");
#define DOT_TIMEOUT_US 1000000u

struct dot_device {
    void __iomem *regs;
    void __iomem *mem;
    struct miscdevice misc;
    struct mutex lock;
    struct completion done;
    struct kref refs;
    int irq;
    bool interrupt_mode;
    bool removed;
    u32 irq_status;
};
struct dot_inputs {
    u8 xq[DOT_XQ_BYTES], yq[DOT_XQ_BYTES];
    u8 xd[DOT_XD_BYTES], yd[DOT_XD_BYTES];
};

static void dot_free(struct kref *refs)
{
    struct dot_device *d = container_of(refs, struct dot_device, refs);
    kfree(d);
}

/* All device accesses use the mapped hardware resources. */
static u32 dot_read(struct dot_device *d, unsigned offset)
{
    return readl(d->regs + offset);
}
static void dot_write(struct dot_device *d, unsigned offset, u32 value)
{
    writel(value, d->regs + offset);
}
static void dot_mem_write(struct dot_device *d, unsigned offset, u32 value)
{
    writel(value, d->mem + offset);
}

static void dot_irq_off(struct dot_device *d)
{
    dot_write(d, DOT_REG_IRQ_EN, 0);
    dot_read(d, DOT_REG_IRQ_EN); /* flush before draining ISR */
    if (d->irq >= 0)
        synchronize_irq(d->irq);
}

static irqreturn_t dot_interrupt(int irq, void *data)
{
    struct dot_device *d = data;
    u32 status = dot_read(d, DOT_REG_STATUS);
    if (!(status & DOT_STATUS_DONE))
        return IRQ_NONE;
    WRITE_ONCE(d->irq_status, status);
    dot_write(d, DOT_REG_CTRL, DOT_CTRL_CLEAR);
    dot_read(d, DOT_REG_STATUS); /* deassert level before returning */
    complete(&d->done);
    return IRQ_HANDLED;
}

static int dot_open(struct inode *inode, struct file *file)
{
    struct dot_device *d = container_of(file->private_data, struct dot_device, misc);
    int ret = 0;
    mutex_lock(&d->lock);
    if (d->removed)
        ret = -ENODEV;
    else {
        kref_get(&d->refs);
        file->private_data = d;
    }
    mutex_unlock(&d->lock);
    return ret ? ret : nonseekable_open(inode, file);
}

static int dot_release(struct inode *inode, struct file *file)
{
    struct dot_device *d = file->private_data;
    kref_put(&d->refs, dot_free);
    return 0;
}

/* Fixed aligned 32-bit MMIO writes avoid arm64 unaligned device accesses.
 * Staging buffers are zeroed, so odd nb scale arrays have initialized padding.
 */
static void dot_load(struct dot_device *d, unsigned offset, const u8 *src, size_t bytes)
{
    size_t i;
    for (i = 0; i < ALIGN(bytes, 4); i += 4) {
        u32 word = (u32)src[i] | ((u32)src[i+1] << 8) |
                   ((u32)src[i+2] << 16) | ((u32)src[i+3] << 24);
        dot_mem_write(d, offset + i, word);
    }
}

/* Caller holds d->lock across all MMIO, waiting, and result retrieval. */
static int dot_execute(struct dot_device *d, struct dot_run *r, struct dot_inputs *in)
{
    u32 status;
    int ret = 0;
    if (dot_read(d, DOT_REG_STATUS) & DOT_STATUS_BUSY)
        return -EBUSY; /* previous timed-out operation has not finished */
    dot_irq_off(d);
    dot_write(d, DOT_REG_CTRL, DOT_CTRL_CLEAR);
    dot_read(d, DOT_REG_STATUS);
    reinit_completion(&d->done);
    WRITE_ONCE(d->irq_status, 0);
    dot_load(d, DOT_MEM_XQ, in->xq, r->nb * DOT_BLOCK_SIZE);
    dot_load(d, DOT_MEM_YQ, in->yq, r->nb * DOT_BLOCK_SIZE);
    dot_load(d, DOT_MEM_XD, in->xd, r->nb * 2);
    dot_load(d, DOT_MEM_YD, in->yd, r->nb * 2);
    dot_write(d, DOT_REG_NB, r->nb);
    dot_write(d, DOT_REG_IRQ_EN, d->interrupt_mode);
    dot_write(d, DOT_REG_CTRL, DOT_CTRL_START);
    if (d->interrupt_mode) {
        if (!wait_for_completion_timeout(&d->done, msecs_to_jiffies(1000)))
            ret = -ETIMEDOUT;
        status = READ_ONCE(d->irq_status);
    } else {
        ret = read_poll_timeout(dot_read, status, status & DOT_STATUS_DONE,
                               10, DOT_TIMEOUT_US, false, d, DOT_REG_STATUS);
    }
    /* Quiesce a late IRQ before checking results or reusing completion state. */
    dot_irq_off(d);
    if (!ret) {
        if (!(status & DOT_STATUS_DONE) || (status & (DOT_STATUS_ERR | DOT_STATUS_BUSY)))
            ret = -EIO;
        else {
            r->result_bits = dot_read(d, DOT_REG_RESULT);
            r->cycles = dot_read(d, DOT_REG_CYCLES);
            r->status = status;
        }
    }
    dot_write(d, DOT_REG_CTRL, DOT_CTRL_CLEAR);
    dot_read(d, DOT_REG_STATUS);
    /* CLEAR is not abort: after timeout a subsequent RUN still checks BUSY. */
    return ret;
}

static long dot_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct dot_device *d = file->private_data;
    void __user *user = (void __user *)arg;
    struct dot_run r;
    struct dot_info info;
    struct dot_inputs *in = NULL;
    int ret;
    if (cmd != DOT_IOC_RUN && cmd != DOT_IOC_INFO)
        return -ENOTTY;
    if (cmd == DOT_IOC_RUN) {
        if (copy_from_user(&r, user, sizeof(r)))
            return -EFAULT;
        if (r.nb > DOT_CAPACITY_BLOCKS || r.flags || r.reserved)
            return -EINVAL;
        in = kzalloc(sizeof(*in), GFP_KERNEL);
        if (!in)
            return -ENOMEM;
        if (r.nb &&
            (copy_from_user(in->xq, u64_to_user_ptr(r.xq), r.nb * DOT_BLOCK_SIZE) ||
             copy_from_user(in->yq, u64_to_user_ptr(r.yq), r.nb * DOT_BLOCK_SIZE) ||
             copy_from_user(in->xd, u64_to_user_ptr(r.xd), r.nb * 2) ||
             copy_from_user(in->yd, u64_to_user_ptr(r.yd), r.nb * 2))) {
            ret = -EFAULT;
            goto free_inputs;
        }
    }
    ret = mutex_lock_interruptible(&d->lock);
    if (ret)
        goto free_inputs;
    if (d->removed) {
        ret = -ENODEV;
        goto unlock;
    }
    if (cmd == DOT_IOC_INFO) {
        memset(&info, 0, sizeof(info));
        info.id = dot_read(d, DOT_REG_ID);
        info.max_blocks = DOT_CAPACITY_BLOCKS;
        info.block_size = DOT_BLOCK_SIZE;
        info.irq_enabled = d->interrupt_mode;
        ret = copy_to_user(user, &info, sizeof(info)) ? -EFAULT : 0;
    } else {
        r.result_bits = r.cycles = r.status = 0;
        ret = dot_execute(d, &r, in);
        if (!ret && copy_to_user(user, &r, sizeof(r)))
            ret = -EFAULT;
    }
unlock:
    mutex_unlock(&d->lock);
free_inputs:
    kfree(in);
    return ret;
}

static const struct file_operations dot_fops = {
    .owner = THIS_MODULE,
    .open = dot_open,
    .release = dot_release,
    .unlocked_ioctl = dot_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = compat_ptr_ioctl,
#endif
    .llseek = NULL,
};

static int dot_probe(struct platform_device *pdev)
{
    struct dot_device *d;
    struct resource *regs, *mem;
    int ret;
    regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
    if (!regs || !mem || resource_size(regs) < DOT_REG_WINDOW ||
        resource_size(mem) < DOT_MEM_SIZE)
        return -EINVAL;
    d = kzalloc(sizeof(*d), GFP_KERNEL);
    if (!d)
        return -ENOMEM;
    kref_init(&d->refs);
    mutex_init(&d->lock);
    init_completion(&d->done);
    d->irq = -1;
    d->interrupt_mode = use_irq;
    d->regs = devm_ioremap_resource(&pdev->dev, regs);
    if (IS_ERR(d->regs)) {
        ret = PTR_ERR(d->regs);
        goto fail;
    }
    d->mem = devm_ioremap_resource(&pdev->dev, mem);
    if (IS_ERR(d->mem)) {
        ret = PTR_ERR(d->mem);
        goto fail;
    }
    if (dot_read(d, DOT_REG_ID) != DOT_ID_VALUE) {
        dev_err(&pdev->dev, "Accelerator ID mismatch; check platform/address map\n");
        ret = -ENODEV;
        goto fail;
    }
    dot_write(d, DOT_REG_IRQ_EN, 0);
    dot_write(d, DOT_REG_CTRL, DOT_CTRL_CLEAR);
    dot_read(d, DOT_REG_STATUS);
    if (use_irq) {
        d->irq = platform_get_irq(pdev, 0);
        if (d->irq < 0) {
            ret = d->irq;
            goto fail;
        }
        ret = devm_request_irq(&pdev->dev, d->irq, dot_interrupt, 0, "dot_accel", d);
        if (ret)
            goto fail;
    }
    d->misc.minor = MISC_DYNAMIC_MINOR;
    d->misc.name = "dot_accel";
    d->misc.fops = &dot_fops;
    d->misc.parent = &pdev->dev;
    d->misc.mode = 0600;
    ret = misc_register(&d->misc);
    if (ret) {
        if (d->irq >= 0)
            devm_free_irq(&pdev->dev, d->irq, d);
        goto fail;
    }
    platform_set_drvdata(pdev, d);
    dev_info(&pdev->dev, "/dev/%s ready (%s; hardware MMIO)\n",
             d->misc.name, use_irq ? "interrupt" : "polling");
    return 0;
fail:
    kref_put(&d->refs, dot_free);
    return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void dot_remove(struct platform_device *pdev)
#else
static int dot_remove(struct platform_device *pdev)
#endif
{
    struct dot_device *d = platform_get_drvdata(pdev);
    misc_deregister(&d->misc);
    mutex_lock(&d->lock);
    d->removed = true;
    dot_irq_off(d);
    if (d->irq >= 0)
        devm_free_irq(&pdev->dev, d->irq, d);
    mutex_unlock(&d->lock);
    kref_put(&d->refs, dot_free);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
    return 0;
#endif
}

/* Separate binding preserves the tutorial's debugdev and its original driver. */
static const struct of_device_id dot_match[] = {
    { .compatible = "ece382m,dotaccel" }, { }
};
MODULE_DEVICE_TABLE(of, dot_match);
static struct platform_driver dot_driver = {
    .probe = dot_probe,
    .remove = dot_remove,
    .driver = { .name = "dot_accel", .of_match_table = dot_match },
};
static struct platform_device *manual_device;
static int __init dot_init(void)
{
    struct resource resources[] = {
        DEFINE_RES_MEM(DOT_BASE_ADDR, DOT_REG_WINDOW),
        DEFINE_RES_MEM(DOT_BASE_ADDR + DOT_MEM_OFFSET, DOT_MEM_SIZE),
    };
    int ret;
    if (manual && use_irq)
        return -EINVAL; /* no guessed Linux IRQ numbers */
    ret = platform_driver_register(&dot_driver);
    if (ret)
        return ret;
    if (manual) {
        manual_device = platform_device_register_simple("dot_accel", -1,
                                      resources, ARRAY_SIZE(resources));
        if (IS_ERR(manual_device)) {
            ret = PTR_ERR(manual_device);
            platform_driver_unregister(&dot_driver);
            return ret;
        }
    }
    return 0;
}
static void __exit dot_exit(void)
{
    if (manual_device)
        platform_device_unregister(manual_device);
    platform_driver_unregister(&dot_driver);
}
module_init(dot_init);
module_exit(dot_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ECE382M Lab 3 team; tutorial reference: Andreas Gerstlauer");
MODULE_DESCRIPTION("Serialized Q8_0 dot product accelerator driver");
