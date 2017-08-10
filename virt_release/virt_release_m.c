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
#include "vector.h"
#include "chunk.h"

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
				printk("args is 0x%lx\r\n",args);
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
char* blk_id_2_ptr(cluster_head_t *pclst, unsigned int id)
{
    int ptrs_bit = pclst->pg_ptr_bits;
    int ptrs = (1 << ptrs_bit);
    u64 direct_pgs = CLST_NDIR_PGS;
    u64 indirect_pgs = 1<<ptrs_bit;
    u64 double_pgs = 1<<(ptrs_bit*2);
    int pg_id = id >> pclst->blk_per_pg_bits;
    int offset;
    char *page, **indir_page, ***dindir_page;

    if(pg_id < direct_pgs)
    {
        page = (char *)pclst->pglist[pg_id];
        if(page == 0)
        {
            smp_mb();
            page = (char *)atomic64_read((atomic64_t *)&pclst->pglist[pg_id]);
            printk("blk_id_2_ptr,line[%d] id %d\r\n",__LINE__, id);
            return 0;
        }
    }
    else if((pg_id -= direct_pgs) < indirect_pgs)
    {
        indir_page = (char **)pclst->pglist[CLST_IND_PG];
        if(indir_page == NULL)
        {
            smp_mb();
            indir_page = (char **)atomic64_read((atomic64_t *)&pclst->pglist[CLST_IND_PG]);
            printk("blk_id_2_ptr,line[%d] id %d\r\n",__LINE__, id);
            return 0;
        }
        offset = pg_id;
        page = indir_page[offset];
        if(page == 0)
        {
            smp_mb();
            page = (char *)atomic64_read((atomic64_t *)&indir_page[offset]);
            printk("blk_id_2_ptr,line[%d] id %d\r\n",__LINE__, id);
            return 0;
        }        
    }
    else if((pg_id -= indirect_pgs) < double_pgs)
    {
        dindir_page = (char ***)pclst->pglist[CLST_DIND_PG];
        if(dindir_page == NULL)
        {
            smp_mb();
            dindir_page = (char ***)atomic64_read((atomic64_t *)&pclst->pglist[CLST_DIND_PG]);
            printk("blk_id_2_ptr,line[%d] id %d\r\n",__LINE__, id);
            return 0;
        }
        offset = pg_id >> ptrs_bit;
        indir_page = dindir_page[offset];
        if(indir_page == 0)
        {
            smp_mb();
            indir_page = (char **)atomic64_read((atomic64_t *)&dindir_page[offset]);
            printk("blk_id_2_ptr,line[%d] id %d\r\n",__LINE__, id);
            return 0;
        }         
        offset = pg_id & (ptrs-1);
        page = indir_page[offset];
        if(page == 0)
        {
            smp_mb();
            page = (char *)atomic64_read((atomic64_t *)&indir_page[offset]);
            printk("blk_id_2_ptr,line[%d] id %d\r\n",__LINE__, id);
            return 0;
        }
    }
    else
    {
        printk("warning: %s: id is too big\r\n", __func__);
        return 0;
        while(1);
        return 0;
    }
    
    offset = id & (pclst->blk_per_pg-1);
    return    page + (offset << BLK_BITS); 
    
}

char* db_id_2_ptr(cluster_head_t *pclst, unsigned int id)
{
    char *p = blk_id_2_ptr(pclst, id/pclst->db_per_blk);
    if(p == 0)
        return 0;
    return p + id%pclst->db_per_blk * DBLK_SIZE;
    
}

char* vec_id_2_ptr(cluster_head_t *pclst, unsigned int id)
{
    char *p = blk_id_2_ptr(pclst, id/pclst->vec_per_blk);
    if(p == 0)
        return 0;
    return p + id%pclst->vec_per_blk * VBLK_SIZE;
    
}

static int __init virt_release_mem_init(void) {
    //virt_release_dev_init();
    cluster_head_t *pclst = (cluster_head_t *)0xffff88405f276000ull;
    spt_vec *pvec;
    int blk_id, id, db_id, i, j;
    spt_dh *pdh;
    spt_thrd_data *pthrd_data;
    u32 list_vec_id, ret_id;
    spt_buf_list *pnode;
    db_head_t *db;
    int cnt = 0;
    int buf_total;
    struct page *page;

    id = pclst->dblk_free_head;
    printk("\r\n===========================\r\n");
    printk("free db:%d free vec:%d\r\n", pclst->free_dblk_cnt, pclst->free_vec_cnt);
    printk("pclst->thrd_total: %d  pg_cursor %d pg_num_total:%d\r\n", pclst->thrd_total, pclst->pg_cursor, pclst->pg_num_total);

    page = (struct page *)0xffffea00f71ec8d0ull;
    if(page->page_mem != NULL)
    {
        pdh = page->page_mem;
        printk("pdh->data:%p  pdh->ref: %d\r\n", pdh->pdata, pdh->ref);
    }
    page = (struct page *)0xffffea00a7b5a890ull;
    if(page->page_mem != NULL)
    {
        pdh = page->page_mem;
        printk("pdh->data:%p  pdh->ref: %d\r\n", pdh->pdata, pdh->ref);
    }
#if 1
    for(j=0;j<49;j++)
    {
        pthrd_data = &pclst->thrd_data[i];
        list_vec_id = pthrd_data->vec_alloc_out;
        cnt = 0;
        while(list_vec_id != SPT_NULL)
        {
            pnode = (spt_buf_list *)vec_id_2_ptr(pclst,list_vec_id);
            if(pnode == 0)
            {
                printk("%s\t%d\r\n", __FUNCTION__, __LINE__);
                return 0;
            }
            pvec = (spt_vec *)vec_id_2_ptr(pclst, pnode->id);
            if(pvec->rd == 1514392 || pvec->rd == 1567556)
            {
                printk("%s\t%d\r\n", __FUNCTION__, __LINE__);
                printk("vec status:%d, vec type:%d, vec down:%d pos:%d\r\n", 
                pvec->status, pvec->type, pvec->down, pvec->pos);
                return 0;
            }
            
            list_vec_id = pnode->next;
            cnt++;
            if(cnt > pthrd_data->vec_cnt)
            {
                printk("cnt %d   data_cnt  %d\r\n", cnt , pthrd_data->vec_cnt);
                break;
            }
        }
    }
#endif
#if 0
    1514392
    1567556

    while(id != -1)
    {
        pdh = (spt_dh *)blk_id_2_ptr(pclst, id/pclst->db_per_blk);
        if(pdh == 0)
        {
            printk("cnt[%d] [id %d] %s\t%d\r\n",cnt, id,  __FUNCTION__, __LINE__);
            return 0;
        }
        
        db = (db_head_t *)db_id_2_ptr(pclst, id);
        for(i = 0; i < pclst->db_per_blk; i++)
        {
            if(pdh->ref != 0 && ((db_head_t *)pdh)->magic!= 0xdeadbeef)
            {
                printk("dbid: %d ,i:%d, pdh ref:%d, data:%p\r\n",id, i, pdh->ref, pdh->pdata);
                //return 0;
            }
            pdh++;
        }
        id = db->next;
        cnt++;
        if(cnt > pclst->free_dblk_cnt)
        {
            printk("????????????????cnt :%d\r\n");
            break;
        }
    }
    printk("==========================free list ok\r\n");
    cnt = 0;
    for(j=0;j<49;j++)
    {
        pthrd_data = &pclst->thrd_data[i];
        list_vec_id = pthrd_data->data_alloc_out;
        cnt = 0;
        while(list_vec_id != SPT_NULL)
        {
            pnode = (spt_buf_list *)vec_id_2_ptr(pclst,list_vec_id);
            if(pnode == 0)
            {
                printk("%s\t%d\r\n", __FUNCTION__, __LINE__);
                return 0;
            }
            pdh = (spt_dh *)blk_id_2_ptr(pclst, pnode->id/pclst->db_per_blk);
            if(pdh == 0)
            {
                printk("%s\t%d\r\n", __FUNCTION__, __LINE__);
                return 0;
            }
            
            for(i = 0; i < pclst->db_per_blk; i++)
            {
                if(pdh->ref != 0)
                {
                    printk("dbid : %d i:%d pdh ref:%d, data:%p\r\n",pnode->id, i, pdh->ref, pdh->pdata);
                    //return 0;
                }
                pdh++;
            }
            list_vec_id = pnode->next;
            cnt++;
            if(cnt > pthrd_data->data_cnt)
            {
                printk("cnt %d   data_cnt  %d\r\n", cnt , pthrd_data->data_cnt);
                break;
            }
        }
    }
#endif
    printk("\r\n======init done==========\r\n");
    return 0;
}


static void __exit virt_release_mem_exit(void) {
    //virt_release_dev_exit();
}

module_init(virt_release_mem_init);
module_exit(virt_release_mem_exit);
MODULE_AUTHOR("lijiyong");
MODULE_DESCRIPTION("virt_release_mem");
