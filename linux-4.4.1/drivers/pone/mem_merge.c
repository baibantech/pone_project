#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/threads.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/cpumask.h>
#include "mem_merge.h"
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include <pone/pone.h>

MODULE_LICENSE("GPL");
dev_t merge_dev;
int merge_dev_major = 0,merge_dev_minor = 0;
struct cdev *merge_cdev = NULL;
struct class *merge_class = NULL;
extern void pone_sysfs_init(void);

int merge_dev_open(struct inode *inode,struct file *filp)
{
    printk("open merge dev file\r\n");
    return 0;
}
static size_t merge_dev_read(struct file *filp,char __user *buf,size_t size,loff_t *ppos)
{
    return 0;
}

static size_t merge_dev_write(struct file *filp,const char __user *buf,size_t size,loff_t *ppos)
{
    return 0;
}

void print_vma_detail(struct vm_area_struct *vma)
{
    if(!vma)
    {
        return;
    }
    
    printk("\r\n-------------------------------------\r\n");
    printk("vma start is 0x%llx\r\n",vma->vm_start); 
    printk("vma end   is 0x%llx\r\n",vma->vm_end);
    printk("vma flags is 0x%llx\r\n",vma->vm_flags);
    printk("vma ops   is %p \r\n",   vma->vm_ops);
    printk("vma file  is %p \r\n",   vma->vm_file);

}


int merge_dev_ioctl(struct file *filp,unsigned int cmd,unsigned long args)
{
    printk("enter ioctl\r\n");
    struct mm_struct *mm = current->mm;
    struct vm_area_struct *vma;
    switch(cmd)
    {
        case MERGE_IOC_SHOW_VMA:
            {
                vma  = find_vma(mm,0);
                for (; vma; vma = vma->vm_next)
                {
                    print_vma_detail(vma);
                }    


                printk("show current process vma\r\n");
            }       


            break;
        default : printk("error cmd number\r\n");break;
    }
    return 0;
}


int merge_dev_mmap(struct file *filp,struct vm_area_struct *vm)
{
    #if 0
    unsigned long addr = vm->vm_start;
    unsigned long collector_id =vm->vm_pgoff;
    wireway_collector *collector = cache_id_lookup(collector_cache,collector_id);
    if(collector)
    {
        unsigned long que_addr = collector->rcv_queue;
        unsigned long que_phy_addr = virt_to_phys(que_addr);
        unsigned long size = vm->vm_end - vm->vm_start; 
        if(remap_pfn_range(vm,addr,que_phy_addr>>PAGE_SHIFT,size,PAGE_SHARED))
        return -1;  

    }
    #endif
    return 0;
}

static const struct file_operations merge_dev_fops = 
{
    .owner = THIS_MODULE,
    .read =  merge_dev_read,
    .write = merge_dev_write,
    .open =  merge_dev_open,
    .unlocked_ioctl = merge_dev_ioctl,
    .mmap = merge_dev_mmap,
    #if 0
    .get_unmaped_area = merge_dev_get_unmap_area, 
    #endif
};

int merge_dev_init(void)
{
    int result ;
    dev_t devno = MKDEV(merge_dev_major,0);
    printk("hello merge dev init\r\n");
    if(merge_dev_major)
    {
        result = register_chrdev_region(devno,1,"merge_mem_dev");
    }
    else
    {
        result = alloc_chrdev_region(&devno,0,1,"merge_mem_dev");
        merge_dev_major = MAJOR(devno);
    }
    if(result < 0)
    {
        return result;
    }

    merge_cdev = cdev_alloc();
    if(!merge_cdev)
    {
        return -1;
    }

    merge_cdev->owner = THIS_MODULE;
    merge_cdev->ops = &merge_dev_fops;
    
    #if 1
    result = cdev_add(merge_cdev,MKDEV(merge_dev_major,0),1);

    if(result < 0)
    {
        printk("cdev_add return err\r\n");
        return result;
    }
    #endif
    merge_class = class_create(THIS_MODULE,"merge_class");
    
    if(IS_ERR(merge_class))
    {
        printk("create class error\r\n");
        return -1;
    }
    
    device_create(merge_class,NULL,devno,NULL,"merge_mem");
 
    merge_dev = devno;
  
    return 0;
}
void merge_dev_exit(void)
{
    unregister_chrdev_region(MKDEV(merge_dev_major,0),1);
    if(merge_cdev)
    {
        cdev_del(merge_cdev);        
    }
    device_destroy(merge_class,merge_dev);
    class_destroy(merge_class);    
    return;    
}


#if 0
static int __init merge_mem_init(void) {
    merge_dev_init();
    return 0;
}


static void __exit merge_mem_exit(void) {
    merge_dev_exit();
}

module_init(merge_mem_init);
module_exit(merge_mem_exit);
MODULE_AUTHOR("lijiyong");
MODULE_DESCRIPTION("merge_mem");
#endif

static int __init merge_mem_init(void)
{
	slice_que_resource_init();
	slice_que_reader_init();
	pone_hash_table_init(0x10000);
	slice_state_control_init();
	slice_merge_timer_init(1000);
	pone_sysfs_init();
	printk("slice_control_init \r\n");
}

subsys_initcall(merge_mem_init);

