#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include "vnic.h"

MODULE_AUTHOR("Samuel Bailey");
MODULE_LICENSE("Dual BSD/GPL");

/**
 * Command line arguments for loading the module
 * vnic_count : the number of vnics to instantiate on module load
 */
int vnic_count = 2;

module_param(vnic_count, int, 0644);


/**
 * ===============================================================
 *               Variables for storing module data
 * ===============================================================
 */

// TODO: Define vnic_packet
struct vnic_packet {

};

/**
 * Private structure for each device that is instantiated
 * Used for passing packets in and out.
 * 
 * Adapted from `snull.c` in Linux Device Drivers 3rd Ed.
 */
struct vnic_priv {
    struct net_device_stats stats;
    int status;
    struct vnic_packet* ppool;
    struct vnic_packet* rx_queue; /* List of incoming packets */
    int rx_int_enabled;
    int tx_packetlen;
    u8* tx_packetdata;
    struct sk_buff* skb;
    spinlock_t lock;
    struct net_device* dev;
    struct napi_struct napi;
};

//
// TODO: Change this to use heap allocated memory, so it can be variable
//

/**
 * vnic_devs is an array of pointers to net_devices. Each net_device is allocated with 
 * alloc_netdev() or alloc_etherdev()
 * 
 * Copyright (C) 2020 Samuel Bailey
 */
static struct net_device** vnic_devs;

/**
 * ===============================================================
 *                         Module methods
 * ===============================================================
 */

/**
 * For debugging
 */
void print_netdev_name(struct net_device* dev) {
    if (dev->name == NULL) {
        printk("vnic: Name is NULL\n");
    } else {
        if (dev->name[0] == '\0') {
            printk("vnic: Name is empty");
        } else {
            printk("vnic: Name is %s\n", dev->name);
        }
    }
}

/**
 * Init function for VNICs
 * This will only be called if using alloc_netdev() instead of alloc_etherdev()
 */
void vnic_init(struct net_device* dev) {
    struct vnic_priv* priv;

    // Assign some fields of the device
    print_netdev_name(dev);
    ether_setup(dev);
    print_netdev_name(dev);

    priv = netdev_priv(dev);

    // Zero out private memory
    memset(priv, 0, sizeof(struct vnic_priv));

    // Enable receiving of interrupts
    priv->rx_int_enabled = 1;

    printk("vnic: vnic_init()\n");
}

/**
 * EXIT functions for unloading the module
 */
void cleanup_vnic_module(void) {
    int i;

    printk("vnic: Unloading module\n");
    printk("vnic: Destroying %d devices\n", vnic_count);

    for (i = 0; i < vnic_count; i++) {
        if (vnic_devs[i]) {
            printk("vnic: Cleaning up device %d\n", i);
            
            // Unregister device to stop it being used
            // unregister_netdev(vnic_devs[i]);

            // free memory allocated to the device in the kernel
            free_netdev(vnic_devs[i]);
        }
    }
    kfree(vnic_devs);
}

/**
 * INIT function for loading the module into the kernel
 */
int setup_vnic_module(void) {
    int i;

    // Instantiate the array of net_devices
    vnic_devs = kmalloc_array(vnic_count, sizeof(struct net_device*), GFP_KERNEL);

    printk("vnic: Initialising module\n");
    printk("vnic: Creating %d devices\n", vnic_count);

    // Load the required number of devices
    for (i = 0; i < vnic_count; i++) {
        printk("vnic: alloc_netdev, device number: %d\n", i);
        // TODO: Change to alloc_etherdev() for an ethernet device
        // vnic_devs[i] = alloc_netdev(sizeof(struct vnic_priv), "vnic%d", NET_NAME_UNKNOWN, vnic_init);

        // Allocate an ethernet device
        // if (!(vnic_devs[i] = alloc_etherdev(sizeof(struct vnic_priv)))) {
        if (!(vnic_devs[i] = alloc_netdev(sizeof(struct vnic_priv), "vnic%d", NET_NAME_UNKNOWN, vnic_init))) {
            printk(KERN_ALERT "vnic: Could not allocate etherdev %d", i);
            cleanup_vnic_module();
            return 1;
        }

        struct vnic_priv* vnic_priv_mem = netdev_priv(vnic_devs[i]);
    }

    // Check that all devices were allocated successfully
    for (i = 0; i < vnic_count; i++) {
        if (vnic_devs[i] == NULL) {
            printk(KERN_ALERT "vnic: WARNING unable to create device %d\n", i);
            cleanup_vnic_module();
            return 1;
        }
    }



    // Register the net device

    return 0;
}

module_init(setup_vnic_module);
module_exit(cleanup_vnic_module);
