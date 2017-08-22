#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm-generic/pgtable.h>
#include <linux/bootmem.h>
#include <linux/hash.h>
#include <linux/ksm.h>
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include <pone/lf_rwq.h>
#include "vector.h"
#include "chunk.h"
#include  "splitter_adp.h"
#include <pone/virt_release.h>
#include "pone_time.h"
#include "lf_order.h"

lfrwq_t *slice_que = NULL;
lfrwq_t *slice_watch_que = NULL;
slice_state_control_block *global_block = NULL;
unsigned long long alloc_check = 0;
int pone_run = 1;

unsigned long slice_watch_que_debug = 1;
unsigned long slice_que_debug = 1;


unsigned long long slice_alloc_num = 0;
unsigned long long slice_change_volatile_ok = 0;


unsigned long long slice_out_que_num = 0;
unsigned long long slice_protect_err = 0;
unsigned long long virt_page_release_merge_ok = 0;
unsigned long long virt_page_release_merge_err = 0;
unsigned long long slice_que_change_watch = 0;
unsigned long long slice_mem_que_free =0;

unsigned long long slice_out_watch_que_num = 0;
unsigned long long slice_insert_sd_tree_err = 0;
unsigned long long slice_new_insert_num = 0 ;
unsigned long long slice_change_ref_err = 0;
unsigned long long slice_merge_num = 0;

unsigned long long slice_fix_free_num = 0;
unsigned long long slice_volatile_free_num = 0;
unsigned long long slice_other_free_num = 0;
unsigned long long slice_watch_free_num = 0;
unsigned long long slice_sys_free_num = 0;

unsigned long long slice_mem_watch_reuse= 0;


unsigned long long slice_file_protect_num = 0;
unsigned long long slice_file_chgref_num = 0;
unsigned long long slice_file_cow = 0;
unsigned long long slice_file_watch_chg = 0;
unsigned long long slice_file_fix_chg = 0;
extern unsigned long pone_file_watch ;
extern struct mm_struct *pone_debug_ljy_mm;
int ljy_printk_count = 0;
int global_pone_init = 0;

void slice_debug_area_insert(struct page *page);
int is_in_slice_debug_area(struct page *page);
extern void pone_linux_adp_init(void);
extern orderq_h_t *slice_watch_order_que[64];

typedef struct slice_order_watch_arg_tag
{
	void* old_slice;
	void* new_slice; 
}slice_order_watch_arg;

int is_pone_init(void)
{
	return global_pone_init;
}

void free_slice_state_control(slice_state_control_block *blk)
{
    if(blk)
    {
        int i;
        for( i = 0; i < blk->node_num ; i++)
        {
            if(blk->slice_node[i].slice_state_map)
            {
                vfree((void*)blk->slice_node[i].slice_state_map);
            }
        }
        kfree(blk);
    }   
}

int slice_state_map_init(slice_state_control_block *blk)
{
    int i = 0;
    for (i =0 ; i < blk->node_num ; i++)
    {
        long long slice_num = blk->slice_node[i].slice_num;
        long long mem_size = ((slice_num/SLICE_NUM_PER_UNIT)+1)*sizeof(unsigned long long);
        if(slice_num > 0)
        {
            printk("mem_size is 0x%llx,order is %d\r\n",mem_size,get_order(mem_size));
			blk->slice_node[i].slice_state_map = vmalloc(mem_size);    
            
			if(!blk->slice_node[i].slice_state_map)
            {
                free_slice_state_control(blk);
                blk = NULL;
				printk("vmalloc mem failed \r\n");
				return -1;
            }
			printk("nid is %d ,map addr is %p \r\n",i,blk->slice_node[i].slice_state_map);
            blk->slice_node[i].mem_size = mem_size;
			memset(blk->slice_node[i].slice_state_map,0,mem_size);           
        }
    }
	pone_linux_adp_init();
	global_block = blk;
	global_pone_init = 1;
    return 0;
}

int slice_state_control_init(void)
{
    int ret = -1;
	slice_state_control_block *blk;
    if(NULL == global_block)
    {
        blk  = kmalloc(PAGE_SIZE,GFP_KERNEL);
        if(blk)
        {
            ret = collect_sys_slice_info(blk);
            if( 0 == ret)
            {
                ret = slice_state_map_init(blk);            
            }
        }
    }
	if(0 == ret) 
	{
		return slice_deamon_init();
	}
    return ret;
}

static inline  unsigned long long* get_slice_state_unit_addr(unsigned int nid,unsigned long long slice_id)
{
    return global_block->slice_node[nid].slice_state_map + (slice_id/SLICE_NUM_PER_UNIT);
}

static inline unsigned long long get_slice_state_by_unit(unsigned long long state_unit,unsigned long slice_id)
{
    int offset = slice_id%SLICE_NUM_PER_UNIT;
    return  (state_unit >> (offset * SLICE_STATE_BITS))&SLICE_STATE_MASK;
}


unsigned long long get_slice_state_by_id(unsigned long slice_idx)
{
	unsigned int nid = 0;
	unsigned long long slice_id = 0;
	if(!is_pone_init())
	{
		return SLICE_NULL;
	}
	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	barrier();
	return get_slice_state(nid,slice_id);	
}
EXPORT_SYMBOL(get_slice_state_by_id);


unsigned long long construct_new_state(unsigned int nid,unsigned long long slice_id,unsigned long long new_state)
{
    unsigned long long low_unit = 0;
	unsigned long long high_unit = 0;
	unsigned long long state_unit = *(global_block->slice_node[nid].slice_state_map + (slice_id/SLICE_NUM_PER_UNIT));
    int offset = slice_id%SLICE_NUM_PER_UNIT;
	
	unsigned long long tmp = state_unit;
    int offsetbit = offset*SLICE_STATE_BITS;
	
	if(0 != offsetbit)
	{
		low_unit = (tmp << (SLICE_STATE_UNIT_BITS - offsetbit)) >> (SLICE_STATE_UNIT_BITS - offsetbit);
	}
	tmp = state_unit; 
	high_unit = (tmp  >> (offsetbit + SLICE_STATE_BITS))<<(offsetbit+SLICE_STATE_BITS);
    return  high_unit + (new_state << offsetbit) + low_unit;
}

int change_slice_state(unsigned int nid, unsigned long long slice_id,unsigned long long old_state,unsigned long long new_state)
{
    unsigned long long cur_state_unit;
    unsigned long long new_state_unit;

    volatile unsigned long long *state_unit_addr = get_slice_state_unit_addr(nid,slice_id);
    do
    {
        cur_state_unit = *state_unit_addr;

        if(old_state != get_slice_state_by_unit(cur_state_unit,slice_id))
        {
            return -1;
        }   

        new_state_unit = construct_new_state(nid,slice_id,new_state);
    
        if(cur_state_unit == atomic64_cmpxchg((atomic64_t*)state_unit_addr,cur_state_unit,new_state_unit))
        {
            if(SLICE_VOLATILE == new_state)
			{
				per_cpu(volatile_cnt,smp_processor_id())++;
			}
			break;
        }

    }while(1);
	
	return 0;    
}

int slice_que_resource_init(void)
{
    slice_que = lfrwq_init(8192*32,2048,50);
    if(!slice_que)
    {
        return -1;
    }
    slice_watch_que = lfrwq_init(8192*32,2048,50);
    if(!slice_watch_que)
    {
        vfree(slice_que);
        return -1;
    }
	slice_que_reader_init();
    return 0;
}

int pone_slice_add_mapcount_process(void  *slice)
{
	unsigned int nid;
	unsigned long long slice_id;
	unsigned long long cur_state;
	struct page *page = (struct page*)slice;
	unsigned long slice_idx = page_to_pfn(page);
	struct page *result = NULL;	
	
	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	do{

		barrier();
		cur_state = get_slice_state(nid,slice_id);	
		if(SLICE_NULL == cur_state)
		{
			break;
		}
		else if(SLICE_FIX == cur_state)
		{
			preempt_disable();
			result = insert_sd_tree(slice_idx);
			preempt_enable();
			if(result != page)
			{
				PONE_DEBUG("error in mapcount add proc\r\n");	
			}

			if(SLICE_FIX != get_slice_state(nid ,slice_id))
			{
				PONE_DEBUG("error in mapcount add proc\r\n");	
			}
			break;
		}
		else if(SLICE_WATCH == cur_state)
		{
			if(0 == change_slice_state(nid ,slice_id,SLICE_WATCH,SLICE_VOLATILE))
			{
				add_slice_volatile_cnt(nid,slice_id);
				break;
			}
		}
		else if(SLICE_ENQUE == cur_state)
		{
			if(0 == change_slice_state(nid ,slice_id,SLICE_ENQUE,SLICE_CHG))
			{
				break;
			}
		}
		else if(SLICE_WATCH_QUE == cur_state)
		{
			if(0 == change_slice_state(nid ,slice_id,SLICE_WATCH_QUE,SLICE_CHG))
			{
				break;
			}
		}
		else if(SLICE_IDLE == cur_state)
		{
			PONE_DEBUG("error in mapcount add proc\r\n");	
			break;
		}
		else
		{
			/*slice_chg ,slice_volatile */
			break;
		}

	}while(1);
	return PONE_OK;
}

unsigned long long slice_pre_fix_check_cnt = 0;
int  pone_slice_dec_mapcount_process(void *slice)
{
	unsigned int nid;
	unsigned long long slice_id;
	unsigned long long cur_state;
	struct page *page = (struct page*)slice;
	unsigned long slice_idx = page_to_pfn(page);
	
	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	do{

		barrier();
		cur_state = get_slice_state(nid,slice_id);	
		if(SLICE_FIX == cur_state)
		{
			if(0 == change_slice_state(nid ,slice_id,SLICE_FIX,SLICE_FIX))
			{
				atomic64_add(1,(atomic64_t*)&slice_pre_fix_check_cnt);
				delete_sd_tree(slice_idx,SLICE_FIX);
				break;
			}
		}
		else if(SLICE_WATCH == cur_state)
		{
			if(0 == change_slice_state(nid ,slice_id,SLICE_WATCH,SLICE_VOLATILE))
			{
				add_slice_volatile_cnt(nid,slice_id);
				break;
			}
		}
		else if(SLICE_WATCH_QUE == cur_state || SLICE_ENQUE == cur_state)
		{
			if(0 == change_slice_state(nid ,slice_id,cur_state,SLICE_CHG))
			{
				break;
			}
		}
		else
		{
			/*slice_volatile ,chg ,idle,null*/	
			break;
		}
	}while(1);
	return PONE_OK;
}

/*proc new page in pone ,change null state to volatile state,deamon scan in next period*/
int pone_slice_alloc_process(void *slice)
{
	unsigned long long cur_state;
	unsigned int  nid ;
	unsigned long long slice_id ;
	int ret = PONE_ERR;
	struct page *page = (struct page*)slice;
	unsigned long slice_idx = page_to_pfn(page);

	if(!process_slice_check())
	{
		return PONE_ERR;
	}

	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	
	atomic64_add(1,(atomic64_t*)&slice_alloc_num);
		
	do
	{
		cur_state = get_slice_state(nid,slice_id);
		if(SLICE_NULL != cur_state){
			PONE_DEBUG("slice state is %lld ,err state\r\n",cur_state);
			break;
		}
		if(0 != atomic_read(&page->_mapcount))
		{
			PONE_DEBUG("slice state is %lld ,err mapcount \r\n",cur_state);
			break;
		}

		add_slice_volatile_cnt(nid,slice_id);
		if(0 == change_slice_state(nid,slice_id,SLICE_NULL,SLICE_VOLATILE)) {
			ret = PONE_OK;
			atomic64_add(1,(atomic64_t*)&slice_change_volatile_ok);
			break;
		}

	}while(1);
	return ret;	
}

int pone_slice_free_check_process(void *slice)
{
	unsigned long long cur_state;
	unsigned int  nid ;
	unsigned long long slice_id ;
	int ret = PONE_ERR;
	struct page *page = (struct page *)slice;
	unsigned long slice_idx = page_to_pfn(page);
	
	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	/* enter this function ,the page count is zero*/
	
	do
	{
		/* return value PONE_OK ,make page free by linux system */
		cur_state = get_slice_state(nid,slice_id);
		if(cur_state != SLICE_NULL)
		{
			if(SLICE_IDLE == cur_state)
			{
				if(0 == change_slice_state(nid,slice_id,SLICE_IDLE,SLICE_NULL))
				{
					ret = PONE_OK;
					clear_deamon_cnt(nid,slice_id);
					break;
				}
			}
			else if(SLICE_FIX == cur_state)
			{
				if(0 == change_slice_state(nid,slice_id,SLICE_FIX,SLICE_NULL))
				{
					atomic64_add(1,(atomic64_t*)&slice_fix_free_num);
					ret = PONE_OK;
					clear_deamon_cnt(nid,slice_id);
					break;
				}

			}
			else if(SLICE_VOLATILE == cur_state)
			{
				if(0 == change_slice_state(nid,slice_id,SLICE_VOLATILE,SLICE_NULL))
				{
					atomic64_add(1,(atomic64_t*)&slice_volatile_free_num);
					ret = PONE_OK;
					clear_deamon_cnt(nid,slice_id);
					break;
				}
			}
			else if(SLICE_WATCH == cur_state)
			{
				if(0 == change_slice_state(nid,slice_id,SLICE_WATCH,SLICE_NULL))
				{
					atomic64_add(1,(atomic64_t*)&slice_watch_free_num);
					ret = PONE_OK;
					clear_deamon_cnt(nid,slice_id);
					break;
				}
			}
			else 
			{
				/*slice state is SLICE_ENQUE,SLICE_WATCH_QUE,SLICE_CHG*/
				if(0 == change_slice_state(nid,slice_id,cur_state,SLICE_IDLE))
				{
						atomic64_add(1,(atomic64_t*)&slice_other_free_num);
						ret = PONE_ERR;/*sys do not free this page*/
						clear_deamon_cnt(nid,slice_id);
						break;
				}
			}
		}
		else
		{
			/* 0 make linux sys free this page*/
			ret = PONE_OK;
			break;	
		}

	}while(1); 

	return ret;
}
int pone_slice_watched_page(void *slice)
{
	struct page *page = (struct page *)slice;
	if(get_slice_state_by_id(page_to_pfn(page)) != SLICE_NULL)
	{
		return PONE_OK;
	}
	return PONE_ERR;
}
int pone_slice_can_reuse(void *slice)
{
	unsigned long long cur_state;
	unsigned int  nid ;
	unsigned long long slice_id ;
	struct page *page = (struct page *)slice;
	unsigned long slice_idx = page_to_pfn(page);
	
	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	
	do
	{
		barrier();
		cur_state = get_slice_state(nid,slice_id);
		if((SLICE_NULL == cur_state) || (SLICE_VOLATILE == cur_state))
		{
			return PONE_OK;
		}
		else if((cur_state == SLICE_FIX)|| (cur_state == SLICE_WATCH_QUE)|| (cur_state == SLICE_ENQUE))
		{
			return PONE_ERR;
		}
		else if(SLICE_WATCH == cur_state)
		{
			if(0 == change_slice_state(nid,slice_id,SLICE_WATCH,SLICE_VOLATILE))
			{
				add_slice_volatile_cnt(nid,slice_id);
				atomic64_add(1,(atomic64_t *)&slice_mem_watch_reuse);
				return PONE_OK;
			}
		}
		else
		{
			/*state chg,idle*/
			return PONE_ERR;
		}

	}while(1);

	return PONE_ERR;
}

int pone_slice_mark_volatile_cnt(void *slice,void *slice_new)
{
	struct page *page = (struct page*)slice;
	struct page *page_new = (struct page*)slice_new;
	if(!process_slice_check())
	{
		return PONE_ERR;
	}
	mark_volatile_cnt_in_wcopy(page_to_pfn(page),page_to_pfn(page_new));
	return PONE_OK;
}


int pone_process_watch_que_state(void *args)
{
	slice_order_watch_arg *arg = args;
	struct page *old_page = (struct page *)arg->old_slice;
	struct page *new_page = (struct page *)arg->new_slice;
	unsigned long long cur_state;
	unsigned int  nid ;
	unsigned long long slice_id ;
	int ret = PONE_ERR;
	unsigned long slice_idx = page_to_pfn(old_page);
	unsigned long new_slice_idx = page_to_pfn(new_page);
	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	do
	{
		cur_state = get_slice_state(nid,slice_id);
		
		if(SLICE_IDLE == cur_state){
			/*page free by sys*/
			free_slice(slice_idx);
			atomic64_add(1,(atomic64_t*)&slice_mem_que_free);
			ret = PONE_OK;
			break;
		}
		else if(SLICE_WATCH_QUE == cur_state)
		{
			if(SLICE_OK == change_reverse_ref(slice_idx,new_slice_idx))
			{
				atomic64_add(1,(atomic64_t*)&slice_merge_num);
				while(0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_IDLE));
				put_page(old_page);
				ret = PONE_OK;
			}
			else
			{
				atomic64_add(1,(atomic64_t*)&slice_change_ref_err);
				while (0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_VOLATILE));
				add_slice_volatile_cnt(nid,slice_id);
				delete_sd_tree(slice_idx,SLICE_FIX);
				put_page(old_page);
			}
		}
		else if(SLICE_CHG == cur_state)
		{
			
			if(0 == change_slice_state(nid,slice_id,SLICE_CHG,SLICE_VOLATILE)){
				ret = PONE_OK;
				break;
			}                        
		}
		else
		{
			PONE_DEBUG("slice state err in out que %lld\r\n",cur_state);
			break;
		}
		
	}while(1);

	kfree(args);
	return ret ;

}

int pone_process_deamon_que_state(void *slice)
{
	unsigned long long cur_state;
	unsigned int  nid ;
	unsigned long long slice_id ;
	struct page  *result = NULL;
	int ret = PONE_ERR;
	struct page *org_slice = (struct page*)slice;
	void *page_addr = NULL;
	unsigned long slice_idx = page_to_pfn(org_slice);	
	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	
	do
	{
		cur_state = get_slice_state(nid,slice_id);
		
		if(SLICE_IDLE == cur_state){
			/*page free by sys*/
			free_slice(slice_idx);
			atomic64_add(1,(atomic64_t*)&slice_mem_que_free);
			ret = PONE_OK;
			break;
		}
		else if (SLICE_WATCH_QUE == cur_state)
		{
			atomic64_add(1 ,(atomic64_t *)&slice_out_watch_que_num);
			if(!atomic_inc_not_zero(&org_slice->_count))
			{
				continue;
			}

			if(0 != atomic_read(&org_slice->_mapcount))
			{
				continue;
			}
			result = insert_sd_tree(slice_idx);
			if(NULL == result){
				while (0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_VOLATILE));
				put_page(org_slice);
				atomic64_add(1,(atomic64_t*)&slice_insert_sd_tree_err);
				add_slice_volatile_cnt(nid,slice_id);
				break;
			}
				
			if(result == org_slice){
				if(0 == change_slice_state(nid,slice_id,SLICE_WATCH_QUE,SLICE_FIX)){
					atomic64_add(1,(atomic64_t*)&slice_new_insert_num);
					ret = PONE_OK;
					break;
				}
				else
				{
					delete_sd_tree(slice_idx,SLICE_WATCH);
					continue;
				}
			}
			else{
				unsigned long que_id;	
				que_id = pone_get_slice_que_id(org_slice);
				if((-1 == que_id)|| (0 == que_id))
				{
					delete_sd_tree(slice_idx,SLICE_WATCH);
					continue;
				}
				que_id = hash_64(que_id,48);
				que_id = pone_que_stat_lookup(que_id);
				if((que_id*2 +1)  == smp_processor_id())
				{
					if(SLICE_OK == change_reverse_ref(slice_idx,page_to_pfn(result)))
					{
						atomic64_add(1,(atomic64_t*)&slice_merge_num);
						while(0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_IDLE));
						put_page(org_slice);
						ret = 0;
					}
					else
					{
						atomic64_add(1,(atomic64_t*)&slice_change_ref_err);
						while (0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_VOLATILE));
						add_slice_volatile_cnt(nid,slice_id);
						delete_sd_tree(slice_idx,SLICE_FIX);
						put_page(org_slice);
					}
				}
				else
				{
					slice_order_watch_arg *arg = kmalloc(sizeof(slice_order_watch_arg),GFP_ATOMIC);
					if(NULL == arg)
					{
						BUG();
					}
					arg->old_slice = org_slice;
					arg->new_slice = result;
					lfo_write(slice_watch_order_que[que_id],49,(unsigned long )arg);
				}
				break;
			}
		}
		else if(SLICE_CHG == cur_state){
			if(0 == change_slice_state(nid,slice_id,SLICE_CHG,SLICE_VOLATILE)){
				ret = 0;
				break;
			}                        
		}
		else
		{

			PONE_DEBUG("slice state err in out que %lld\r\n",cur_state);
			break;
		}
	
	}while(1);
	return ret;
}

int pone_process_que_state(void *slice)
{
	unsigned long long cur_state;
	unsigned int  nid ;
	unsigned long long slice_id ;
	struct page  *result = NULL;
	int ret = PONE_ERR;
	struct page *org_slice = (struct page*)slice;
	void *page_addr = NULL;
	unsigned long slice_idx = page_to_pfn(org_slice);	
	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	
	do
	{
		cur_state = get_slice_state(nid,slice_id);
		
		if(SLICE_IDLE == cur_state){
			/*page free by sys*/
			free_slice(slice_idx);
			atomic64_add(1,(atomic64_t*)&slice_mem_que_free);
			ret = PONE_OK;
			break;
		}
		else if(SLICE_ENQUE == cur_state){

			unsigned long long time_begin;
			int virt_release_page = 0;
			page_addr = kmap_atomic(org_slice);
			atomic64_add(1,(atomic64_t *)&slice_out_que_num);	
			if(0 == is_virt_page_release(page_addr))
			{
				time_begin = rdtsc();
				ret =  process_virt_page_release(page_addr,org_slice);
				if(VIRT_MEM_OK == ret)
				{
					PONE_TIMEPOINT_SET(slice_page_recycle,(rdtsc() - time_begin));
					atomic64_add(1,(atomic64_t*)&virt_page_release_merge_ok);
					if(0 !=  change_slice_state(nid,slice_id,SLICE_CHG,SLICE_NULL))
					{
						PONE_DEBUG("bug bug bug bug bug \r\n");
						PONE_DEBUG("page state is %lld\r\n",get_slice_state(nid,slice_id));
						while(1);
					}
					kunmap_atomic(page_addr);
					put_page(org_slice);
					ret = 0;
					break;
				}
				else if (VIRT_MEM_RETRY == ret)
				{
					if(0 ==  change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_VOLATILE))
					{
						kunmap_atomic(page_addr);
						add_slice_volatile_cnt(nid,slice_id);
						ret = 0;
						break;
					}
					else
					{
						continue;
					}
				}
				else
				{
					/* page recycle fail ,continue page merge */
					atomic64_add(1,(atomic64_t*)&virt_page_release_merge_err);
				}
				virt_release_page = 1;
			}

			if(0 != atomic_read(&org_slice->_mapcount))
			{
				kunmap_atomic(page_addr);
				if (0 == change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_VOLATILE))
				{
					add_slice_volatile_cnt(nid,slice_id);
					break;
				}
				else
				{
					continue;
				}
			}
			
			if(SLICE_OK == make_slice_wprotect(slice_idx)){
				if(!virt_release_page)
				if(0 == is_virt_page_release(page_addr))
				{
					add_slice_volatile_cnt(nid,slice_id);
					if(0 ==  change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_VOLATILE))
					{
						kunmap_atomic(page_addr);
						ret = 0;
						break;
					}
				}
				kunmap_atomic(page_addr);
				add_slice_volatile_cnt(nid,slice_id);
				if(0 == change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_WATCH)){
					atomic64_add(1,(atomic64_t *)&slice_que_change_watch);
					break;
				}
				else
				{
					continue;
				}
			}
		    else
			{
				kunmap_atomic(page_addr);
				atomic64_add(1,(atomic64_t*)&slice_protect_err);
				/*make slice state volatile*/
				do{	
					/*make slice wprotect err or inq err*/
					cur_state = get_slice_state(nid,slice_id);

					if((SLICE_IDLE != cur_state) && (SLICE_ENQUE != cur_state)&&(SLICE_CHG != cur_state))
					{
						PONE_DEBUG("slice state err in out que %lld \r\n",cur_state);
						break;
					}
					if(SLICE_IDLE == cur_state)
					{
						free_slice(slice_idx);
						break;
					}
					if (0 == change_slice_state(nid,slice_id,cur_state,SLICE_VOLATILE))
					{
						add_slice_volatile_cnt(nid,slice_id);
						break;
					}
			
				}while(1);
				break;
			}
		}
		else if(SLICE_CHG == cur_state){
			
			if(0 == change_slice_state(nid,slice_id,SLICE_CHG,SLICE_VOLATILE)){
				ret = 0;
				break;
			}                        
		}
		else {
			PONE_DEBUG("slice state err in out que %lld\r\n",cur_state);
			break;
		
		}
		
	}while(1);
	
	return ret;
}


int process_slice_check(void)
{
	char *src = "qemu-system-x86";
	if(!is_pone_init())
		return 0;

	if(!pone_run)
	{
		return 0;
	}
	#if 0

	if(is_in_mem_pool(current->mm))
	{
		return 1;
	}
	return 0;
	#endif
	#if 1
	if(0 == strcmp(current->comm,src))
	{	
	#if 0		
		if(!pone_debug_ljy_mm) 
		{
			pone_debug_ljy_mm = current->mm;
		}
	#endif
		return 1;
	}	
	else
		return 0;
	#endif
	//return 1;
}

int process_slice_file_check(unsigned long i_ino)
{
	if(i_ino == pone_file_watch)
	return 1;

	return 0;
}

int process_state_que(lfrwq_t *qh,lfrwq_reader *reader,int op)
{
	void *msg = NULL;
	int process_cnt = 0;
	unsigned long long r_idx = 0;
	int ret = -1;
	unsigned long long time_begin =0;
	if((!qh) || (!reader))
	{
		return -1;
	}    

	do
	{
		if(reader->local_idx != -1)
		{
			msg = NULL;
			handle_msg_idx:
			if(reader->local_idx > lfrwq_get_r_max_idx(qh))
			{
				ret= -1;
				break;
			}
			
			r_idx = lfrwq_deq_by_idx(qh,reader->local_idx,&msg);
			if(!msg)
			{
				ret = -1;
				break;
			}
			reader->local_idx = -1;
			goto handle_msg;                
		}
		
		if(!reader->local_pmt)
		{
			reader->local_pmt = lfrwq_get_rpermit(qh);
		}

		if(!reader->local_pmt)
		{
			ret = -1;
			break;
		} 

		while(reader->local_pmt)
		{
            msg = NULL;
			r_idx = lfrwq_alloc_r_idx(qh);
            if(r_idx > lfrwq_get_r_max_idx(qh))
            {
                reader->local_idx = r_idx;
                goto handle_msg_idx;        
            }

            r_idx = lfrwq_deq_by_idx(qh,r_idx,&msg);

            if(!msg)
            {
                reader->local_idx = r_idx;
                goto handle_msg_idx;
            }
            else
            {
                handle_msg:               
                reader->local_pmt--;
				pone_process_que_state(msg);
				
                if(0 == reader->r_cnt)
                {
                    reader->local_blk = lfrwq_get_blk_idx(qh,r_idx);
                }  
                else
                {
                    if(lfrwq_get_blk_idx(qh,r_idx) != reader->local_blk)
                    {
                        lfrwq_add_rcnt(qh,reader->r_cnt,reader->local_blk);
                        reader->r_cnt = 0 ;
                        reader->local_blk = lfrwq_get_blk_idx(qh,r_idx);    
                    }
                }
                reader->r_cnt++;
#if 1	
				if(process_cnt++ >10000)
				{
					ret = -2;
					break;
				}
#endif
			}
        }
        
        if(reader->r_cnt)
        {
            lfrwq_add_rcnt(qh,reader->r_cnt,reader->local_blk);
            reader->r_cnt = 0 ;
        }
		if(process_cnt++ >2000)
		{
			ret = -2;
			break;
		}

    }while(1);
    
    return ret;
}

unsigned int slice_debug_area_cnt = 0;
struct page **debug_page_ptr = NULL;
int slice_area_size = 10000;
int slice_debug_area_init(void)
{
	if(NULL == debug_page_ptr)
	{
		debug_page_ptr = vmalloc(slice_area_size *sizeof(struct page*));
		if(debug_page_ptr)
		{
			memset(debug_page_ptr,0,slice_area_size*sizeof(struct page*));
			return 0;
		}

	}
	return -1;
}

void slice_debug_area_insert(struct page *page)
{
	int index = atomic_add_return(1,(atomic_t*)&slice_debug_area_cnt)-1;
	index = index%slice_area_size;
	debug_page_ptr[index] = page;
}

int is_in_slice_debug_area(struct page *page)
{
	int i = 0;
	if(!debug_page_ptr)
	{
		return 0;
	}
	for(i = 0; i < slice_area_size; i++)
	{
		if(debug_page_ptr[i] == page)
		{
			return 1;
		}
	}
	return 0;
}

int slice_debug_area_show(void)
{
	int i = 0;
	int loop_cnt = slice_debug_area_cnt;
	unsigned int nid;
	unsigned long long slice_id;
	unsigned long long slice_idx;
	unsigned long long cur_state;
	printk("slice insert cnt %d\r\n",slice_debug_area_cnt);
	if(debug_page_ptr == NULL)
	{
		return -1;
	}
	if(slice_debug_area_cnt > slice_area_size)
	{
		loop_cnt = slice_area_size;
	}
	for(i = 0;i < loop_cnt;i++)
	{
		slice_idx = page_to_pfn(debug_page_ptr[i]);
		nid = slice_idx_to_node(slice_idx);
		slice_id = slice_nr_in_node(nid,slice_idx);
		printk("page ptr is %p,page cnt is %d ,map_cnt is %d ,cur_state is %lld\r\n",debug_page_ptr[i],atomic_read(&debug_page_ptr[i]->_count),atomic_read(&debug_page_ptr[i]->_mapcount),get_slice_state(nid,slice_id));
	}
	return 0;
}










