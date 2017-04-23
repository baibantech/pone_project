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
lfrwq_t *slice_que = NULL;
lfrwq_t *slice_watch_que = NULL;
slice_state_control_block *global_block = NULL;
unsigned long long alloc_check = 0;

unsigned long slice_watch_que_debug = 1;
unsigned long slice_que_debug = 1;

unsigned long long slice_alloc_num = 0;
unsigned long long slice_in_que_ok =0;
unsigned long long slice_in_que_err = 0;

unsigned long long slice_protect_err = 0;
unsigned long long slice_in_watch_que_ok = 0;
unsigned long long slice_in_watch_que_err = 0;
unsigned long long slice_mem_que_free =0;


unsigned long long slice_new_insert_num = 0 ;
unsigned long long slice_insert_sd_tree_err = 0;
unsigned long long slice_change_ref_err = 0;
unsigned long long slice_merge_num = 0;
unsigned long long slice_mem_watch_free = 0;

unsigned long long slice_free_num =0 ;
unsigned long long slice_fix_free_num = 0;
unsigned long long slice_volatile_free_num = 0;
unsigned long long slice_other_free_num = 0;
unsigned long long slice_sys_free_num = 0;

unsigned long long slice_mem_watch_change= 0;
unsigned long long slice_mem_fix_change = 0;


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
int slice_debug_area_init(void);
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
                return  slice_state_map_init(blk);            
            }
        }
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

    unsigned long long *state_unit_addr = get_slice_state_unit_addr(nid,slice_id);
//	printk("state unit addr is %p\r\n",state_unit_addr);
//	printk("state unit mem is 0x%llx\r\n",*state_unit_addr);
    do
    {
        cur_state_unit = *state_unit_addr;

        if(old_state != get_slice_state_by_unit(cur_state_unit,slice_id))
        {
            printk("error slice state in slice alloc %lld\r\n",cur_state_unit);
            return -1;
        }   

        new_state_unit = construct_new_state(nid,slice_id,new_state);
    
        if(cur_state_unit == atomic64_cmpxchg((atomic64_t*)state_unit_addr,cur_state_unit,new_state_unit))
        {
            break;
        }

    }while(1);
	
//	printk("state unit mem change is 0x%llx\r\n",*state_unit_addr);
//	printk("slice state is %lld\r\n",get_slice_state(nid,slice_id));
	return 0;    
}

int slice_que_resource_init(void)
{
    slice_que = lfrwq_init(8192*16,2048,50);
    if(!slice_que)
    {
        return -1;
    }
    slice_watch_que = lfrwq_init(8192*8,2048,50);
    if(!slice_watch_que)
    {
        vfree(slice_que);
        return -1;
    }
	slice_debug_area_init();
    return 0;
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
	barrier();
	cur_state = get_slice_state(nid,slice_id);	
	if(SLICE_FIX == cur_state)
	{
		if(atomic_read(&(org_page->_mapcount)) > 0)
		{
			if(0 == change_slice_state(nid ,slice_id,SLICE_FIX,SLICE_FIX))
			{
				atomic64_add(1,(atomic64_t*)&slice_pre_fix_check_cnt);
				delete_sd_tree(slice_idx,SLICE_FIX);			
			}
			else
			{	
				printk("error in pre slice fix check");
			}
		}
		else
		{
			printk("mapcount err in pre fix check\r\n");
		}
	}
}
EXPORT_SYMBOL(pre_fix_slice_check);

int process_slice_state(unsigned long slice_idx ,int op,void *data)
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
			/* data is the struct page* */
			if(!slice_que_debug)
			{
				return 0;
			}
			atomic64_add(1,(atomic64_t*)&slice_alloc_num);
			
			if(per_cpu(in_que_cnt,smp_processor_id())++  > 100)
			{
				per_cpu(in_que_cnt,smp_processor_id()) = 0;
				lfrwq_set_r_max_idx(slice_que,lfrwq_get_w_idx(slice_que));
			}
			slice_debug_area_insert(org_slice);

			do
			{
				cur_state = get_slice_state(nid,slice_id);
				if(SLICE_IDLE != cur_state){
					printk("slice state is %lld ,err in slice alloc\r\n",cur_state);
					break;
				}
				if(0 != atomic_read(&org_slice->_mapcount))
				{
					break;
				}
				if(0 == change_slice_state(nid,slice_id,SLICE_IDLE,SLICE_ENQUE)) {
					if(-1 == lfrwq_inq(slice_que,data)){
						atomic64_add(1,(atomic64_t*)&slice_in_que_err);
						while(0 != change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_VOLATILE));
					}
					else{
						atomic64_add(1,(atomic64_t*)&slice_in_que_ok);
						ret = 0;
					}
					break;
				}

			}while(1);
			
			break;
		}
        
        case SLICE_FREE: /*page refcount  is 0*/
		{
			do
			{
				/* return value 0 ,make page free by linux system*/
				cur_state = get_slice_state(nid,slice_id);
				if(cur_state != SLICE_IDLE)
				{
					atomic64_add(1,(atomic64_t*)&slice_free_num);
					if(0 == change_slice_state(nid,slice_id,cur_state,SLICE_IDLE)){
						if(SLICE_FIX == cur_state){
							atomic64_add(1,(atomic64_t*)&slice_fix_free_num);
							ret = 0;
						}
						else if(SLICE_VOLATILE == cur_state){
							atomic64_add(1,(atomic64_t*)&slice_volatile_free_num);
							ret = 0;
						}
						else {
							/*slice state is SLICE_ENQUE,SLICE_WATCH,SLICE_WATCH_CHG*/
							atomic64_add(1,(atomic64_t*)&slice_other_free_num);
							ret = -1 ;/*sys do not free this page*/
						}
					}
					else {
						continue;
					}
					
				}
				else
				{
					/* 0 make linux sys free this page*/
					ret = 0;
				
				}
				break;

			}while(1);            

			break;
		}

		case SLICE_CHANGE:/*cow excption:reuse*/
		{
			do
			{
				/* slice state is fix or watch*/
				cur_state = get_slice_state(nid,slice_id);
				if(SLICE_WATCH == cur_state){
					atomic64_add(1,(atomic64_t*)&slice_mem_watch_change);
					if(0 == change_slice_state(nid,slice_id,SLICE_WATCH,SLICE_WATCH_CHG)){
						break;
					}
				}    
				else if(SLICE_FIX == cur_state){
					
					printk("change status is err\r\n");
					if(atomic_read(&org_slice->_count) > 2)
					{
						printk("org_slice count is uper 2\r\n");
					}

					atomic64_add(1,(atomic64_t*)&slice_mem_fix_change);
					if(0 == change_slice_state(nid ,slice_id,SLICE_FIX,SLICE_FIX))
					{
						printk("org_slice %p count %d,mapcount %d\r\n",org_slice,atomic_read(&org_slice->_count),atomic_read(&org_slice->_mapcount));
						delete_sd_tree(slice_idx,SLICE_VOLATILE);			
						if( 0 == change_slice_state(nid,slice_id,SLICE_FIX,SLICE_VOLATILE)){
							
							break;
						}
						printk("err in fix chg status\r\n");
						break;
					}
					else
					{
						continue;
					}
				}
				else{
					break;
				}
			}while(1);
			ret = 0;
			break; 
		}
        case SLICE_OUT_QUE:
		{
			do
			{
				cur_state = get_slice_state(nid,slice_id);
				
				if(!slice_que_debug){
					ret = 0;
					break;
				}	
				if(SLICE_IDLE == cur_state){
					/*page free by sys*/
					free_slice(slice_idx);
					atomic64_add(1,(atomic64_t*)&slice_mem_que_free);
					ret = 0;
					break;
				}
				else if(SLICE_ENQUE == cur_state){
					
					
					if(per_cpu(in_watch_que_cnt,smp_processor_id())++  > 100)
					{
						per_cpu(in_watch_que_cnt,smp_processor_id()) = 0;
						lfrwq_set_r_max_idx(slice_watch_que,lfrwq_get_w_idx(slice_watch_que));
					}
					if(0 == change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_WATCH)){
						
						atomic_add(1,&org_slice->_mapcount);
						if(SLICE_OK == make_slice_wprotect(slice_idx)){
							
							if(0 != lfrwq_inq(slice_watch_que,data))
							{
								atomic64_add(1,(atomic64_t*)&slice_in_watch_que_err);
							}
							else
							{
								atomic64_add(1,(atomic64_t*)&slice_in_watch_que_ok);
								ret = 0;
								break;
							}
						}
						else {
							
							atomic64_add(1,(atomic64_t*)&slice_protect_err);
						}
						
						/*make slice state volatile*/
						atomic_sub(1,&org_slice->_mapcount);
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
								break;
							}
					
						}while(1);	
						
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
			do
			{
				cur_state = get_slice_state(nid,slice_id);
							 
				//printk("SLICE OUT WATCH QUE,cur_state is %d\r\n",cur_state);
				//printk("slice_idx is %ld, nid is %d,slice_id is %lld\r\n",slice_idx,nid,slice_id);
				if(!slice_watch_que_debug) return 0;

				//printk("slice out watch que page count is %d\r\n",*(int*)&page->_count);
				if(SLICE_IDLE == cur_state){
					atomic_sub(1,&org_slice->_mapcount);
					free_slice(slice_idx);
					ret = 0;
					atomic64_add(1,(atomic64_t*)&slice_mem_watch_free);
					break;
				}
				else if (SLICE_WATCH == cur_state)
				{
					if(atomic_read(&org_slice->_mapcount)!= 1)
					{
						while (0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_VOLATILE));
						printk("mapcount err is %d\r\n",atomic_read(&org_slice->_mapcount));
						atomic_sub(1,&org_slice->_mapcount);
						break;
					}
					get_page(org_slice);
					result = insert_sd_tree(slice_idx);
					if(NULL == result){
						while (0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_VOLATILE));
						atomic_sub(1,&org_slice->_mapcount);
						put_page(org_slice);
						atomic64_add(1,(atomic64_t*)slice_insert_sd_tree_err);
						break;
					}
						
					if(result == org_slice){
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
						if(SLICE_OK == change_reverse_ref(slice_idx,page_to_pfn(result)))
						{
							atomic64_add(1,(atomic64_t*)&slice_merge_num);
							while(0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_IDLE));
							atomic_sub(1,&org_slice->_mapcount);
							put_page(org_slice);
							ret = 0;
						}
						else
						{
							//printk("change ref ret err\r\n");
							atomic64_add(1,(atomic64_t*)&slice_change_ref_err);
							while (0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_VOLATILE));
							delete_sd_tree(slice_idx,SLICE_FIX);
							atomic_sub(1,&org_slice->_mapcount);
							put_page(org_slice);
						}
						break;
					}
				}
				else if(SLICE_WATCH_CHG == cur_state){
					
					printk("watch chg state is err\r\n");
					if(0 == change_slice_state(nid,slice_id,SLICE_WATCH_CHG,SLICE_VOLATILE)){
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
	char *src = "test_case";
	if(!is_pone_init())
		return 0;
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
}

int process_slice_file_check(unsigned long i_ino)
{
	if(i_ino == pone_file_watch)
	return 1;

	return 0;
}

int process_state_que(lfrwq_t *qh,lfrwq_reader *reader)
{
	void *msg = NULL;
	int process_cnt = 0;
	unsigned long long r_idx = 0;
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
			if(reader->local_idx >= lfrwq_get_r_max_idx(qh))
			{
				break;
			}
			
			r_idx = lfrwq_deq_by_idx(qh,reader->local_idx,&msg);
			if(!msg)
			{

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
			break;
		} 

        while(reader->local_pmt)
        {
            msg = NULL;
			r_idx = lfrwq_alloc_r_idx(qh);
            if(r_idx >= lfrwq_get_r_max_idx(qh))
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
				if(qh == slice_que)
                {
                    process_slice_state(page_to_pfn((struct page*)(msg)),SLICE_OUT_QUE,msg);
                }
                else
                {
                    process_slice_state(page_to_pfn((struct page*)(msg)),SLICE_OUT_WATCH_QUE,msg);
                }
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
			break;
		}

    }while(1);
    
    return 0;
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










