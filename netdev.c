#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

MODULE_AUTHOR("Samuel Bailey");
MODULE_LICENSE("Dual BSD/GPL");

void cleanup_netdev(void) {
    printk(KERN_ALERT "Initialising module");
}

int setup_netdev(void) {
    printk(KERN_ALERT "Unloading module");
    return 0;
}

module_init(setup_netdev);
module_exit(cleanup_netdev);
