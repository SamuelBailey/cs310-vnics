/**
 * Header for VNIC device file
 * 
 * Copyright (C) 2020 Samuel Bailey
 */
#ifndef VNIC_H
#define VNIC_H

#include <linux/netdevice.h>

#define VNIC_TIMEOUT 5

void print_netdev_name(struct net_device *dev);
void vnic_init(struct net_device *dev);
void vnic_setup_packet_pool(struct net_device *dev);
int vnic_dev_init(struct net_device *dev);
int vnic_open(struct net_device *dev);
int vnic_release(struct net_device *dev);
netdev_tx_t vnic_xmit(struct sk_buff* skb, struct net_device *dev);
int debug_init(struct net_device *dev);


#endif