/**
 * Header for VNIC device file
 * 
 * Copyright (C) 2020 Samuel Bailey
 */
#ifndef VNIC_H
#define VNIC_H

#include <linux/netdevice.h>

#define VNIC_TIMEOUT 5
// TODO: remove hard coded definition of HASH_BITS
#define MY_HASH_BITS 5
#define MAX_VNICS (1 << MY_HASH_BITS)

#define DEBUG_ON
#ifdef DEBUG_ON
    // Log message to kernal logs if DEBUG_ON is defined
    #define LOG(message) printk("vnic: "); printk(KERN_CONT message)
#else
    // Log message to kernal logs if DEBUG_ON is defined
    #define LOG(message)
#endif

struct vnic_packet {
    struct vnic_packet *next;
    struct net_device *dev;
    int datalen;
    u8 data[ETH_DATA_LEN];
};

void print_netdev_name(struct net_device *dev);
void vnic_init(struct net_device *dev);
int vnic_header(struct sk_buff *skb, struct net_device *dev,
			   unsigned short type, const void *daddr,
			   const void *saddr, unsigned int len);
void vnic_setup_packet_pool(struct net_device *dev);
int vnic_dev_init(struct net_device *dev);
int vnic_open(struct net_device *dev);
int vnic_release(struct net_device *dev);
netdev_tx_t vnic_xmit(struct sk_buff *skb, struct net_device *dev);
void vnic_rx(struct net_device *dev, struct sk_buff *skb);
int debug_init(struct net_device *dev);


#endif