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
#include <linux/kvm_host.h>
#include "virt_release_m.h"

MODULE_LICENSE("GPL");
dev_t virt_release_dev;
int virt_release_dev_major = 0,virt_release_dev_minor = 0;
struct cdev *virt_release_cdev = NULL;
struct class *virt_release_class = NULL;
extern int mem_pool_reg(unsigned long gpa, struct kvm *kvm,struct mm_struct *mm,struct task_struct *task);
int virt_release_dev_open(struct inode *inode,struct file *filp)
{
    printk("open virt_release dev file\r\n");
    return 0;
}
static size_t virt_release_dev_read(struct file *filp,char __user *buf,size_t size,loff_t *ppos)
{
    return 0;
}

static size_t virt_release_dev_write(struct file *filp,const char __user *buf,size_t size,loff_t *ppos)
{
    return 0;
}


int virt_release_dev_ioctl(struct file *filp,unsigned int cmd,unsigned long args)
{
    printk("enter ioctl\r\n");
    struct mm_struct *mm = current->mm;
    struct vm_area_struct *vma;
    switch(cmd)
    {
		case VIRT_RELEASE_IOC_REG_MEM_POOL :
			{
				printk("virt release reg mem pool\r\n");
				mem_pool_reg(args,NULL,current->mm,current);	
				
				break;
			}
		default : printk("error cmd number\r\n");break;
    }
    return 0;
}


int virt_release_dev_mmap(struct file *filp,struct vm_area_struct *vm)
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

static const struct file_operations virt_release_dev_fops = 
{
    .owner = THIS_MODULE,
    .read =  virt_release_dev_read,
    .write = virt_release_dev_write,
    .open =  virt_release_dev_open,
    .unlocked_ioctl = virt_release_dev_ioctl,
    .mmap = virt_release_dev_mmap,
    #if 0
    .get_unmaped_area = virt_release_dev_get_unmap_area, 
    #endif
};

int virt_release_dev_init(void)
{
    int result ;
    dev_t devno = MKDEV(virt_release_dev_major,0);
    printk("hello virt_release dev init\r\n");
    if(virt_release_dev_major)
    {
        result = register_chrdev_region(devno,1,"virt_release_mem_dev");
    }
    else
    {
        result = alloc_chrdev_region(&devno,0,1,"virt_release_mem_dev");
        virt_release_dev_major = MAJOR(devno);
    }
    if(result < 0)
    {
        return result;
    }

    virt_release_cdev = cdev_alloc();
    if(!virt_release_cdev)
    {
        return -1;
    }

    virt_release_cdev->owner = THIS_MODULE;
    virt_release_cdev->ops = &virt_release_dev_fops;
    
    #if 1
    result = cdev_add(virt_release_cdev,MKDEV(virt_release_dev_major,0),1);

    if(result < 0)
    {
        printk("cdev_add return err\r\n");
        return result;
    }
    #endif
    virt_release_class = class_create(THIS_MODULE,"virt_release_class");
    
    if(IS_ERR(virt_release_class))
    {
        printk("create class error\r\n");
        return -1;
    }
    
    device_create(virt_release_class,NULL,devno,NULL,"virt_release_mem");
 
    virt_release_dev = devno;
  
    return 0;
}
void virt_release_dev_exit(void)
{
    unregister_chrdev_region(MKDEV(virt_release_dev_major,0),1);
    if(virt_release_cdev)
    {
        cdev_del(virt_release_cdev);        
    }
    device_destroy(virt_release_class,virt_release_dev);
    class_destroy(virt_release_class);    
    return;    
}



static int __init virt_release_mem_init(void) {
    virt_release_dev_init();
    return 0;
}


static void __exit virt_release_mem_exit(void) {
    virt_release_dev_exit();
}

module_init(virt_release_mem_init);
module_exit(virt_release_mem_exit);
MODULE_AUTHOR("lijiyong");
MODULE_DESCRIPTION("virt_release_mem");
