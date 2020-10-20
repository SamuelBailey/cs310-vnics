#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

#include <linux/netdevice.h>

MODULE_AUTHOR("Samuel Bailey");
MODULE_LICENSE("Dual BSD/GPL");

/**
 * Command line arguments for loading the module
 * vnic_count : the number of vnics to instantiate on module load
 */
int vnic_count = 2;

module_param(vnic_count, int, 0644);


/**
 * Variables for storing module data
 */
//
// TODO: Change this to use heap allocated memory, so it can be variable
//
struct net_device* vnic_devs[2];


void cleanup_vnic_module(void) {
    printk(KERN_ALERT "Initialising module");
    printk(KERN_ALERT "Destroying %d devices", vnic_count);
}

int setup_vnic_module(void) {
    printk(KERN_ALERT "Unloading module");
    printk(KERN_ALERT "Creating %d devices", vnic_count);
    return 0;
}

module_init(setup_vnic_module);
module_exit(cleanup_vnic_module);
