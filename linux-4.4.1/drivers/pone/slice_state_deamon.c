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
#include <pone/lf_rwq.h>
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include  "splitter_adp.h"
int deamon_scan_period = 20000;
unsigned long long wakeup_deamon_cnt = 0;

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
					
					if(0 != change_slice_state(i,j,SLICE_VOLATILE,SLICE_ENQUE))
					{
						need_repeat++;
						continue;
					}
					
					slice_deamon_find_volatile++;
					slice_idx = slice_begin + j; 
retry:
					if(-1 == lfrwq_inq(slice_deamon_que,pfn_to_page(slice_idx)))
					{
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
					volatile_count++; 
					if(0 == (volatile_count%1000))
					{
						lfrwq_set_r_max_idx(slice_deamon_que,lfrwq_get_w_idx(slice_deamon_que));
						splitter_thread_wakeup();		
					}
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

