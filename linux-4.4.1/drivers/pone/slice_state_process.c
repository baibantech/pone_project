#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm-generic/pgtable.h>
#include <linux/bootmem.h>
#include <linux/ksm.h>
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include <pone/lf_rwq.h>

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
unsigned long long slice_new_insert_num = 0 ;
unsigned long long slice_change_ref_err = 0;
unsigned long long slice_merge_num = 0;
unsigned long long slice_free_num = 0;
unsigned long long slice_mem_watch_change= 0;
unsigned long long slice_mem_fix_change = 0;

unsigned long long slice_file_protect_num = 0;
unsigned long long slice_file_chgref_num = 0;
unsigned long long slice_file_cow = 0;
unsigned long long slice_file_watch_chg = 0;
unsigned long long slice_file_fix_chg = 0;
extern unsigned long pone_file_watch ;
void free_slice_state_control(slice_state_control_block *blk)
{
    if(blk)
    {
        int i;
        for( i = 0; i < blk->node_num ; i++)
        {
            if(blk->slice_node[i].slice_state_map)
            {
                kfree((void*)blk->slice_node[i].slice_state_map);
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
        long long mem_size = ((((slice_num *SLICE_STATE_BITS)/8) /sizeof(unsigned long long)) +1)*sizeof(unsigned long long);
        if(slice_num > 0)
        {
            blk->slice_node[i].slice_state_map = kmalloc(mem_size,GFP_KERNEL);    
            if(!blk->slice_node[i].slice_state_map)
            {
                free_slice_state_control(blk);
                return -1;
            }
			printk("nid is %d ,map addr is %p \r\n",i,blk->slice_node[i].slice_state_map);
            memset(blk->slice_node[i].slice_state_map,0,mem_size);           
        }
    }
    return 0;
}

int slice_state_control_init(void)
{
    int ret = -1;
    if(NULL == global_block)
    {
        global_block = kmalloc(PAGE_SIZE,GFP_KERNEL);
        if(global_block)
        {
            ret = collect_sys_slice_info(global_block);
            if( 0 == ret)
            {
                return  slice_state_map_init(global_block);            
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
    slice_que = lfrwq_init(8192,512,6);
    if(!slice_que)
    {
        return -1;
    }
    slice_watch_que = lfrwq_init(8192,512,6);
    if(!slice_watch_que)
    {
        kfree(slice_que);
        return -1;
    }
    return 0;
}

int process_slice_state(unsigned long slice_idx ,int op,void *data)
{
    unsigned long long cur_state;
    unsigned int  nid ;
    unsigned long long slice_id ;
	struct pone_desc *pone = NULL;
	int ret = -1;
	struct page *slice = pfn_to_page(slice_idx);
	if(!global_block)
	{
		return 0;
	}

	if(PageKsm(slice))
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
			//printk("slice op is SLICE_ALLOC\r\n");
			printk("slice_idx is %ld, nid is %d,slice_id is %lld\r\n",slice_idx,nid,slice_id);
			atomic64_add(1,(atomic64_t*)&slice_alloc_num);
			do
			{
				cur_state = get_slice_state(nid,slice_id);
				if(SLICE_IDLE != cur_state){
					printk("slice state is %lld ,err in slice alloc\r\n",cur_state);
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
					//printk("slice op is SLICE_FREE\r\n");
					//printk("cur state is %d\r\n",cur_state);
					//printk("slice_idx is %ld, nid is %d,slice_id is %lld\r\n",slice_idx,nid,slice_id);
					atomic64_add(1,(atomic64_t*)&slice_free_num);
					if(0 == change_slice_state(nid,slice_id,cur_state,SLICE_IDLE)){
						if(SLICE_FIX == cur_state){
							delete_fix_slice(slice_idx);
							ret = 0;
						}
						else if(SLICE_VOLATILE == cur_state){
							ret = 0;
						}
						else {
							/*slice state is SLICE_ENQUE,SLICE_WATCH,SLICE_WATCH_CHG*/
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

        case SLICE_CHANGE:/*cow excption,reuse*/
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
					atomic64_add(1,(atomic64_t*)&slice_mem_fix_change);
					if( 0 == change_slice_state(nid,slice_id,SLICE_FIX,SLICE_VOLATILE)){
						delete_fix_slice(slice_idx);
						break;
					}    
				}
				else{
					break;
					//printk("error state in slice change %lld\r\n",cur_state);
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
				//printk("SLICE OUT QUE ,cur state is %lld\r\n",cur_state);
				//printk("slice_idx is %ld, nid is %d,slice_id is %lld\r\n",slice_idx,nid,slice_id);
				
				if(!slice_que_debug){
					ret = 0;
					break;
				}	
				struct page *page = pfn_to_page(slice_idx);
				printk("slice out que page count is %d\r\n",*(int*)&page->_count);
				if(SLICE_IDLE == cur_state){
					/*page free by sys*/
					free_slice(slice_idx);
					ret = 0;
				}
				else if(SLICE_ENQUE == cur_state){
					if(0 == change_slice_state(nid,slice_id,SLICE_ENQUE,SLICE_WATCH)){
						
						if(SLICE_OK == make_slice_wprotect(slice_idx)){
							
							if(0 != lfrwq_inq(slice_watch_que,data))
							{
								atomic64_add(1,(atomic64_t*)&slice_in_watch_que_err);
								printk("in watch que err\r\n");
							}
							else
							{
								atomic64_add(1,(atomic64_t*)&slice_in_watch_que_ok);
								//printk("in watch que ok \r\n");
								ret = 0;
								break;
							}
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

				struct page *page = pfn_to_page(slice_idx);
				printk("slice out watch que page count is %d\r\n",*(int*)&page->_count);
				if(SLICE_IDLE == cur_state){
					free_slice(slice_idx);
					ret = 0;
					break;
				}
				else if (SLICE_WATCH == cur_state)
				{
					//printk("insert sd tree\r\n");
					pone = insert_sd_tree(slice_idx);
					if(NULL == pone){
						break;
					}
						
					if(pone->slice_idx == slice_idx){
						if(0 == change_slice_state(nid,slice_id,SLICE_WATCH,SLICE_FIX)){
							atomic64_add(1,(atomic64_t*)&slice_new_insert_num);
							//printk("slice_idx is new insert ok\r\n");
							ret = 0;
							break;
						}
						pone_delete_desc(pone);
					}
					else{
						
						get_page(pfn_to_page(slice_idx));
						//printk("slice_idx is same to new_slice %lld,%lld\r\n",slice_idx,pone->slice_idx);
						if(SLICE_OK == change_reverse_ref(slice_idx,pone->slice_idx))
						{
							atomic64_add(1,(atomic64_t*)&slice_merge_num);
							while(0 != change_slice_state(nid,slice_id,get_slice_state(nid,slice_id),SLICE_IDLE));
							put_page(pfn_to_page(slice_idx));
							ret = 0;
							break;
						}
						else
						{
							//printk("change ref ret err\r\n");
							atomic64_add(1,(atomic64_t*)&slice_change_ref_err);
							break;
						}
					}
				}
				else if(SLICE_WATCH_CHG == cur_state){
					
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
	char *src = "wireway";
	if(!global_block)
		return 0;

#if 0
	if(1 == atomic64_add_return(1,(atomic64_t*)&alloc_check))	
#endif	
	if(0 == strcmp(current->comm,src))	
		return 1;
	else
		return 0;
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
            }
        }
        
        if(reader->r_cnt)
        {
            lfrwq_add_rcnt(qh,reader->r_cnt,reader->local_blk);
            reader->r_cnt = 0 ;
        }

    }while(1);
    
    return 0;
}















