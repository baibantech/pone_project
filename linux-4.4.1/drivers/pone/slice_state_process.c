#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm-generic/pgtable.h>
#include <linux/bootmem.h>
#include <linux/ksm.h>
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include <pone/lf_rwq.h>
#include "vector.h"
#include "chunk.h"
#include  "splitter_adp.h"
#include <pone/virt_release.h>
#include "pone_time.h"

lfrwq_t *slice_que = NULL;
lfrwq_t *slice_watch_que = NULL;
slice_state_control_block *global_block = NULL;
unsigned long long alloc_check = 0;
int pone_run = 1;

unsigned long slice_watch_que_debug = 1;
unsigned long slice_que_debug = 1;


unsigned long long slice_alloc_num = 0;
unsigned long long slice_in_que_ok =0;
unsigned long long slice_in_que_err = 0;

unsigned long long slice_out_que_num = 0;
unsigned long long slice_protect_err = 0;
unsigned long long slice_in_watch_que_ok = 0;
unsigned long long slice_in_watch_que_err = 0;
unsigned long long slice_mem_que_free =0;

unsigned long long slice_map_cnt_err = 0;
unsigned long long slice_insert_sd_tree_err = 0;
unsigned long long slice_new_insert_num = 0 ;
unsigned long long slice_change_ref_err = 0;
unsigned long long slice_merge_num = 0;
unsigned long long slice_mem_watch_free = 0;

unsigned long long slice_fix_free_num = 0;
unsigned long long slice_volatile_free_num = 0;
unsigned long long slice_other_free_num = 0;
unsigned long long slice_sys_free_num = 0;

unsigned long long slice_mem_watch_change= 0;
unsigned long long slice_mem_fix_change = 0;

unsigned long long slice_volatile_in_que_err = 0;
unsigned long long slice_volatile_in_que_ok = 0;


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
			//blk->slice_node[i].slice_state_map = kmalloc(mem_size,GFP_KERNEL);    
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
	//printk("offsetbit is %d\r\n",offset);
	//printk("state unit is 0x%llx\r\n",state_unit);
	//printk("low unit is 0x%llx\r\n",low_unit);
	//printk("high unit is 0x%llx\r\n",high_unit);
    return  high_unit + (new_state << offsetbit) + low_unit;
}

int change_slice_state(unsigned int nid, unsigned long long slice_id,unsigned long long old_state,unsigned long long new_state)
{
    unsigned long long cur_state_unit;
    unsigned long long new_state_unit;

    volatile unsigned long long *state_unit_addr = get_slice_state_unit_addr(nid,slice_id);
//	printk("state unit addr is %p\r\n",state_unit_addr);
//	printk("state unit mem is 0x%llx\r\n",*state_unit_addr);
    do
    {
        cur_state_unit = *state_unit_addr;

        if(old_state != get_slice_state_by_unit(cur_state_unit,slice_id))
        {
            //printk("error slice state in slice alloc %lld\r\n",cur_state_unit);
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
	
//	printk("state unit mem change is 0x%llx\r\n",*state_unit_addr);
//	printk("slice state is %lld\r\n",get_slice_state(nid,slice_id));
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

void slice_mapcount_add_process(void *data)
{
	unsigned int nid;
	unsigned long long slice_id;
	int i = 0;
	unsigned long long cur_state;
	struct page *org_page = (struct page*)data;
	unsigned long slice_idx = page_to_pfn(org_page);
	struct page *result = NULL;	
	if(!is_pone_init())
	{
		return;
	}


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
			if(result != org_page)
			{
				printk("error in mapcount add proc line %d\r\n",__LINE__);	
			}

			if(SLICE_FIX != get_slice_state(nid ,slice_id))
			{
				printk("error in mapcount add proc line %d\r\n",__LINE__);	
			}
			break;
		}
		else if(SLICE_WATCH == cur_state)
		{
			if(0 == change_slice_state(nid ,slice_id,SLICE_WATCH,SLICE_CHG))
			{
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
		else if(SLICE_IDLE == cur_state)
		{
			printk("error in mapcount add proc line %d\r\n",__LINE__);	
		}
		else
		{
			/*slice_chg ,slice_volatile */
			break;
		}

	}while(1);
}

unsigned long long slice_pre_fix_check_cnt = 0;
void pre_fix_slice_check(void *data)
{
	unsigned int nid;
	unsigned long long slice_id;
	int i = 0;
	unsigned long long cur_state;
	struct page *org_page = (struct page*)data;
	unsigned long slice_idx = page_to_pfn(org_page);
	
	if(!is_pone_init())
	{
		return;
	}

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
			if(0 == change_slice_state(nid ,slice_id,SLICE_WATCH,SLICE_CHG))
			{
				break;
			}
		}
		else
		{
			if(0 == change_slice_state(nid ,slice_id,cur_state,cur_state))
			{
				break;
			}
		}
	}while(1);
}

EXPORT_SYMBOL(pre_fix_slice_check);

unsigned long long virt_page_release_merge_ok = 0;
unsigned long long virt_page_release_merge_err = 0;
unsigned long long slice_change_volatile_ok = 0;
int process_slice_state(unsigned long slice_idx ,int op,void *data,unsigned long  que)
{
    unsigned long long cur_state;
    unsigned int  nid ;
    unsigned long long slice_id ;
	struct page  *result = NULL;
	int ret = -1;
	struct page *org_slice = pfn_to_page(slice_idx);
	if(!is_pone_init())
	{
		return 0;
	}

	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
		
	switch(op)
    {
        case SLICE_ALLOC:
		{
			atomic64_add(1,(atomic64_t*)&slice_alloc_num);
			
			slice_debug_area_insert(org_slice);

			do
			{
				cur_state = get_slice_state(nid,slice_id);
				if(SLICE_NULL != cur_state){
					printk("slice state is %lld ,err in slice alloc\r\n",cur_state);
					break;
				}
				if(0 != atomic_read(&org_slice->_mapcount))
				{
					printk("slice state is %lld ,err in map slice alloc\r\n",cur_state);
					break;
				}
#if 1

				add_slice_volatile_cnt(nid,slice_id);
				if(0 == change_slice_state(nid,slice_id,SLICE_NULL,SLICE_VOLATILE)) {
					ret = 0;
					atomic64_add(1,(atomic64_t*)&slice_change_volatile_ok);
					break;
				}
#endif
#if 0
				if(0 == change_slice_state(nid,slice_id,SLICE_NULL,SLICE_ENQUE)) {
					
#ifdef SLICE_OP_CLUSTER_QUE
					if(-1 == lfrwq_in_cluster_que(data,que))
#else
					if(-1 == lfrwq_inq(slice_que,data))
#endif
					{
						atomic64_add(1,(atomic64_t*)&slice_in_que_err);
						while(0 != change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_VOLATILE));
					}
					else{
						atomic64_add(1,(atomic64_t*)&slice_in_que_ok);
						ret = 0;
					}
					break;
				}

#endif
			}while(1);
			break;
		}
        
        case SLICE_FREE: /*page refcount  is 0*/
		{
			do
			{
				/* return value 0 ,make page free by linux system*/
				cur_state = get_slice_state(nid,slice_id);
				if(cur_state != SLICE_NULL)
				{
					if(SLICE_IDLE == cur_state)
					{
						if(0 == change_slice_state(nid,slice_id,SLICE_IDLE,SLICE_NULL))
						{
							ret = 0;
					clear_deamon_cnt(nid,slice_id);
							break;
						}
					}
					else if(SLICE_FIX == cur_state)
					{
						if(0 == change_slice_state(nid,slice_id,SLICE_FIX,SLICE_NULL))
						{
							//printk("fix free slice is %p,mapcount is %d\r\n",org_slice,atomic_read(&org_slice->_mapcount));
							atomic64_add(1,(atomic64_t*)&slice_fix_free_num);
							ret = 0;
					clear_deamon_cnt(nid,slice_id);
							break;
						}

					}else if(SLICE_VOLATILE == cur_state){
						if(0 == change_slice_state(nid,slice_id,SLICE_VOLATILE,SLICE_NULL))
						{
							atomic64_add(1,(atomic64_t*)&slice_volatile_free_num);
							ret = 0;
					clear_deamon_cnt(nid,slice_id);
							break;
						}
					}
					else 
					{
						/*slice state is SLICE_ENQUE,SLICE_WATCH,SLICE_WATCH_CHG*/
						if(0 == change_slice_state(nid,slice_id,cur_state,SLICE_IDLE))
						{
							atomic64_add(1,(atomic64_t*)&slice_other_free_num);
							ret = -1 ;/*sys do not free this page*/
					clear_deamon_cnt(nid,slice_id);
							break;
						}
					}
				}
				else
				{
					/* 0 make linux sys free this page*/
					ret = 0;
					break;	
				}

			}while(1);            

			break;
		}

		case SLICE_CHANGE:/*cow excption:reuse*/
		{
			if(get_slice_state_by_id(slice_idx)!= SLICE_NULL)
			printk("change watch status is err\r\n");
			break;	
		}
        case SLICE_OUT_QUE:
		{
			void *page_addr = NULL;

			atomic64_add(1,(atomic64_t*)&slice_out_que_num);
			do
			{
				cur_state = get_slice_state(nid,slice_id);
				
				if(SLICE_IDLE == cur_state){
					/*page free by sys*/
					free_slice(slice_idx);
					atomic64_add(1,(atomic64_t*)&slice_mem_que_free);
					ret = 0;
					break;
				}
				else if(SLICE_ENQUE == cur_state){

					unsigned long long time_begin;
					int virt_release_page = 0;
					page_addr = kmap_atomic(org_slice);
#if 1
					if(0 == is_virt_page_release(page_addr))
					{
						time_begin = rdtsc();
						if(0 == process_virt_page_release(page_addr,org_slice))
						{
							PONE_TIMEPOINT_SET(slice_page_recycle,(rdtsc() - time_begin));
							atomic64_add(1,(atomic64_t*)&virt_page_release_merge_ok);
							
							if(0 !=  change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_NULL))
							{
								printk("bug bug bug bug bug \r\n");
								while(1);
							}
							kunmap_atomic(page_addr);
							put_page(org_slice);
							ret = 0;
							break;

						}
						else
						{
							atomic64_add(1,(atomic64_t*)&virt_page_release_merge_err);
						}
						virt_release_page = 1;
					}
#endif

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

#if 1
					if(0 == change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_WATCH)){
						time_begin = rdtsc();	
						if(SLICE_OK == make_slice_wprotect(slice_idx)){
#if 1
							if(!virt_release_page)
							if(0 == is_virt_page_release(page_addr))
							{
								if(0 ==  change_slice_state(nid,slice_id,SLICE_WATCH,SLICE_VOLATILE))
								{
									kunmap_atomic(page_addr);
									add_slice_volatile_cnt(nid,slice_id);
									ret = 0;
									break;
								}
							
							}
#endif
							kunmap_atomic(page_addr);
								
							PONE_TIMEPOINT_SET(slice_protect_ok,(rdtsc()- time_begin));
							time_begin = rdtsc();
#if 1
#ifdef SLICE_OP_CLUSTER_QUE
							if(-1 ==  lfrwq_in_cluster_watch_que(data,smp_processor_id()/2))
#else
							if(-1 == lfrwq_inq(slice_watch_que,data))
#endif
							{
								atomic64_add(1,(atomic64_t*)&slice_in_watch_que_err);
							}
							else
							{
								PONE_TIMEPOINT_SET(slice_in_watch_que,(rdtsc()- time_begin));
								atomic64_add(1,(atomic64_t*)&slice_in_watch_que_ok);
								ret = 0;
								
								break;
							}
#else
							atomic64_add(1,(atomic64_t*)&slice_in_watch_que_ok);
							ret =0;
							break;
#endif
						
						}
						else {
							
							atomic64_add(1,(atomic64_t*)&slice_protect_err);
						}
						
						/*make slice state volatile*/
						do{	
							/*make slice wprotect err or inq err*/
							cur_state = get_slice_state(nid,slice_id);

							if((SLICE_IDLE != cur_state) && (SLICE_WATCH != cur_state))
							{
								printk("slice state err in out que %lld \r\n",cur_state);
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
					else
						kunmap_atomic(page_addr);
#endif
				}
				else if(SLICE_CHG == cur_state){
					
					if(0 == change_slice_state(nid,slice_id,SLICE_CHG,SLICE_VOLATILE)){
						ret = 0;
						break;
					}                        
				}
				else
				{
					printk("slice_state is %lld err in out que\r\n",cur_state);
					break;
				}

			}while(1); 
            
			break;     
		}
        
        case SLICE_OUT_WATCH_QUE:
		{    
			void *page_addr = NULL;
			unsigned long long time_begin = 0;
			do
			{
				cur_state = get_slice_state(nid,slice_id);
							 
				if(!slice_watch_que_debug) return 0;

				if(SLICE_IDLE == cur_state){
					free_slice(slice_idx);
					ret = 0;
					atomic64_add(1,(atomic64_t*)&slice_mem_watch_free);
					break;
				}
				else if (SLICE_WATCH == cur_state)
				{
					if(!atomic_inc_not_zero(&org_slice->_count))
					{
						continue;
					}

					if(0 != atomic_read(&org_slice->_mapcount))
					{
						continue;
					}
					#if 0
					org_slice->page_mem = kmalloc(PAGE_SIZE,GFP_ATOMIC);
					if(!org_slice->page_mem)
					{
						printk("kmalloc err\r\n");
					}
					else
					{
						page_addr = kmap_atomic(org_slice);
						memcpy(org_slice->page_mem,page_addr,PAGE_SIZE);
						kunmap_atomic(page_addr);
					}
#endif
					time_begin = rdtsc();
					result = insert_sd_tree(slice_idx);
					if(NULL == result){
						while (0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_VOLATILE));
						put_page(org_slice);
						atomic64_add(1,(atomic64_t*)&slice_insert_sd_tree_err);
						add_slice_volatile_cnt(nid,slice_id);
						break;
					}
						
					if(result == org_slice){
						PONE_TIMEPOINT_SET(slice_insert_tree,(rdtsc() - time_begin));
						if(0 == change_slice_state(nid,slice_id,SLICE_WATCH,SLICE_FIX)){
							atomic64_add(1,(atomic64_t*)&slice_new_insert_num);
							//printk("slice_idx is new insert ok\r\n");
							ret = 0;
							break;
						}
						else
						{
							delete_sd_tree(slice_idx,SLICE_WATCH);
							continue;
						}
					}
					else{
						//printk("slice_idx is same to new_slice %lld,%lld\r\n",slice_idx,pone->slice_idx);
						
						PONE_TIMEPOINT_SET(slice_merge_tree,(rdtsc() - time_begin));
						time_begin = rdtsc();
						if(SLICE_OK == change_reverse_ref(slice_idx,page_to_pfn(result)))
						{
							PONE_TIMEPOINT_SET(slice_changeref_ok,(rdtsc() - time_begin));
							atomic64_add(1,(atomic64_t*)&slice_merge_num);
							while(0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_IDLE));
							put_page(org_slice);
							ret = 0;
						}
						else
						{
							//printk("change ref ret err\r\n");
							atomic64_add(1,(atomic64_t*)&slice_change_ref_err);
							while (0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_VOLATILE));
							add_slice_volatile_cnt(nid,slice_id);
							delete_sd_tree(slice_idx,SLICE_FIX);
							put_page(org_slice);
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
				else {
					printk("slice state err in out watch que %lld\r\n",cur_state);
					break;
				}
			}while(1);
			break;
		}

		case SLICE_OUT_DEAMON_QUE:
		{
			long que_id;
			do
			{
				cur_state = get_slice_state(nid,slice_id);
			
				if(SLICE_ENQUE == cur_state)
				{
#ifdef SLICE_OP_CLUSTER_QUE
					que_id = pone_get_slice_que_id(org_slice);
					if(-1 == que_id)
					{
						goto que_err;
					}
					if(-1 == lfrwq_in_cluster_que(data,que_id))
#else
					if(-1 == lfrwq_inq(slice_que,data))
#endif
					{
						atomic64_add(1,(atomic64_t*)&slice_volatile_in_que_err);
que_err:
						do
						{
							cur_state = get_slice_state(nid,slice_id);
							if(SLICE_ENQUE == cur_state)
							{
								if(0 == change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_VOLATILE))
								{
									ret = 0;
									break;
								}
							}
							else if(SLICE_IDLE == cur_state)
							{	
								free_slice(slice_idx);
								ret =0;
								break;
							}
							else if(SLICE_CHG == cur_state)
							{
								if(0 == change_slice_state(nid,slice_id,SLICE_CHG,SLICE_VOLATILE))
								{
									ret = 0;
									break;
								}
							}
							else
							{
								printk("err state in volatile proc %lld\r\n",cur_state);
								break;
							}

						}while(1);
					}
					else
					{
						atomic64_add(1,(atomic64_t*)&slice_volatile_in_que_ok);
						ret = 0;
					}
					break;
				}
				else if(SLICE_IDLE == cur_state)
				{
					free_slice(slice_idx);
					ret =0;
					break;
				}
				else if(SLICE_CHG == cur_state)
				{
					if(0 == change_slice_state(nid,slice_id,SLICE_CHG,SLICE_VOLATILE))
					{
						ret = 0;
						break;
					}
				}
				else
				{
					printk("err state in volatile proc %lld\r\n",cur_state);
					break;
				}

			}while(1);
			break;
		}
	
		default : 
		{
			printk("slice op err \r\n");
			break;
		}
	}

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
#if 1

				preempt_disable();
				
				if(op  == 1)
                {
					time_begin = rdtsc();
                    process_slice_state(page_to_pfn((struct page*)(msg)),SLICE_OUT_QUE,msg,-1);
					PONE_TIMEPOINT_SET(slice_out_que,(rdtsc()-time_begin));
					
				}
                else if(op == 2)
                {
					time_begin = rdtsc();
                    process_slice_state(page_to_pfn((struct page*)(msg)),SLICE_OUT_WATCH_QUE,msg,-1);
					PONE_TIMEPOINT_SET(slice_out_watch_que,(rdtsc()-time_begin));
                }
				else if(op == 3)
				{
                    process_slice_state(page_to_pfn((struct page*)(msg)),SLICE_OUT_DEAMON_QUE,msg,-1);
				}
				else
				{
					printk("que desc err\r\n");
				}
				preempt_enable();
#endif
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
				if(process_cnt++ >2000)
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










