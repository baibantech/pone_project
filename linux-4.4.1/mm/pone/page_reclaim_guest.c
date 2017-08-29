/*************************************************************************
	> File Name: page_reclaim_guest.c
	> Author: lijiyong0303
	> Mail: lijiyong0303@163.com 
	> Created Time: Tue 29 Aug 2017 10:52:08 AM CST
 ************************************************************************/

#include<linux/kernel.h>
#include <linux/highmem.h>
#include <linux/rmap.h>
#include <asm/tlbflush.h>
#include <pone/slice_state.h>
#include <pone/virt_release.h>

struct virt_mem_pool *guest_mem_pool = NULL;
int guest_page_clear_ok = 0;
int guest_page_no_need_clear = 0; 

int virt_mark_page_release(struct page *page)
{
	int pool_id ;
	unsigned long long alloc_id;
	unsigned long long state;
	unsigned long long idx ;
	volatile struct virt_release_mark *mark ;
	if(!guest_mem_pool)
	{
		clear_highpage(page);
		set_guest_page_clear_ok();
		return -1;
	}
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_r_begin);
	pool_id = guest_mem_pool->pool_id;
	alloc_id = atomic64_add_return(1,(atomic64_t*)&guest_mem_pool->alloc_idx)-1;
	idx = alloc_id%guest_mem_pool->desc_max;
	state = guest_mem_pool->desc[idx];
	
	mark =kmap_atomic(page);
	if(0 == state)
	{
		if(0 != atomic64_cmpxchg((atomic64_t*)&guest_mem_pool->desc[idx],0,page_to_pfn(page)))
		{
			pool_id = guest_mem_pool->pool_max  +1;
			idx = guest_mem_pool->desc_max +1;
			atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_release_err_conflict);
			
		}
		else
		{
			atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_release_ok);
		}
		mark->pool_id = pool_id;
		mark->alloc_id = idx;
		barrier();
		mark->desc = guest_mem_pool->mem_ind;
		barrier();
		kunmap_atomic(mark);

		atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_r_end);
		return 0;
	}
	else
	{
		mark->pool_id = guest_mem_pool->pool_max +1;
		mark->alloc_id = guest_mem_pool->desc_max +1;
		barrier();
		mark->desc = guest_mem_pool->mem_ind;
		barrier();
		kunmap_atomic(mark);
	}
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_release_err_state);
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_r_end);
	return -1;
}

int virt_mark_page_alloc(struct page *page)
{
	unsigned long long state;
	unsigned long long idx ;
	volatile struct virt_release_mark *mark ;
		
	if(!guest_mem_pool)
	{
		return 0;
	}

	atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_a_begin);
	mark =kmap_atomic(page);

	if(mark->desc == guest_mem_pool->mem_ind)
	{
		if(mark->pool_id == guest_mem_pool->pool_id)
		{
			if(mark->alloc_id < guest_mem_pool->desc_max)
			{
				idx = mark->alloc_id;
				state = guest_mem_pool->desc[mark->alloc_id];
				if(state == page_to_pfn(page))
				{
					if(state == atomic64_cmpxchg((atomic64_t*)&guest_mem_pool->desc[idx],state,0))
					{
						atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_alloc_ok);
						atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_a_end);
					}
					else
					{
						while(mark->desc != 0)
						{
							barrier();
						}
					}
				}
			}
		}
		memset(mark,0,sizeof(struct virt_release_mark));
		barrier();
		kunmap_atomic(mark);
		return 0;
	}
	else
	{
	}
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_alloc_err_state);
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_a_end);
	kunmap_atomic((void*)mark);
	return -1;
}

void init_guest_mem_pool(void *addr,int len)
{
	struct virt_mem_pool *pool = addr;
	memset(addr,0,len);
	pool->magic= 0xABABABABABABABAB;
	pool->pool_id = -1;
	pool->desc_max = (len - sizeof(struct virt_mem_pool))/sizeof(unsigned long long);
}

static int __init virt_mem_guest_init(void)
{
    void __iomem *ioaddr = ioport_map(0xb000,0);
	unsigned long long  io_reserve_mem = 0;	
	char *ptr = NULL;

	io_reserve_mem = ioread32(ioaddr);
	printk("io reserve mem add is 0x%llx\r\n",io_reserve_mem);
	if(io_reserve_mem != 0xFFFFFFFF)
	{
		ptr = ioremap(io_reserve_mem <<12 , 0x4000000);
		printk("ptr remap addr is %p\r\n",ptr);
		if(ptr != NULL)
		{
			
			init_guest_mem_pool(ptr,0x4000000); 
			
			iowrite32(io_reserve_mem, ioaddr);
			if(guest_page_clear_ok)
				guest_page_no_need_clear  = 1;
			barrier();	
			guest_mem_pool = ptr;
		}
	}
    return 0;
}

subsys_initcall(virt_mem_guest_init);
