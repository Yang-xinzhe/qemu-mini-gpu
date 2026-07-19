//SPDX-Licence-Identifier: GPL-2.0
#include "edu_uapi.h"
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define EDU_VENDOR_ID 0x1234
#define EDU_DEVICE_ID 0x11e8

/* EDU BAR0 register offsets */
#define EDU_REG_IDENTIFICATION      0x00
#define EDU_REG_LIVENESS            0x04
#define EDU_REG_FACTORIAL           0x08

#define EDU_REG_STATUS              0x20
#define EDU_REG_IRQ_STATUS          0x24

#define EDU_REG_IRQ_RAISE           0x60
#define EDU_REG_IRQ_ACK             0x64

#define EDU_REG_DMA_SOURCE          0x80
#define EDU_REG_DMA_DESTINATION     0x88
#define EDU_REG_DMA_COUNT           0x90
#define EDU_REG_DMA_COMMAND         0x98

struct edu_device {
    /* PCI */
    struct pci_dev *pdev;
    void __iomem *bar0;
    resource_size_t bar0_start;
    resource_size_t bar0_len;
    unsigned long bar0_flags;

    /* Character device */
    dev_t devt;
    struct cdev cdev;
    struct device *char_dev;

    /* Synchronization */
    struct mutex ioctl_lock;
};

static struct class *edu_class;

static int edu_open(struct inode *inode, struct file *file) {
    struct edu_device *edu;

    edu = container_of(inode->i_cdev, struct edu_device, cdev);

    file->private_data = edu;
    // dev_info(&edu->pdev->dev, "open: edu-gpu0\n");
    return 0;
}

static int edu_release(struct inode *inode, struct file *file){
    // struct edu_device *edu = file->private_data;
    // dev_info(&edu->pdev->dev, "release: edu-gpu0\n");
    return 0;
}

static long edu_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct edu_device *edu = file->private_data;
    struct edu_reg_io reg;
    long ret = 0;

    dev_dbg(&edu->pdev->dev, "ioctl cmd=0x%x, arg=0x%lx\n", cmd, arg);

    if(_IOC_TYPE(cmd) != EDU_IOCTL_MAGIC)
        return -ENOTTY;

    mutex_lock(&edu->ioctl_lock);

    switch(cmd) {
        case EDU_IOCTL_REG_READ:
            if(copy_from_user(&reg, (void __user *)arg, sizeof(reg))){
                ret = -EFAULT;
                break;
            }
            
            if(reg.offset > edu->bar0_len - sizeof(u32) || (reg.offset & 0x3)) { // 4 Bytes align
                ret = -EINVAL;
                break;
            }

            reg.value = readl(edu->bar0 + reg.offset);
            
            if(copy_to_user((void __user*)arg, &reg, sizeof(reg)))
                ret = -EFAULT;
            break;

        case EDU_IOCTL_REG_WRITE:
            if(copy_from_user(&reg, (void __user *)arg, sizeof(reg))) {
                ret = -EFAULT;
                break;
            }

            if(reg.offset > edu->bar0_len - sizeof(u32) || (reg.offset & 0x3)) {
                ret = -EINVAL;
                break;
            }

            writel(reg.value, edu->bar0 + reg.offset);

            break;
        
            
        default:
            ret = -ENOTTY;
            break;
    }
    mutex_unlock(&edu->ioctl_lock);
    return ret;
}

static const struct file_operations edu_fops = {
    .owner = THIS_MODULE,
    .open = edu_open,
    .release = edu_release,
    .unlocked_ioctl = edu_unlocked_ioctl,
};

static int edu_chrdev_register(struct edu_device *edu) {
    int ret;

    ret = alloc_chrdev_region(&edu->devt, 0, 1, "edu_gpu");
    if(ret)
        return ret;

    cdev_init(&edu->cdev, &edu_fops);
    edu->cdev.owner = THIS_MODULE;

    ret = cdev_add(&edu->cdev, edu->devt, 1);
    if(ret)
        goto err_unregister_region;

    edu->char_dev = device_create(edu_class, &edu->pdev->dev, edu->devt, edu, "edu_gpu0");

    if(IS_ERR(edu->char_dev)) {
        ret = PTR_ERR(edu->char_dev);
        edu->char_dev = NULL;
        goto err_cdev_del;
    }

    dev_info(&edu->pdev->dev, "register /dev/edu_gpu0 major=%u minor=%u \n", MAJOR(edu->devt), MINOR(edu->devt));
    return 0;

err_cdev_del:
    cdev_del(&edu->cdev);

err_unregister_region:
    unregister_chrdev_region(edu->devt, 1);
    return ret;
}

static void edu_chrdev_unregister(struct edu_device *edu) {
    if(edu->char_dev) 
        device_destroy(edu_class, edu->devt);

    cdev_del(&edu->cdev);
    unregister_chrdev_region(edu->devt, 1);
}

#ifdef MMIO_TEST
static u32 edu_read32(struct edu_device *edu, u32 offset) {
    if(offset > edu->bar0_len - sizeof(u32))
        return 0xffffffff;

    return readl(edu->bar0 + offset);
}

static void edu_write32(struct edu_device *edu, u32 offset, u32 value) {
    if(offset > edu->bar0_len - sizeof(u32))
        return ;

    return writel(value, edu->bar0 + offset);
}
#endif

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
    struct edu_device *edu;
    int ret;

    dev_info(&pdev->dev, "probe: vendor=%04x device=%04x\n", pdev->vendor, pdev->device);

    edu = devm_kzalloc(&pdev->dev, sizeof(*edu), GFP_KERNEL);
    if(!edu)
        return -ENOMEM;

    edu->pdev = pdev;
    pci_set_drvdata(pdev, edu);

    ret = pci_enable_device(pdev);
    if(ret) {
        dev_err(&pdev->dev, "pci_enable_device failed: %d\n", ret);
        return ret;
    }

    ret = pci_request_regions(pdev, "edu_drv");
    if(ret) {
        dev_err(&pdev->dev, "pci_request_regions failed: %d\n", ret);
        goto err_disable_device;
    }

    edu->bar0_len = pci_resource_len(pdev, 0);
    edu->bar0_start = pci_resource_start(pdev, 0);
    edu->bar0_flags = pci_resource_flags(pdev, 0);

    dev_info(&pdev->dev, "BAR0 start=%pa, len=%pa, flags=%lx\n", &edu->bar0_start, &edu->bar0_len, edu->bar0_flags);

    edu->bar0 = pci_iomap(pdev, 0, 0);
    if(!edu->bar0) {
        ret = -ENOMEM;
        goto err_release_region;
    }

    ret = edu_chrdev_register(edu);
    if(ret)
        return ret;

#ifdef MMIO_TEST
    u32 edu_id;
    u32 liveness;
    // BAR0 identification
    edu_id = edu_read32(edu, EDU_REG_IDENTIFICATION);
    printk("BAR0 identification: 0x%x\n", edu_id);
    // BAR0 liveness
    edu_write32(edu, EDU_REG_LIVENESS, 0x21524110);
    liveness = edu_read32(edu, EDU_REG_LIVENESS);
    printk("BAR0 liveness: 0x%x\n", liveness);
#endif

    return 0;
err_release_region:
    pci_release_regions(pdev);
err_disable_device:
    pci_disable_device(pdev);
    return ret;
}

static void edu_remove(struct pci_dev *pdev) {
    dev_info(&pdev->dev, "remove: edu\n");
    
    struct edu_device *edu = pci_get_drvdata(pdev);
    if(edu->bar0) {
        pci_iounmap(pdev, edu->bar0);
    }
    edu_chrdev_unregister(edu);
    pci_release_regions(pdev);
    pci_disable_device(pdev);
}

static const struct pci_device_id edu_pci_ids[] = {
    { PCI_DEVICE(EDU_VENDOR_ID, EDU_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, edu_pci_ids);

static struct pci_driver edu_pci_driver = {
    .name = "edu_drv",
    .id_table = edu_pci_ids,
    .probe = edu_probe,
    .remove = edu_remove,
};

static int __init edu_init(void) {
    int ret;
    edu_class = class_create("edu_class");
    if(IS_ERR(edu_class))
        return PTR_ERR(edu_class);

    ret = pci_register_driver(&edu_pci_driver);
    if(ret) {
        class_destroy(edu_class);
        edu_class = NULL;
        return ret;
    }
    return 0;
}

static void __exit edu_exit(void) {
    pci_unregister_driver(&edu_pci_driver);
    class_destroy(edu_class);
    edu_class = NULL;
}

module_init(edu_init);
module_exit(edu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yang xinzhe");
MODULE_DESCRIPTION("QEME EDU PCI training driver");
