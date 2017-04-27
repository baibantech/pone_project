/*************************************************************************
	> File Name: splitter_adp.c
	> Author: ma6174
	> Mail: ma6174@163.com 
	> Created Time: Fri 24 Mar 2017 12:18:58 AM PDT
 ************************************************************************/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/rmap.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include "vector.h"
#include "chunk.h"


struct task_struct *spt_thread_id[128] = {NULL};

unsigned long long data_map_key_cnt = 0;
unsigned long long data_unmap_key_cnt = 0;
struct page *get_page_ptr(char *pdata)
{
	return (struct page*)(pdata);
}

char *tree_get_key_from_data(char *pdata)
{
	struct page *page = NULL;
	atomic64_add(1,(atomic64_t*)&data_map_key_cnt);
	if(NULL == pdata)
	{
		return NULL;
	}
	page = get_page_ptr(pdata);
	if(NULL  == page)
	{
		return NULL;
	}

	return kmap_atomic(page);
}
void tree_free_key(char *key)
{
	atomic64_add(1,(atomic64_t*)&data_unmap_key_cnt);
	kunmap_atomic(key);
	return;
}

void tree_free_data(char *pdata)
{
	struct page *page = NULL;
	if(NULL != pdata)
	{
		page = get_page_ptr(pdata);
		if(NULL  != page)
		{	
			put_page(page);
		}
	}
	return;
}

char *tree_construct_data_from_key(char *pkey)
{
    char *pdata;

    pdata = (char *)kmalloc(DATA_SIZE,GFP_ATOMIC);
    if(pdata == NULL)
        return NULL;
    memcpy(pdata, pkey, DATA_SIZE);
    return (char *)pdata;
}

static int splitter_process_thread(void *data)
{
	int cpu = smp_processor_id();
	int check  = per_cpu(process_enter_check,cpu);
	lfrwq_reader *watch_reader =  &per_cpu(int_slice_watch_que_reader,cpu);
	lfrwq_reader *reader =  &per_cpu(int_slice_que_reader,cpu);
	lfrwq_reader *deamon_reader = &per_cpu(int_slice_deamon_que_reader,cpu);
	int ret = 0;
	int w_ret = 0;
	int d_ret = 0;
	__set_current_state(TASK_RUNNING);
	do
	{
		if(cpu%2)
		{
			w_ret = process_state_que(slice_watch_que,watch_reader);
			ret = process_state_que(slice_que,reader);
		}
		else
		{
			ret = process_state_que(slice_que,reader);
			w_ret = process_state_que(slice_watch_que,watch_reader);
		}

		if(per_cpu(volatile_cnt,cpu))
		{
			splitter_deamon_wakeup();
			set_deamon_run();
			per_cpu(volatile_cnt,cpu) = 0;	
		}

		d_ret = process_state_que(slice_deamon_que,deamon_reader);
		
		if((w_ret != -2) && (ret!= -2)&&(d_ret !=-2))
		{
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		}

	}while(!kthread_should_stop());

	return 0;

}

void splitter_thread_wakeup(void)
{
	int i = 0;
	for(i = 0;i <num_online_cpus();i++)
	{
		if(spt_thread_id[i] != NULL)
		{
			if(spt_thread_id[i]->state != TASK_RUNNING)
			{
				wake_up_process(spt_thread_id[i]);
			}
		}
	}
}

int pone_case_init(void)
{
	int thread_num = 0;
	int cpu = 0;
	set_data_size(PAGE_SIZE);
	
	thread_num = num_online_cpus()+1;
	printk("the thread_num is %d\r\n",thread_num);
	if(pgclst)
	{
		return 0;
	}
	pgclst = spt_cluster_init(0,DATA_BIT_MAX, thread_num, 
                              tree_get_key_from_data,
                              tree_free_key,
                              tree_free_data,
                              tree_construct_data_from_key);
    if(pgclst == NULL)
    {
        spt_debug("cluster_init err\r\n");
        return 1;
    }

    g_thrd_h = spt_thread_init(thread_num);
    if(g_thrd_h == NULL)
    {
        spt_debug("spt_thread_init err\r\n");
        return 1;
	}
	for_each_online_cpu(cpu)
	{
		if(cpu%2)
		{
			continue;
		}
		spt_thread_id[cpu] = kthread_create(splitter_process_thread,cpu,"spthrd_%d",cpu);
		if(IS_ERR(spt_thread_id[cpu]))
		{
			spt_thread_id[cpu] = NULL;
			printk("err create spt_thread in cpu %d\r\n",cpu);
			return -1;
		}
		else
		{
			kthread_bind(spt_thread_id[cpu],cpu);
			wake_up_process(spt_thread_id[cpu]);
		}
	
	}
	return 0;
}
unsigned long long insert_sd_tree_ok =0;
char * insert_sd_tree(unsigned long slice_idx)
{
	struct page *page = pfn_to_page(slice_idx);
	void *r_data = NULL;
	if(page)
	{
		spt_thread_start(g_thrd_id);
		r_data = insert_data(pgclst,(char*)page);
		spt_thread_exit(g_thrd_id);
		if(r_data !=NULL)
		{
			atomic64_add(1,(atomic64_t*)&insert_sd_tree_ok);
		}
	}
	return r_data;
}
unsigned long long delete_sd_tree_ok =0;
int delete_sd_tree(unsigned long slice_idx,int op)
{
	struct page *page = pfn_to_page(slice_idx);
	int ret = -1;
	int cpu = smp_processor_id();

	if(page)
	{
		preempt_disable();
		spt_thread_start(g_thrd_id);
		ret = delete_data(pgclst,page);
		preempt_enable();
		spt_thread_exit(g_thrd_id);
		
		if(ret < 0)
		{
			printk("delete_sd_tree %p,op is %d,err ret is %d\r\n",page,op,ret);
			printk("org_slice %p count %d,mapcount %d\r\n",page,atomic_read(&page->_count),atomic_read(&page->_mapcount));
			printk("delete thread erro code is %d\r\n",spt_get_errno());
			dump_stack();
		}
		else
		{
			atomic64_add(1,(atomic64_t*)&delete_sd_tree_ok);
		}
	}
	return ret;

}
