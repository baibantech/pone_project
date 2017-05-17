#define _VERSION__
#include <linux/kernel.h>
#include <linux/module.h>
#if 0//CONFIG_MODVERSIOINS==1
#define MODVERSIONS
#include <linux/modversions.h>
#endif
#include<linux/fs.h>
#include<linux/string.h>
#include<linux/errno.h>

#include<linux/mm.h>
#include<linux/vmalloc.h>
//#include<linux/wrapper.h>
#include<linux/slab.h>
#include<asm/io.h>
#include<linux/mman.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/sched.h>

#include <asm-generic/current.h>
#include <linux/smp.h>
#include <asm/irqflags.h>
//#include <asm/pgtable.h>
//#include <asm/paravirt.h>

#ifdef CONFIG_PARAVIRT
//#error yes
#else
//#error no
#endif

#define mapdrv_debug(f, a...)	{ \
					printk ("MAPDRV DEBUG (%s, %d): %s:", \
						__FILE__, __LINE__, __func__); \
				  	printk (f, ## a); \
					}

#define MAPLEN (4096*100)
/* device open */
int mapdrv_open(struct inode *inode,struct file *file);
/* device close */
int mapdrv_release(struct inode *inode,struct file *file);
/*device mmap */
int mapdrv_mmap(struct file *file,struct vm_area_struct *vma);

long mapdrv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* vm area open */
void map_vopen(struct vm_area_struct *vma);
/* vm area close */
void map_vclose(struct vm_area_struct *vma);
/* vm area nopage */
struct page *map_nopage(struct vm_area_struct *vma,unsigned long address,int write_access);
int map_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
static unsigned long long rdtsc(void);

//extern struct mm_struct init_mm;

extern void msleep(unsigned int msecs);

static struct file_operations mapdrv_fops=
{
	.owner  = THIS_MODULE,
	.mmap   = mapdrv_mmap,
    .unlocked_ioctl = mapdrv_ioctl,
	.open   = mapdrv_open,
	.release = mapdrv_release,
};

static struct vm_operations_struct map_vm_ops=
{
	open  : map_vopen,
	close : map_vclose,
//	nopage: map_nopage,
	fault : map_fault,
};
      
static int *vmalloc_area = NULL;     
        
static int major; // major number of device      

#if 0
static unsigned long long rdtsc()
{
    unsigned int lo,hi;
    asm volatile
    (
     "rdtsc":"=a"(lo),"=d"(hi)
    );
    return (unsigned long long)hi<<32|lo;
}
#endif
extern unsigned int __read_mostly cpu_khz;	/* TSC clocks / usec, not used here */

extern unsigned int __read_mostly tsc_khz;

#if 1
static int  __init mapdrv_init(void)
{
    void __iomem *ioaddr = ioport_map(0xb000,0);
    struct page *page = alloc_pages(GFP_KERNEL|__GFP_ZERO,5);
    char *ptr = (char *)page_address(page);
    
    strcpy((char*)ptr,"hello world from guest !"); 

    iowrite32(virt_to_phys(ptr) >> 12, ioaddr);
    return 0;
}
#else
static int  __init mapdrv_init(void)
{
  
    unsigned long virt_addr;
//    unsigned int unstable, disable;
    unsigned int eax, ebx, ecx, edx;


    if((major=register_chrdev(0, "mapdrv", &mapdrv_fops))<0) 
    {
        mapdrv_debug("mapdrv: unable to register character device\n");
        return (-EIO);
    }
    /* get a memory area that is only virtual contigous. */
    vmalloc_area=vmalloc(MAPLEN/*+2*PAGE_SIZE*/);

    for (virt_addr=(unsigned long)vmalloc_area;virt_addr<(unsigned long)(&(vmalloc_area[MAPLEN/sizeof(int)]));virt_addr+=PAGE_SIZE)
    {    
        SetPageReserved(vmalloc_to_page((void *)virt_addr));
    }
    /* set a hello message to kernel space for read by user */
    strcpy((char*)vmalloc_area,"hello world from kernel space !"); 
    
    mapdrv_debug("vmalloc_area at 0x%p (page 0x%p)\n", vmalloc_area,
    vmalloc_to_page(vmalloc_area));

//    disable = check_tsc_disabled();
//    unstable = check_tsc_unstable();
    eax = 0X15;// 0x80000007;
    native_cpuid(&eax, &ebx, &ecx, &edx);

    mapdrv_debug("tsc_khz = %d, cpu_khz=%d, ebx:%x, edx:%x\n", tsc_khz, 
    cpu_khz,ebx, edx);
    
    return(0);
}
#endif


static void __exit mapdrv_exit(void)
{
  unsigned long virt_addr;
  /* unreserve all pages */
 for (virt_addr=(unsigned long)vmalloc_area;virt_addr<(unsigned long)(&(vmalloc_area[MAPLEN/sizeof(int)]));virt_addr+=PAGE_SIZE)
  {
   ClearPageReserved(vmalloc_to_page((void *)virt_addr));
  }
  /* and free the two areas */
  if (vmalloc_area)
    vfree(vmalloc_area);
 /* unregister the device */
  unregister_chrdev(major, "mapdrv");
  return;

}
/* device open method */
int mapdrv_open(struct inode *inode, struct file *file)
{
    try_module_get(mapdrv_fops.owner);
    return(0);
}

/* device close method */
int mapdrv_release(struct inode *inode, struct file *file)
{
    module_put(mapdrv_fops.owner);
    return(0);
}

int mapdrv_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long offset = vma->vm_pgoff<<PAGE_SHIFT;

    unsigned long size = vma->vm_end - vma->vm_start;
    if (offset & ~PAGE_MASK)
    {
        mapdrv_debug("offset not aligned: %ld\n", offset);
        return -ENXIO;
    }
    if (size>MAPLEN)
    {
        mapdrv_debug("size too big\n");
        return(-ENXIO);
    }
    /*  only support shared mappings.*/
    if ((vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_SHARED))
    {
        mapdrv_debug("writeable mappings must be shared, rejecting\n");
        return(-EINVAL);
    }
    /* do not want to have this area swapped out, lock it */
    vma->vm_flags |= VM_LOCKED;
    if (offset == 0)
    {
        vma->vm_ops = &map_vm_ops;
        /* call the open routine to increment the usage count */
        map_vopen(vma);
    }
    else
    {
        mapdrv_debug("offset out of range\n");
        return -ENXIO;
    }
    return(0);
}	

struct my_arg
{
    long offset;
    long cnt;
};

static void delay_loop(unsigned long loops)
{
	asm volatile(
		"	test %0,%0	\n"
		"	jz 3f		\n"
		"	jmp 1f		\n"

		".align 16		\n"
		"1:	jmp 2f		\n"

		".align 16		\n"
		"2:	dec %0		\n"
		"	jnz 2b		\n"
		"3:	dec %0		\n"

		: /* we don't need output */
		:"a" (loops)
	);
}


long mapdrv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct my_arg tmp = {0};
    volatile long i;
    volatile unsigned long long *p;
    unsigned int *q, cpuid;
    unsigned int start,end,max;

    cpuid = get_cpu();
 //   cr4 = read_cr4();
//	mapdrv_debug("cmd = %u, arg = %lu\n", cmd, arg);
    mapdrv_debug("on cpu = %d, pid = %d\n", get_cpu(), current->pid);

    switch(cmd)
    {
        case 1:
            copy_from_user(&tmp, (void __user *)arg, sizeof(tmp));
            p = (unsigned long long *)((unsigned long)vmalloc_area + tmp.offset);
            mapdrv_debug("p addr = %p\n", p);
            q = (unsigned int *)((unsigned long)p + 4096);
            q = q + cpuid*1024;
            max = 0;
            #if 1
            native_irq_disable();
            #if 1
            for(i=0; i<tmp.cnt; i++)
            {
                //rdtsc_barrier();
                //rdtscl(start);
                //*p += 1;
                //start = rdtsc();
                atomic_add(1, (atomic_t *)p);
                //*p += 1;
                //rdtsc_barrier();
                //rdtscl(end);
                end = end-start;
                q[i&0x3ff] = end;
                if(end > max) max = end;
                //delay_loop(200);
            }            
            #endif           
            native_irq_enable();
            end = end - start;
            mapdrv_debug("cpu[%d]*p = %llx, max cycle:%d\n",get_cpu(), *p, max);
            #endif
            return 0;
        default:
            return -1;
    }
    return 0;
}


/* open handler for vm area */
void map_vopen(struct vm_area_struct *vma)
{
  /* needed to prevent the unloading of the module while
  somebody still has memory mapped */
     try_module_get(mapdrv_fops.owner);
}

/* close handler form vm area */
void map_vclose(struct vm_area_struct *vma)
{
     module_put(mapdrv_fops.owner);
}
#if 0
/* page fault handler */
struct page *map_nopage(struct vm_area_struct *vma, unsigned long address, int write_access)
{
  unsigned long offset;
  unsigned long virt_addr;
  /* determine the offset within the vmalloc'd area  */
  offset = address - vma->vm_start + (vma->vm_pgoff<<PAGE_SHIFT);
  /* translate the vmalloc address to kmalloc address  */
  virt_addr = (unsigned long)vaddr_to_kaddr(&vmalloc_area[offset/sizeof(int)]);
  if (virt_addr == 0UL)
     {
     return((struct page *)0UL);
     }
  /* increment the usage count of the page */
  atomic_inc(&(virt_to_page(virt_addr)->count));
  mapdrv_debug("map_drv: page fault for offset 0x%lx (kseg x%lx)\n",offset, virt_addr);
   return(virt_to_page(virt_addr));
	  
}
#endif
/* page fault handler */
int map_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    unsigned long virt_addr;
    virt_addr = (unsigned long)vmalloc_area + (unsigned long)(vmf->pgoff << PAGE_SHIFT);
    vmf->page = vmalloc_to_page((void *)virt_addr);
    get_page(vmf->page);
    return 0;	  
}
MODULE_LICENSE("GPL");
MODULE_AUTHOR("hahah");
MODULE_DESCRIPTION("kernel module mapdr");
MODULE_VERSION("1.0");	
module_init(mapdrv_init);
module_exit(mapdrv_exit);
