//SPDX-Licence-Identifier: GPL-2.0
#include "asm-generic/errno-base.h"
#include "linux/dev_printk.h"
#include "linux/types.h"
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>

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
    struct pci_dev *pdev;
    void __iomem *bar0;
    resource_size_t bar0_start;
    resource_size_t bar0_len;
    unsigned long bar0_flags;
};

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

#if 0
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
    
    //TODO: if(edu->bar0) how to get edu in remove? pci_iounmap
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

module_pci_driver(edu_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yang xinzhe");
MODULE_DESCRIPTION("QEME EDU PCI training driver");