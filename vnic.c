#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>   // Using struct iphdr
#include <linux/hash.h>

#include "vnic.h"

MODULE_AUTHOR("Samuel Bailey");
MODULE_LICENSE("Dual BSD/GPL");

/**
 * Command line arguments for loading the module
 * vnic_count : the number of vnics to instantiate on module load
 * print_packet : bool, whether the packets should be printed on transmission
 * pool_size : int, the number of packets that the pool should be able to contain.
 */
static int vnic_count = 2;
static int print_packet = 0;
static int pool_size = 8;
static char *ip_mappings[MAX_VNICS] = {"192.168.0.1", "192.168.1.2"};


// TODO: Change this to work with as many addresses as required
// Done this way for proof of concept and getting things up and running

// module_param(vnic_count, int, 0644);
module_param(print_packet, int, 0644);
module_param(pool_size, int, 0644);
module_param_array(ip_mappings, charp, &vnic_count, 0644);

/**
 * ===============================================================
 *               Variables for storing module data
 * ===============================================================
 */


/**
 * Private structure for each device that is instantiated
 * Used for passing packets in and out.
 * 
 * Originally from `snull.c` in Linux Device Drivers 3rd Ed.
 */
struct vnic_priv {
    struct net_device_stats stats;
    int status;
    struct vnic_packet *ppool;
    struct vnic_packet *rx_queue; /* List of incoming packets */
    int rx_int_enabled;
    int tx_packetlen;
    u8 *tx_packetdata;
    struct sk_buff *skb;
    spinlock_t lock;
    struct net_device *dev;
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

static struct net_device **vnic_devs;

static const struct header_ops my_header_ops = {
    .create = vnic_header
};

// Doesn't contain a vnic_rx method
static const struct net_device_ops my_ops = {
    // .ndo_init = vnic_init,
    .ndo_open = vnic_open,
    .ndo_stop = vnic_release,
    .ndo_start_xmit = vnic_xmit,
};

/**
 * ===============================================================
 *                         Helper methods
 * ===============================================================
 */

// Takes a dotted decimal IP address, terminated by a null character, and returns the IP address
// as an unsigned int
uint32_t ip_addr_str_to_int(const char *input) {
    u32 output = 0;
    int pos = 0;
    u32 decimal = 0;
    // Used to define the start and end of each dotted decimal value in the address
    int start_index = 0;
    int end_index = 0;

    do {
        if (input[pos] == '.' || input[pos] == '\0') {
            // each decimal number is max size 3. Allocate 4 to allow for null termination character
            char *temp_str = kcalloc(4, sizeof(char), GFP_KERNEL);

            end_index = pos - 1;
            memcpy(temp_str, input + start_index, (end_index - start_index + 1) * sizeof(char));
            kstrtou32(temp_str, 10, &decimal);
            output = (output * 1<<8) + decimal;
            start_index = pos + 1;
            kfree(temp_str);
        }
    } while (input[pos++] != '\0');

    return output;
}

struct net_device_addr {
    u32 ip_addr;
    struct net_device *device;
};

static int lookup_table_len;
static struct net_device_addr *ip_addr_lookup_table;
static int hash_bits;

/**
 * Creates an empty lookup table of IP addresses to net_device structures for fast access
 * This is allocated on the heap, so remember to free the memory on closure.
 * 
 * Uses the global ip_addr_lookup_table to be accessible anywhere in the module
 */
void setup_hash_table(void) {
    int i;
    // ip_addr of null_device is undefined
    struct net_device_addr null_device = {
        .device = NULL
    };

    hash_bits = fls((vnic_count * 3) / 2);
    // lookup_table_len is the first power of 2 after vnic_count * 1.5
    lookup_table_len = 1 << hash_bits;

    ip_addr_lookup_table = kmalloc_array(lookup_table_len, sizeof(struct net_device_addr), GFP_KERNEL);

    for (i = 0; i < lookup_table_len; i++) {
        ip_addr_lookup_table[i] = null_device;
    }
}

/**
 * Frees the ip_addr_lookup_table
 */
void free_hash_table(void) {
    LOG("Freed ip_addr_lookup_table\n");
    kfree(ip_addr_lookup_table);
}

/**
 * Stores a reference to the given device in the hash table
 * 
 * returns 1 for success, 0 for failure to insert
 */
int add_dev_to_hash_table(u32 ip_addr, struct net_device *dev) {
    u32 index = hash_32(ip_addr, hash_bits); // The size of the lookup table is dependent on HASH_BITS, so this will not overflow
    int attempts = 0;
    while (ip_addr_lookup_table[index].device && attempts < lookup_table_len) {
        index = (index + 1) % lookup_table_len;
        attempts++;
    }
    if (!ip_addr_lookup_table[index].device) {
        ip_addr_lookup_table[index].device = dev;
        ip_addr_lookup_table[index].ip_addr = ip_addr;
        return 1;
    } else {
        return 0;
    }
}

/**
 * Returns a pointer to the net device with the defined IP address.
 * Pointer is NULL if no device exists with the IP address
 */
struct net_device *get_dev_from_hash_table(u32 ip_addr) {
    u32 index = hash_32(ip_addr, hash_bits);
    int attempts = 0;

    printk("vnic: ==========Looking for %pI4h=========\n", &ip_addr);
    printk("vnic: hash = %u", index);

    while (ip_addr_lookup_table[index].device && attempts++ < lookup_table_len) {
        if (ip_addr_lookup_table[index].ip_addr == ip_addr) {
            printk("vnic: Found %pI4h after %d attempts\n", &ip_addr, attempts);
            printk("vnic: index = %u, (max %d)\n", index, lookup_table_len);
            return ip_addr_lookup_table[index].device;
        }
        index = (index + 1) % lookup_table_len;
    }
    return NULL;
}

/**
 * For debugging
 */
void print_netdev_name(struct net_device *dev) {
    if (dev->name == NULL) {
        printk(KERN_CONT "NULL");
    } else {
        if (dev->name[0] == '\0') {
            printk(KERN_CONT "Empty");
        } else {
            printk(KERN_CONT "%s", dev->name);
        }
    }
}

// Prints the source and destination addresses pointed to by saddr and daddr
// saddr and daddr must be in Network byte order
void print_ip_addresses_n(u32 *saddr, u32 *daddr) {
    printk("vnic: saddr: %pI4, daddr: %pI4 \n", saddr, daddr);
}

// Prints the source and destination addresses pointed to by saddr and daddr
// saddr and daddr must be in Host byte order
void print_ip_addresses_h(u32 *saddr, u32 *daddr) {
    printk("vnic: saddr: %pI4h, daddr: %pI4h \n", saddr, daddr);
}

/**
 * ===============================================================
 *                         Module methods
 * ===============================================================
 */

/**
 * Init function for VNICs
 * This will only be called if using alloc_netdev() instead of alloc_etherdev()
 */
void vnic_init(struct net_device *dev) {
    struct vnic_priv *priv;

    // Assign some fields of the device
    ether_setup(dev);
    printk("vnic: VNIC name: ");
    print_netdev_name(dev);
    printk(KERN_CONT "\n");

    dev->flags |= IFF_NOARP;
    dev->features |= NETIF_F_HW_CSUM;

    priv = netdev_priv(dev);

    // Zero out private memory
    memset(priv, 0, sizeof(struct vnic_priv));

    // Enable receiving of interrupts
    priv->rx_int_enabled = 1;

    vnic_setup_packet_pool(dev);

    printk("vnic: vnic_init()\n");
}

int vnic_header(struct sk_buff *skb, struct net_device *dev,
                unsigned short type, const void *daddr,
                const void *saddr, unsigned int len) {
    //
    // Copied from SNULL - needs modifying to direct to MAC addresses of multiple VNICs
    //

    struct ethhdr *eth = (struct ethhdr *)skb_push(skb, ETH_HLEN);
    // struct sk_buff *ip_skb = ((void *)skb) + ETH_HLEN;
    struct iphdr *iph;
    u32 ip_dest;
    struct net_device *dest_dev;

    // Set the protocol
    eth->h_proto = htons(type);
    // If saddr or daddr is null, set to the addres of this device, else don't change
    memcpy(eth->h_source, saddr ? saddr : dev->dev_addr, dev->addr_len);
    memcpy(eth->h_dest, daddr ? daddr : dev->dev_addr, dev->addr_len);

    // Find the destination IP address of the packet, in order to determine the MAC address to route to
    iph = ip_hdr(skb);
    ip_dest = ntohl(iph->daddr);
    dest_dev = get_dev_from_hash_table(ip_dest);
    if (!dest_dev) {
        printk(KERN_ALERT "Couldn't find device with ip addr %pI4\n", &(iph->daddr));
        return (dev->hard_header_len);
    }
    printk(KERN_ALERT "Setting destination address %pM, ip addr: %pI4\n", dest_dev->dev_addr, &iph->daddr);
    memcpy(eth->h_dest, dest_dev->dev_addr, dev->addr_len);

    // // Set MAC dest addr len in header to the other VNIC - needs updating for final project
    // eth->h_dest[ETH_ALEN - 1] ^= 0x01; // XOR last bit of address - Change this
    return (dev->hard_header_len);
}

/**
 * Sets up an area of private memory for each device to store
 * packets
 */
void vnic_setup_packet_pool(struct net_device *dev) {
    struct vnic_priv *priv = netdev_priv(dev);
    int i;
    struct vnic_packet *allocated_packet; // Empty packet structure for allocating memory

    priv->ppool = NULL;
    for (i = 0; i < pool_size; i++) {
        allocated_packet = kmalloc(sizeof(struct vnic_packet), GFP_KERNEL);
        if (allocated_packet == NULL) {
            printk(KERN_ALERT "Ran out of memory allocating packet pool");
            return;
        }
        // Set up a linked list - Each packet will point to the next in the pool
        allocated_packet->dev = dev;
        allocated_packet->next = priv->ppool;
        // Now there is a pointer to the allocated memory, move the ppool pointer
        // back one packet, to include it in the linked list
        priv->ppool = allocated_packet;
    }
}

void vnic_teardown_packet_pool(struct net_device *dev) {
    struct vnic_priv *priv = netdev_priv(dev);
    struct vnic_packet *curr_packet = priv->ppool;
    struct vnic_packet *next_packet;

    while (curr_packet != NULL) {
        next_packet = curr_packet->next;
        kfree(curr_packet);
        curr_packet = next_packet;
    }
    priv->ppool = NULL;
}

int vnic_dev_init(struct net_device *dev) {
    printk("vnic: vnic_dev_init()");
    return 0;
}

/**
 * Stub for open
 * Sets the MAC address for the device
 */
int vnic_open(struct net_device *dev) {

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
int vnic_release(struct net_device *dev) {
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

    // Copied from snull
    // Enable to look at the data
    if (1) {
        int i;
        printk(KERN_DEBUG "len is %i\n data:",len);
        for (i=0; i < 14; i++) {
            printk(KERN_CONT " %02x", buf[i]&0xff);
        }
        printk("rest:");
        for (i=14 ; i<len; i++)
            printk(KERN_CONT " %02x",buf[i]&0xff);
        printk("\n");
	}

    // Print the source and destination ip addresses of the packet
    iph = (struct iphdr *)(buf + sizeof(struct ethhdr));
    saddr = &iph->saddr;
    daddr = &iph->daddr;
    print_ip_addresses_n(saddr, daddr);

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
netdev_tx_t vnic_xmit(struct sk_buff *skb, struct net_device *dev) {
    int length;
    char *data, shortpacket[ETH_ZLEN];
    struct vnic_priv *priv = netdev_priv(dev);
    struct iphdr *iph;
    struct net_device *dest_dev;
    u32 dest_addr;
    
    // Get the IP address header
    // printk(KERN_ALERT "Getting the IP addresses\n");
    iph = ip_hdr(skb);
    // print_ip_addresses_n(&iph->saddr, &iph->daddr);

    printk("\n\n");

    printk("vnic: ============================================================================\n");
    printk("vnic: Transmitting a new packet from %s\n", dev->name);


    length = skb->len;
    data = skb->data;


    // pad short packets with 0s
    if (skb->len < ETH_ZLEN) {
        memset(shortpacket, 0, ETH_ZLEN);
        memcpy(shortpacket, skb->data, skb->len);
        length = ETH_ZLEN;
        data = shortpacket;
        memcpy(skb->data, data, ETH_ZLEN);
        skb->len = length;
    }
    // Save timestamp for start of transmission
    netif_trans_update(dev);

    // This memory gets freed during interrupt after sending. Need to store a reference to it to free it
    priv->skb = skb;

    // If the source address is NOT the network simulator, send it to the network simulator.
    // Otherwise, send the 
    dest_dev = get_dev_from_hash_table(ntohl(iph->daddr));
    if (!dest_dev) {
        // printk(KERN_ALERT "Dropped packet\n");
        // Drop the packet if destination is null
        dev_kfree_skb(skb);
        return NETDEV_TX_OK;
    }
    printk("Transmitting packet\n");
    // Otherwise, send the packet to the selected device
    vnic_rx(dest_dev, skb);

    return NETDEV_TX_OK;

    // dev_kfree_skb(skb);
    // if (vnic_transfer(data, length, dest_dev)) {
    //     // dev_kfree_skb(skb);
    //     return NETDEV_TX_OK;
    // }
    // DONT FREE THE DATA IF THE DEVICE IS BUSY
    // dev_kfree_skb(skb);
    // return NETDEV_TX_OK;
    // return NETDEV_TX_BUSY;
}

void vnic_rx(struct net_device *dev, struct sk_buff *skb) {
    // struct sk_buff *skb;
    struct vnic_priv *priv = netdev_priv(dev);
    char *buf = skb->data;
    struct iphdr *iph;
    u32 *saddr, *daddr;

    // Need to build a socket buffer for the packet to be placed in, so
    // it can be passed to upper levels.

    printk("vnic: Receiving packet on device %s\n", dev->name);

    //
    // We don't need to assign a buffer, because this has come from an internal
    // function instead of a real card. Therefore the packet is already in memory
    // The free has been removed from the xmit function, so the buffer will remain
    // in memory.
    //
    if (1) {
        int i;
        printk(KERN_DEBUG "len is %i\n data:",skb->len);
        for (i=0; i < 14; i++) {
            printk(KERN_CONT " %02x", buf[i]&0xff);
        }
        printk("rest:");
        for (i=14 ; i<skb->len; i++)
            printk(KERN_CONT " %02x",buf[i]&0xff);
        printk("\n");
	}

    //
    // Following snull - change the source and destination addresses
    //

    iph = (struct iphdr *)(buf + sizeof(struct ethhdr));
    // saddr = &iph->saddr;
    // daddr = &iph->daddr;
    // ((u8 *)saddr)[2] ^= 1;
    // ((u8 *)daddr)[2] ^= 1;
    // iph->check = 0;         /* and rebuild the checksum (ip needs it) */
	// iph->check = ip_fast_csum((unsigned char *)iph,iph->ihl);

    saddr = &iph->saddr;
    daddr = &iph->daddr;
    print_ip_addresses_n(saddr, daddr);


    // Copy the data back into the skb
    memcpy(skb->data, buf, skb->len);

    printk("vnic: === Receiving packet ===\n");

    skb->dev = dev;
    skb->protocol = eth_type_trans(skb, dev);
    skb->ip_summed = CHECKSUM_UNNECESSARY; // From snull - don't check the checksum
    netif_rx(skb);
}

int debug_init(struct net_device *dev) {
    ether_setup(dev);

    struct net_device_ops *ops = kmalloc(sizeof(struct net_device_ops), GFP_KERNEL);
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
            vnic_teardown_packet_pool(vnic_devs[i]);
            // free memory allocated to the device in the kernel
            free_netdev(vnic_devs[i]);
        }
    }
    // unregister_netdev(my_device);
    // free_netdev(my_device);
    kfree(vnic_devs);
    free_hash_table();

    // Create visible break in kernel output
    printk("vnic: \n\n\n");
}

/**
 * INIT function for loading the module into the kernel
 */
int setup_vnic_module(void) {
    int i;
    struct vnic_priv *priv;
    int result;

    printk(KERN_ALERT "vnic: Listing devices to setup\n");
    for (i = 0; i < vnic_count; i++) {
        printk("%s \n", ip_mappings[i]);
    }

    // Instantiate the array of net_devices
    vnic_devs = kmalloc_array(vnic_count, sizeof(struct net_device*), GFP_KERNEL);

    printk("vnic: Initialising module\n");
    printk("vnic: Creating %d devices\n", vnic_count);

    // Allocate memory for all vnic devices
    for (i = 0; i < vnic_count; i++) {

        // alloc_netdev allows the setting of name, but passed ether_setup to set ethernet values
        vnic_devs[i] = alloc_netdev(sizeof(struct vnic_priv), "vnic%d", NET_NAME_ENUM, vnic_init);
        if (vnic_devs[i] == NULL) {
            printk(KERN_ALERT "vnic: Unable to allocate space for vnic %d\n", i);
            cleanup_vnic_module();
            return -ENOMEM;
        }
        vnic_devs[i]->netdev_ops = &my_ops;
        vnic_devs[i]->header_ops = &my_header_ops;
    }

    // Setup hashtable of ip addresses to net_devices
    setup_hash_table();
    for (i = 0; i < vnic_count; i++) {
        u32 ip_addr = ip_addr_str_to_int(ip_mappings[i]);
        add_dev_to_hash_table(ip_addr, vnic_devs[i]);
    }


    printk(KERN_ALERT "vnic: Set netdev_ops for my_device\n");

    // Register all vnic devices
    for (i = 0; i < vnic_count; i++) {
        if ((result = register_netdev(vnic_devs[i]))) {
            printk(KERN_ALERT "vnic: Error - failed to register device %d\n", i);
            cleanup_vnic_module();
            return result;
        } else {
            printk("vnic: Successfully registered device %d\n", i);
        }
    }

    // For testing purposes - check that vnics are present in the hash table
    for (i = 0; i < vnic_count; i++) {
        u32 ip_addr = ip_addr_str_to_int(ip_mappings[i]);
        printk("vnic: Finding vnic with address %pI4h : %s\n", &ip_addr, get_dev_from_hash_table(ip_addr)->name);
    }

    return 0;
}

module_init(setup_vnic_module);
module_exit(cleanup_vnic_module);
