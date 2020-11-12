#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>   // Using struct iphdr

#include "vnic.h"

MODULE_AUTHOR("Samuel Bailey");
MODULE_LICENSE("Dual BSD/GPL");

/**
 * Command line arguments for loading the module
 * vnic_count : the number of vnics to instantiate on module load
 * print_packet : bool, whether the packets should be printed on transmission
 */
static int vnic_count = 2;
static int print_packet = 0;

// TODO: Change this to work with as many addresses as required
// Done this way for proof of concept and getting things up and running

module_param(vnic_count, int, 0644);
module_param(print_packet, int, 0644);

/**
 * ===============================================================
 *               Variables for storing module data
 * ===============================================================
 */

struct vnic_packet {
    struct vnic_packet* next;
    struct net_device* dev;
    int datalen;
    u8 data[ETH_DATA_LEN];
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

// For debugging
static struct net_device* my_device;

// Doesn't contain a vnic_rx method
static struct net_device_ops my_ops = {
    .ndo_init = vnic_init,
    .ndo_open = vnic_open,
    .ndo_stop = vnic_release,
    .ndo_start_xmit = vnic_xmit,
};

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
        printk("NULL");
    } else {
        if (dev->name[0] == '\0') {
            printk("Empty");
        } else {
            printk("%s", dev->name);
        }
    }
}

// Prints the source and destination addresses pointed to by saddr and daddr
void print_ip_addresses(u32 *saddr, u32 *daddr) {
    u32 host_saddr, host_daddr;
    host_saddr = ntohl(*saddr);
    host_daddr = ntohl(*daddr);
    printk("vnic: saddr: %pI4, daddr: %pI4 \n", &host_saddr, &host_daddr);
}

/**
 * Init function for VNICs
 * This will only be called if using alloc_netdev() instead of alloc_etherdev()
 */
int vnic_init(struct net_device* dev) {
    struct vnic_priv* priv;

    // Assign some fields of the device
    ether_setup(dev);
    printk("vnic: VNIC name: ");
    print_netdev_name(dev);
    printk("\n");

    priv = netdev_priv(dev);

    // Zero out private memory
    memset(priv, 0, sizeof(struct vnic_priv));

    // Enable receiving of interrupts
    priv->rx_int_enabled = 1;

    printk("vnic: vnic_init()\n");
    return 0;
}

int vnic_dev_init(struct net_device* dev) {
    printk("vnic: vnic_dev_init()");
    return 0;
}

/**
 * Stub for open
 * Sets the MAC address for the device
 */
int vnic_open(struct net_device* dev) {

    static unsigned char value = 0x00;

    printk("vnic: vnic_open called\n");
    // Start with a \0 to indicate not multicast address
    unsigned char address[ETH_ALEN] = "\0VNIC0";
    address[ETH_ALEN-1] = value++;
    memcpy(dev->dev_addr, address, ETH_ALEN);
    printk(KERN_ALERT "vnic: opening device %pMF", dev->dev_addr);
    netif_start_queue(dev);
    return 0;
}

/**
 * Stub for release
 */
int vnic_release(struct net_device* dev) {
    printk("vnic: vnic_release called\n");
    netif_stop_queue(dev);
    return 0;
}

/**
 * Handles the actual transferring of data from one vnic to another
 * Returns bool. 1 if successful, 0 if not.
 * 
 * This is in place of actually connecting to a hardware device
 * 
 * TODO: Find the destination address of the packet and pass into the function
 */
int vnic_transfer(char *buf, int len, struct net_device *dev) {
    // Make a lookup table for which IP to send data to
    struct net_device *dest_dev;
    struct iphdr *iph;
    u32 *saddr, *daddr;

    // From LDD3 snull. Make sure the packet is long enough to extract an ethernet and ip header
    if (len < sizeof(struct ethhdr) + sizeof(struct iphdr)) {
        printk(KERN_ALERT "Dropped packet due to it being too small to contain ethernet and ip headers\n");
        return 0;
    }

    // Print the source and destination ip addresses of the packet
    iph = (struct iphdr *)(buf + sizeof(struct ethhdr));
    saddr = &iph->saddr;
    daddr = &iph->daddr;
    print_ip_addresses(saddr, daddr);

    // Print the data of the packet if PRINT_PACKET is enabled
    if (print_packet) {
        int i;
        printk("Length of packet: %i\ndata:", len);
        for (i=14 ; i<len; i++)
			printk(KERN_CONT " %02x",buf[i]&0xff);
		printk(KERN_CONT "\n");
    }

    // Very simple implementation - if vnic0, send to vnic1, else send to vnic0
    if (strcmp(dev->name, vnic_devs[0]->name) == 0) {
        dest_dev = vnic_devs[1];
    } else {
        dest_dev = vnic_devs[0];
    }

    // Check that dest is up
    if (!(dest_dev->flags & IFF_UP)) {
        // fail
        printk("vnic: %s failed to send packet\n", dev->name);
        return 0;
    }

    // Send the packet - invoke the interrupt on the other device

    printk("vnic: %s sent packet to %s\n", dev->name, dest_dev->name);
    return 1;
}

/**
 * Method for transmit
 */
netdev_tx_t vnic_xmit(struct sk_buff* skb, struct net_device* dev) {
    int length;
    char *data, shortpacket[ETH_ZLEN];
    struct vnic_priv *priv = netdev_priv(dev);
    struct iphdr *iph;

    // Check whether queue is stopped
    if (netif_queue_stopped(dev))
        return NETDEV_TX_BUSY;

    // Stop the queue - other calls will know it is busy
    netif_stop_queue(dev);
    
    printk("vnic: %s vnic_transmit function called\n", dev->name);

    length = skb->len;
    data = skb->data;

    // pad short packets with 0s
    if (skb->len < ETH_ZLEN) {
        memset(shortpacket, 0, ETH_ZLEN);
        memcpy(shortpacket, skb->data, skb->len);
        length = ETH_ZLEN;
        data = shortpacket;
    }

    // Obtain the lock, so that data can be sent without being interrupted
    spin_lock_irq(&priv->lock);

    // Save timestamp for start of transmission
    netif_trans_update(dev);

    // This memory gets freed during interrupt after sending. Need to store a reference to it to free it
    priv->skb = skb;

    // dev_kfree_skb(skb);
    if (vnic_transfer(data, length, dev)) {
        dev_kfree_skb(skb);

        goto finish_ok;
    }

    // TEMPORARY BEHAVIOUR - Drop the packet whilst destination is not set up
    goto finish_ok;

finish_ok:
    dev_kfree_skb(skb);
    spin_unlock_irq(&priv->lock);
    netif_wake_queue(dev);
    return NETDEV_TX_OK;

finish_busy:
    spin_unlock_irq(&priv->lock);
    netif_wake_queue(dev);
    return NETDEV_TX_BUSY;
}

int debug_init(struct net_device* dev) {
    ether_setup(dev);

    struct net_device_ops* ops = kmalloc(sizeof(struct net_device_ops), GFP_KERNEL);
    ops->ndo_open = vnic_open;
    ops->ndo_stop = vnic_release;
    ops->ndo_start_xmit = vnic_xmit;

    // dev->netdev_ops->ndo_open = vnic_open;
    // dev->netdev_ops->ndo_stop = vnic_release;
    return 0;
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
            unregister_netdev(vnic_devs[i]);

            // free memory allocated to the device in the kernel
            free_netdev(vnic_devs[i]);
        }
    }
    // unregister_netdev(my_device);
    // free_netdev(my_device);
    kfree(vnic_devs);
}

/**
 * INIT function for loading the module into the kernel
 */
int setup_vnic_module(void) {
    int i;
    struct vnic_priv* priv;
    int result;

    // Instantiate the array of net_devices
    vnic_devs = kmalloc_array(vnic_count, sizeof(struct net_device*), GFP_KERNEL);

    printk("vnic: Initialising module\n");
    printk("vnic: Creating %d devices\n", vnic_count);

    // Allocate memory for all vnic devices
    for (i = 0; i < vnic_count; i++) {

        // alloc_netdev allows the setting of name, but passed ether_setup to set ethernet values
        vnic_devs[i] = alloc_netdev(sizeof(struct vnic_priv), "vnic%d", NET_NAME_ENUM, ether_setup);
        if (vnic_devs[i] == NULL) {
            printk(KERN_ALERT "vnic: Unable to allocate space for vnic %d\n", i);
            cleanup_vnic_module();
            return -ENOMEM;
        }
        
        vnic_devs[i]->netdev_ops = &my_ops;
        printk("vnic: Base address for device %pMF\n", &vnic_devs[i]->base_addr);
        printk("vnic: Broadcase address = %pMF\n", &vnic_devs[i]->broadcast);
        printk("vnic: addr_len = %hhx\n", vnic_devs[i]->addr_len);
        printk("vnic: dev_addr = %pMF\n", vnic_devs[i]->dev_addr);
    }

    printk(KERN_ALERT "vnic: Set netdev_ops for my_device\n");

    // Register all vnic devices
    for (i = 0; i < vnic_count; i++) {
        if (register_netdev(vnic_devs[i])) {
            printk(KERN_ALERT "vnic: Error - failed to register device %d\n", i);
        } else {
            printk("vnic: Successfully registered device %d\n", i);
        }
    }

    // if (register_netdev(my_device)) {
    //     printk("vnic: ERROR - failed to register device.\n");
    // } else {
    //     printk("vnic: SUCCESS!");
    // }

    return 0;
}

module_init(setup_vnic_module);
module_exit(cleanup_vnic_module);
