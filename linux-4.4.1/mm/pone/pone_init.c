#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/cpumask.h>
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include <pone/pone.h>
#include "splitter_adp.h"
#include <pone/virt_release.h>
int pone_sysfs_init(void);
extern void slice_debug_area_init(void);
static int __init merge_mem_init(void)
{
	slice_per_cpu_count_init();
	slice_debug_area_init();
	pone_case_init();
	
	if(0 != slice_state_control_init())
	{
		printk("slice_state_control_init err \r\n");
		return -1;
	}
	virt_mem_release_init();
	slice_merge_timer_init(50);
	pone_sysfs_init();
	printk("slice_control_init \r\n");
}

subsys_initcall(merge_mem_init);

