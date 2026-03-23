#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>


static __exit void goodbye_world(void) {
	printk(KERN_INFO "goodbye_world\n");	
	return;
}

static __init int hello_world(void) {
	printk(KERN_INFO "hello_world\n");	
	return 0;
}


MODULE_LICENSE("GPL");
module_init(hello_world);
module_exit(goodbye_world);

