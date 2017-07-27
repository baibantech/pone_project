/*************************************************************************
	> File Name: slice_state_deamon.c
	> Author: lijiyong
	> Mail: lijiyong0303@163.com 
	> Created Time: Mon 24 Apr 2017 03:30:17 AM EDT
 ************************************************************************/

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <pone/lf_rwq.h>
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include  "splitter_adp.h"
int deamon_scan_period = 20000;
unsigned long long wakeup_deamon_cnt = 0;

slice_state_control_block *deamon_volatile  = NULL;
slice_state_control_block *deamon_scan_volatile = NULL;

int deamon_slice_state_control_init (slice_state_control_block ** blk)
{
	int ret = -1;
    int i = 0;
	
	if(*blk == NULL)
	{
        *blk  = kmalloc(PAGE_SIZE,GFP_KERNEL);
        if(*blk)
        {
            ret = collect_sys_slice_info(*blk);
            if( 0 == ret)
            {
				for (i =0 ; i < (*blk)->node_num ; i++)
				{
					long long slice_num = (*blk)->slice_node[i].slice_num;
					long long mem_size = ((slice_num/SLICE_NUM_PER_UNIT)+1)*sizeof(unsigned long long);
					if(slice_num > 0)
					{
						printk("mem_size is 0x%llx,order is %d\r\n",mem_size,get_order(mem_size));
						(*blk)->slice_node[i].slice_state_map = vmalloc(mem_size);    
						
						if(!(*blk)->slice_node[i].slice_state_map)
						{
							//free_slice_state_control(*blk);
							*blk = NULL;
							printk("vmalloc mem failed \r\n");
							return -1;
						}
						printk("nid is %d ,map addr is %p \r\n",i,(*blk)->slice_node[i].slice_state_map);
						(*blk)->slice_node[i].mem_size = mem_size;
						memset((*blk)->slice_node[i].slice_state_map,0,mem_size);           
					}
				}
				return 0;
			}
		}
	}
	return -1;
}

unsigned long long get_slice_volatile_cnt(unsigned int nid ,unsigned long slice_id)
{
	unsigned long long state_unit = 0;
	int offset;
	state_unit = *(deamon_volatile->slice_node[nid].slice_state_map + (slice_id/SLICE_NUM_PER_UNIT));
    offset = slice_id%SLICE_NUM_PER_UNIT;
    return  (state_unit >> (offset * SLICE_STATE_BITS))&SLICE_STATE_MASK;

}

unsigned long long change_slice_volatile_cnt(unsigned int nid , unsigned long slice_id,unsigned long long old_cnt,unsigned long long new_cnt)
{
    unsigned long long low_unit = 0;
	unsigned long long high_unit = 0;
	unsigned long long new_cnt_unit = 0;	
    unsigned long long cur_cnt_unit = 0;
	unsigned long long tmp =0;
	volatile unsigned long long *state_unit_addr = deamon_volatile->slice_node[nid].slice_state_map + (slice_id/SLICE_NUM_PER_UNIT);
    int offset = slice_id%SLICE_NUM_PER_UNIT;
    int offsetbit = offset*SLICE_STATE_BITS;
	
	do
    {
		barrier();
		if(new_cnt > 7)
		{
			printk("volatile new cnt is err %d\r\n",new_cnt);
		}
        cur_cnt_unit = *state_unit_addr	;
		if(old_cnt != ((cur_cnt_unit >> (offset * SLICE_STATE_BITS))&SLICE_STATE_MASK))
		{
			printk("volatile old cnt is %lld,cur cnt is %lld\r\n",old_cnt, (cur_cnt_unit >> (offset * SLICE_STATE_BITS))&SLICE_STATE_MASK);
			return -1;
		}
        
		tmp = cur_cnt_unit; 
		if(0 != offsetbit)
		{
			low_unit = (tmp << (SLICE_STATE_UNIT_BITS - offsetbit)) >> (SLICE_STATE_UNIT_BITS - offsetbit);
		}
		high_unit = (tmp  >> (offsetbit + SLICE_STATE_BITS))<<(offsetbit+SLICE_STATE_BITS);
		new_cnt_unit =   high_unit + (new_cnt << offsetbit) + low_unit;
    
        if(cur_cnt_unit == atomic64_cmpxchg((atomic64_t*)state_unit_addr,cur_cnt_unit,new_cnt_unit))
        {
			break;
        }

    }while(1);
	return 0;
}

unsigned long long get_slice_scan_cnt(unsigned int nid ,unsigned long long slice_id)
{
	unsigned long long state_unit = 0;
	int offset;
	state_unit = *(deamon_scan_volatile->slice_node[nid].slice_state_map + (slice_id/SLICE_NUM_PER_UNIT));
    offset = slice_id%SLICE_NUM_PER_UNIT;
    return  (state_unit >> (offset * SLICE_STATE_BITS))&SLICE_STATE_MASK;
}

unsigned long long change_slice_scan_cnt(unsigned int nid , unsigned long slice_id,unsigned long long old_cnt,unsigned long long new_cnt)
{
    unsigned long long low_unit = 0;
	unsigned long long high_unit = 0;
	unsigned long long new_cnt_unit = 0;	
    unsigned long long cur_cnt_unit = 0;
	unsigned long long tmp = 0;
	volatile unsigned long long *state_unit_addr = deamon_scan_volatile->slice_node[nid].slice_state_map + (slice_id/SLICE_NUM_PER_UNIT);
    int offset = slice_id%SLICE_NUM_PER_UNIT;
    int offsetbit = offset*SLICE_STATE_BITS;
	
	do
    {
		barrier();
		if(new_cnt > 7)
		{
			printk("scan new cnt err %d\r\n",new_cnt);
		}
        cur_cnt_unit = *state_unit_addr;
		if(old_cnt != ((cur_cnt_unit >> (offset * SLICE_STATE_BITS))&SLICE_STATE_MASK))
		{
			printk("scan old cnt is %lld,cur cnt is %lld\r\n",old_cnt, (cur_cnt_unit >> (offset * SLICE_STATE_BITS))&SLICE_STATE_MASK);
			return -1;
		}
		tmp = cur_cnt_unit; 
		
		if(0 != offsetbit)
		{
			low_unit = (tmp << (SLICE_STATE_UNIT_BITS - offsetbit)) >> (SLICE_STATE_UNIT_BITS - offsetbit);
		}
		
		high_unit = (tmp  >> (offsetbit + SLICE_STATE_BITS))<<(offsetbit+SLICE_STATE_BITS);
		new_cnt_unit =   high_unit + (new_cnt << offsetbit) + low_unit;
    
        if(cur_cnt_unit == atomic64_cmpxchg((atomic64_t*)state_unit_addr,cur_cnt_unit,new_cnt_unit))
        {
			break;
        }

    }while(1);
	return 0;
}

void clear_deamon_cnt(unsigned int nid,unsigned long slice_id)
{
	unsigned long long deamon_vcnt = 0;
	unsigned long long deamon_scnt = 0;
#if 1
	do
	{
		barrier();
		deamon_scnt = get_slice_scan_cnt(nid,slice_id);
		if(deamon_scnt != 0)
		{
			if(0 != change_slice_scan_cnt(nid,slice_id,deamon_scnt,0))
			{
				continue;
			}
		}
		deamon_vcnt = get_slice_volatile_cnt(nid,slice_id);
		if(deamon_vcnt != 0)
		{
			if(0 == change_slice_volatile_cnt(nid,slice_id,deamon_vcnt,0))
			{
				break;
			}
		}
		else
		{
			break;
		}
	}
	while(1);
#endif
}
void add_slice_volatile_cnt_test(unsigned int nid,unsigned long slice_id)
{


}

void add_slice_volatile_cnt(unsigned int nid,unsigned long slice_id)
{
	unsigned long long deamon_vcnt = 0;
#if 1
	do
	{
		barrier();
		deamon_vcnt = get_slice_volatile_cnt(nid,slice_id);
		//printk("deamon_vcnt is %lld\r\n",deamon_vcnt);
		if(deamon_vcnt < 7)
		{
			if(0 != change_slice_volatile_cnt(nid,slice_id,deamon_vcnt,deamon_vcnt+1))
			{
				continue;
			}
		}
		break;
	}
	while(1);
#endif
}

void mark_volatile_cnt_in_wcopy(unsigned long old_slice,unsigned long new_slice)
{
	unsigned int nid,nid1;
	unsigned long slice_id,slice_id1;
	unsigned long long deamon_vcnt = 0;
	unsigned long long deamon_vcnt1 = 0;

	nid = slice_idx_to_node(old_slice);
	slice_id = slice_nr_in_node(nid,old_slice);
	nid1 = slice_idx_to_node(new_slice);
	slice_id1 = slice_nr_in_node(nid1,new_slice);

	do
	{
		deamon_vcnt = get_slice_volatile_cnt(nid,slice_id);
		if(deamon_vcnt < 7)
		{
			if(0 != change_slice_volatile_cnt(nid,slice_id,deamon_vcnt,deamon_vcnt+1))
			{	
				continue;
			}
		}
new_slice_cnt:
		deamon_vcnt1 = get_slice_volatile_cnt(nid1,slice_id1);
		if(deamon_vcnt < 7)
		{
			if(0 == change_slice_volatile_cnt(nid1,slice_id1,deamon_vcnt1,deamon_vcnt+1))
			{
				break;
			}
			goto new_slice_cnt; 
		}
		else
		{
			if(0 == change_slice_volatile_cnt(nid1,slice_id1,deamon_vcnt1,deamon_vcnt))
			{
				break;
			}
			goto new_slice_cnt; 
		}
	}
	while(1);
}



void set_deamon_run(void)
{
	unsigned long long old = 0;
	unsigned long long new = 0;
	do {
		old = wakeup_deamon_cnt;
		new = atomic64_cmpxchg((atomic64_t*)&wakeup_deamon_cnt,old,old+1);
		
		if(new == old)
		{
			break;
		}
	
	}while(1);

}

int need_wakeup_deamon(void)
{
	unsigned long long old = 0;
	unsigned long long new = 0;
	do {
		old = wakeup_deamon_cnt;
		new = atomic64_cmpxchg((atomic64_t*)&wakeup_deamon_cnt,old,0);
		
		if(new == old)
		{
			break;
		}
	
	}while(1);

	if(0 == new)
	{
		return 0;
	}
	return 1;
}

struct task_struct *spt_deamon_thread = NULL;
lfrwq_t *slice_deamon_que = NULL;
unsigned long long slice_deamon_find_volatile = 0;
unsigned long long slice_deamon_in_que_fail = 0;
#if 0
static int splitter_daemon_thread(void *data)
{
	int i = 0;
	int j = 0;
	int k = 0;
	unsigned int nid = 0;
	long long mem_size = 0;
	long long slice_num = 0;
	long long *slice_map;
	long long state_map = 0;
	long long slice_state = 0;
	unsigned long slice_idx = 0;
	unsigned long slice_begin = 0;
	int volatile_count = 0;
	int need_repeat = 0;	
	__set_current_state(TASK_RUNNING);

	do
	{

		volatile_count = 0;
		need_repeat =0;
		for(i = 0 ; i<global_block->node_num;i++)
		{
			slice_map = global_block->slice_node[i].slice_state_map;
			slice_num = global_block->slice_node[i].slice_num;
			mem_size = global_block->slice_node[i].mem_size;
			slice_begin = global_block->slice_node[i].slice_start;
			for(j = 0;j < (mem_size/sizeof(long long)); j++)
			{
				state_map = *(slice_map+j);
				if(0 == state_map)
				{
					continue;
				}
				for(k = 0;k < SLICE_NUM_PER_UNIT;k++)
				{
					slice_state = (state_map>>(k*SLICE_STATE_BITS))&SLICE_STATE_MASK;
					if(SLICE_VOLATILE == slice_state)
					{
						if(0 != change_slice_state(i,j*SLICE_NUM_PER_UNIT+k,SLICE_VOLATILE,SLICE_ENQUE))
						{
							need_repeat++;
							continue;
						}
						
						slice_deamon_find_volatile++;
						slice_idx = slice_begin + j*SLICE_NUM_PER_UNIT+k;
retry:
						if(-1 == lfrwq_inq(slice_deamon_que,pfn_to_page(slice_idx)))
						{
							schedule();
							goto retry;
						}
						volatile_count++; 
						if(0 == (volatile_count%1000))
						{
							lfrwq_set_r_max_idx(slice_deamon_que,lfrwq_get_w_idx(slice_deamon_que));
							splitter_thread_wakeup();		
						}

					}
				}			
			}
		}
		if(need_repeat)
		{
			continue;
		}
		if(0 == volatile_count)		
		{
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		}

	}while(!kthread_should_stop());

	return 0;

}
#endif

unsigned long long slice_deamon_volatile_cnt[16] = {0};

void show_slice_volatile_cnt(void)
{
	int i = 0;
	for(i =0 ;i <16 ;i++)
	{
		printk("num idex %d,num %lld\r\n",i,slice_deamon_volatile_cnt[i]);
	}

}
extern void wakeup_splitter_thread_by_que(long que_id);
static int splitter_daemon_thread(void *data)
{
	int i = 0;
	int j = 0;
	int k = 0;
	unsigned int nid = 0;
	long long mem_size = 0;
	long long slice_num = 0;
	long long *slice_map;
	long long state_map = 0;
	long long slice_state = 0;
	unsigned long slice_idx = 0;
	unsigned long slice_begin = 0;
	int volatile_count = 0;
	int need_repeat = 0;
	unsigned long long start_jiffies = 0;
	unsigned long long end_jiffies = 0;
	unsigned long long cost_time = 0;
	int ret = 0;
	unsigned long long slice_vcnt = 0;
	unsigned long long slice_scan_cnt = 0;
	long que_id;
	struct page *page = NULL;
	__set_current_state(TASK_RUNNING);

	do
	{

		volatile_count = 0;
		need_repeat =0;
		start_jiffies = get_jiffies_64();
		for(i = 0 ; i<global_block->node_num;i++)
		{
			slice_num = global_block->slice_node[i].slice_num;
			slice_begin = global_block->slice_node[i].slice_start;
			
			for(j = 0;j<slice_num;j++)
			{
				slice_state = get_slice_state(i,j);
				
				if(SLICE_VOLATILE == slice_state)
				{
#if 1
get_cnt:
					if(0 != (slice_vcnt = get_slice_volatile_cnt(i,j)))
					{
						slice_scan_cnt  = get_slice_scan_cnt(i,j);
						if(slice_scan_cnt == slice_vcnt)
						{	
							if(0 != change_slice_scan_cnt(i,j,slice_scan_cnt,0))
							{
								goto get_cnt;
							}
							atomic64_add(1,(atomic64_t*)&slice_deamon_volatile_cnt[slice_vcnt]);
						}
						else
						{
							if(slice_scan_cnt > slice_vcnt)
							{
								printk("deamon cnt bug bug bug bug %lld,%lld \r\n",slice_scan_cnt,slice_vcnt);
								if(0 == slice_vcnt)
								{
									continue;
								}
							}
							if(0 != change_slice_scan_cnt(i,j,slice_scan_cnt,slice_scan_cnt+1))
							{
								goto get_cnt;
							}
							continue;

						}
					}
#endif
					slice_idx = slice_begin + j; 
					page = pfn_to_page(slice_idx);
					que_id = pone_get_slice_que_id(page);
					if(-1 == que_id)
					{
						continue;
					}
					if(0 != change_slice_state(i,j,SLICE_VOLATILE,SLICE_ENQUE))
					{
						need_repeat++;
						continue;
					}
					
					slice_deamon_find_volatile++;
retry:

					
#if 0
					if(-1 == lfrwq_inq(slice_deamon_que,pfn_to_page(slice_idx)))
#endif
					if(-1 == lfrwq_in_cluster_que(page,que_id))	
					{
						slice_deamon_in_que_fail++;
						wakeup_splitter_thread_by_que(que_id);
						end_jiffies = get_jiffies_64();
						cost_time = jiffies_to_msecs(end_jiffies - start_jiffies);
						if(cost_time >deamon_scan_period)
						{
							msleep(deamon_scan_period);
							start_jiffies = get_jiffies_64();
						}
						msleep(1);
						goto retry;
					}
					#if 0
					volatile_count++; 
					if(0 == (volatile_count%1000))
					{
						lfrwq_set_r_max_idx(slice_deamon_que,lfrwq_get_w_idx(slice_deamon_que));
						splitter_thread_wakeup();		
					}
#endif
				}

			}

		}
		
		end_jiffies = get_jiffies_64();

		cost_time = jiffies_to_msecs(end_jiffies - start_jiffies);
		if(cost_time >deamon_scan_period)
		{
			msleep(deamon_scan_period);
		
		}
		else
		{	
			msleep(deamon_scan_period - cost_time);
		}

#if 0
		if(0 == volatile_count)		
		{
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		}
#endif

	}while(!kthread_should_stop());
	return 0;
}

void splitter_deamon_wakeup(void)
{
	if(spt_deamon_thread)
	{
		if(spt_deamon_thread->state != TASK_RUNNING)
		{
			//wake_up_process(spt_deamon_thread);
		}
	}
}

int slice_deamon_init(void)
{
	lfrwq_reader *reader;
	int cpu = 0;
	int ret = -1; 
	
	
	ret = deamon_slice_state_control_init(&deamon_volatile);
	ret += deamon_slice_state_control_init(&deamon_scan_volatile);
	if(ret != 0)
	{
		return -1;
	}

	slice_deamon_que = lfrwq_init(8192*32,2048,50);
    if(!slice_deamon_que)
    {
        return -1;
    }
	
	for_each_online_cpu(cpu)
	{
		reader = &per_cpu(int_slice_deamon_que_reader,cpu);
		memset(reader,0,sizeof(lfrwq_reader));
		reader->local_idx = -1;
	}

	spt_deamon_thread = kthread_create(splitter_daemon_thread,NULL,"spdeamon");
	if(IS_ERR(spt_deamon_thread))
	{
		spt_deamon_thread = NULL;
		printk("err create spt_daemon thread\r\n");
		return -1;
	}
	else
	{
		wake_up_process(spt_deamon_thread);
	}
	return 0;
}

