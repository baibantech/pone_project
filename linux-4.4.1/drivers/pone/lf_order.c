#if 0
#define _GNU_SOURCE
#include <sched.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/ipc.h>  
#include <sys/shm.h>  
#include <errno.h>
#include <signal.h>
#include<sys/wait.h>
#include <sys/stat.h> /* For mode constants */
#include <fcntl.h> /* For O_* constants */
#endif
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <linux/atomic.h>
#include  "lf_order.h"
orderq_h_t *goqh;
#if 0
void *lf_malloc_align(int size)
{
    void *ptr, *align;

    ptr = kmalloc(size + 4096);
    if(ptr)
    {
//        printf("%s\tptr:%p\r\n", __FUNCTION__, ptr);
    
        align = (void *)(((u64)ptr + 4096)&(~0xfff));
        ((void **)align)[-1] = ptr;
        return align;
    }
    else
        return NULL;
}

void lf_free_align(void *align)
{
//    printf("%s\t%p\r\n", __FUNCTION__, (((void **)align)[-1]));
    free(((void **)align)[-1]);
}
#endif

void *lf_alloc_page(void)
{
    void *p;
    p = kmalloc(4096,GFP_ATOMIC);
    if(p != 0)
    {
        if(((unsigned long)p & 0x0FFF)!= 0)
		{
			BUG();
		}
		memset(p, 0, 4096);
        smp_mb();
    }
    return p;
}

void lf_free_page(void *page)
{
    kfree(page);
    return;
}
//static
int __lxd_alloc(u64 *lxud, orderq_h_t *oq)
{
    u64 *new_pg, *pg;
    new_pg = (u64 *)lf_alloc_page();
    if(new_pg == NULL)
    {
        lforder_debug("OOM\n");
        return LO_OOM;
    }
    pg = (u64 *)atomic64_cmpxchg((atomic64_t *)lxud, 0, (long)new_pg);
    if(pg != 0)
    {
        lf_free_page(new_pg);
    }
    else
    {
        atomic_add(1, (atomic_t *)&oq->pg_num);
    }
    return LO_OK;
}

static inline u64 *lxd_offset(u64 *lxud, u64 oid, int level)
{
    return (u64 *)PAGE_PTR(*lxud) + lx_index(level, oid);
}

orderq_h_t *lfo_q_init(int thread_num)
{
    orderq_h_t *oq = NULL;
    int i;

    oq = (orderq_h_t *)kmalloc(sizeof(orderq_h_t) + sizeof(u64)*thread_num
            + sizeof(dir_path)*thread_num,GFP_KERNEL);
    if(oq == NULL)
    {
        lforder_debug("OOM\n");
        return NULL;
    }
    memset((void *)oq, 0, sizeof(orderq_h_t));

    oq->thd_num = thread_num;

    oq->l0_fd = (u64 *)lf_alloc_page();
    if(oq->l0_fd == NULL)
    {
        kfree(oq);
        lforder_debug("OOM\n");
        return NULL;
    }
    memset((void *)oq->l0_fd, 0, 4096);
    oq->pg_num = 1;

    oq->local_oid = (u64 *)((u64)oq + sizeof(orderq_h_t));
    memset((void *)oq->local_oid, 0, sizeof(u64)*thread_num);

    oq->local_path= (dir_path *)((u64)oq->local_oid + sizeof(u64)*thread_num);
    for(i=0; i<thread_num; i++)
    {
        oq->local_path[i].entry[0] = oq->l0_fd;
        oq->local_path[i].entry[1] = (u64 *)&oq->l0_fd;
    }
    oq->pg_max = 100;
    return oq;
}

u64 *_get_l0_pg(orderq_h_t *oq, u64 *ltopd, int level, int thread, u64 oid)
{
    int i;
    u64 *lxud, *lxd;
    u64 *new_pg, *pg;

    if(level < 2)
        BUG();
    
    if(*ltopd == 0)
    {
        new_pg = (u64 *)lf_alloc_page();
        if(new_pg == NULL)
        {
            lforder_debug("OOM\n");
            return NULL;
        }
        pg = (u64 *)atomic64_cmpxchg((atomic64_t *)ltopd, 0, (long)new_pg);
        if(pg != 0)
        {
            lf_free_page(new_pg);
        }
        else
        {
            atomic_add(1, (atomic_t *)&oq->pg_num);
            atomic64_add(1, (atomic64_t *)ltopd);
        }
    }
    oq->local_path[thread].level = level;
    oq->local_path[thread].entry[level] = ltopd;

    lxud = ltopd;
    for(i = level-1; i > 0; i--)
    {
        lxd = lxd_offset(lxud, oid, i);
        oq->local_path[thread].entry[i] = lxd;
        if(*lxd == 0)
        {
            new_pg = (u64 *)lf_alloc_page();
            if(new_pg == NULL)
            {
                lforder_debug("OOM\n");
                return NULL;
            }
            pg = (u64 *)atomic64_cmpxchg((atomic64_t *)lxd, 0, (long)new_pg);
            if(pg != 0)
            {
                lf_free_page(new_pg);
            }
            else
            {
//                atomic64_add(i, (atomic64_t *)lxud);
                atomic_add(1, (atomic_t *)&oq->pg_num);
            }
        }
        lxud = lxd;
    }
    oq->local_path[thread].entry[0] = (u64 *)*lxd;
    return (u64 *)*lxd;
}

u64 *get_l0_pg(orderq_h_t *oq, int thread, u64 oid)
{
    u64 last_oid;
    int level;
    u64 *ltopd;

    last_oid = oq->local_oid[thread];
    oq->local_oid[thread] = oid;
    if(oids_in_same_page(oid, last_oid))
    {
        oq->local_oid[thread] = oid;//可以不赋值，等到换页时再赋值
        return oq->local_path[thread].entry[0];
    }
    if(oid < l1_PTRS_PER_PG)
    {
        ltopd = (u64 *)&oq->l1_fd;
        level = 2; 
    }
    else if(oid < l2_PTRS_PER_PG)
    {
        ltopd = (u64 *)&oq->l2_fd;
        level = 3; 
    }        
    else if(oid < l3_PTRS_PER_PG)
    {
        ltopd = (u64 *)&oq->l3_fd;
        level = 4; 
    }        
    else if(oid < l4_PTRS_PER_PG)
    {
        ltopd = (u64 *)&oq->l4_fd;
        level = 5; 
    }        
    else if(oid < l5_PTRS_PER_PG)
    {
        ltopd = (u64 *)&oq->l5_fd;
        level = 6; 
    }
    else if(oid < l6_PTRS_PER_PG) 
    {
        ltopd = (u64 *)&oq->l6_fd;
        level = 7; 
    }
    else
    {
        ltopd = (u64 *)&oq->l7_fd;
        level = 8; 
    }

    return _get_l0_pg(oq, ltopd, level, thread, oid);
}

void deal_finished_pgs(orderq_h_t *oq, int thread)
{
    u64 *pd, *pg;
    int i;

    pd = oq->local_path[thread].entry[1];
    pg = (u64 *)PAGE_PTR(*pd);
    lf_free_page((void *)pg);
    *pd = 0;
    atomic_sub(1, (atomic_t *)&oq->pg_num);        

    for(i = 2; i <= oq->local_path[thread].level; i++)
    {
        pd = oq->local_path[thread].entry[i];
        if(PAGE_CNT(atomic64_add_return(1,(atomic64_t *)pd)) == PTRS_PER_LEVEL)
        {
            pg = (u64 *)PAGE_PTR(*pd);
            lf_free_page((void *)pg);
            *pd = 0;
            atomic_sub(1, (atomic_t *)&oq->pg_num);
            if(i == oq->local_path[thread].level)
                return;
            atomic64_add(1, (atomic64_t *)oq->local_path[thread].entry[i+1]);
        }
        else
        {
            return;
        }
    }
}
u64 lfo_get_count(orderq_h_t *oq)
{
	barrier();
	return oq->count;
}
int lfo_write(orderq_h_t *oq, int thread, u64 cmd)
{
    u64 *cur_pg, *entry;
    u64 val, oid;
    int idx;

    oid = atomic64_add_return(1, (atomic64_t *)&oq->w_idx) - 1;
    cur_pg = get_l0_pg(oq, thread, oid);
    if(cur_pg == NULL)
    {
        lforder_debug("OOM\n");
        return LO_OOM;
    }
    cur_pg = (u64 *)PAGE_PTR(cur_pg);
    idx = l0_index(oid);
    entry = cur_pg + idx;
    
    val = atomic64_xchg((atomic64_t *)entry, cmd);
    atomic64_add(1, (atomic64_t *)&oq->count);

    return LO_OK;
}

u64 lfo_read(orderq_h_t *oq, int thread)
{
    u64 *cur_pg, *entry, *pd;
    u64 val, oid;
    int idx;

    while(atomic64_sub_return(1,(atomic64_t *)&oq->count) < 0)
    {
        if(atomic64_add_return(1,(atomic64_t *)&oq->count) <= 0)
        {
            return 0;
        }
    }

    oid = atomic64_add_return(1, (atomic64_t *)&oq->r_idx) - 1;    
    cur_pg = get_l0_pg(oq, thread, oid);
    if(cur_pg == NULL)
    {
        lforder_debug("OOM\n");
        return LO_OOM;
    }
    cur_pg = (u64 *)PAGE_PTR(cur_pg);    
    idx = l0_index(oid);
    entry = cur_pg + idx;
    
    while((val = atomic64_read((atomic64_t *)entry)) == 0)
    {
        ;
    }
    *entry = 0;
    pd = oq->local_path[thread].entry[1];
    
    if(PAGE_CNT(atomic64_add_return(1,(atomic64_t *)pd)) == PTRS_PER_LEVEL)
    {
        deal_finished_pgs(oq, thread);
    }
    return val;
}

