//SPDX-Licence-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>

#define EDU_VENDOR_ID 0x1234
#define EDU_DEVICE_ID 0x1111

struct edu_device {
    struct pci_dev *pdev;
};

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

    return 0;
}

static void edu_remove(struct pci_dev *pdev) {
    dev_info(&pdev->dev, "remove\n");
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