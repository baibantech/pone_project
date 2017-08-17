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
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <linux/wait.h>
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include "vector.h"
#include "chunk.h"
#include "pone_time.h"

#ifdef SLICE_OP_CLUSTER_QUE
lfrwq_t *slice_cluster_que[64] = {NULL};
lfrwq_t *slice_cluster_watch_que[64] = {NULL};
lfrwq_reader * que_reader[64] =  {NULL};
lfrwq_reader *watch_que_reader[64] = {NULL};
#endif
DEFINE_SPINLOCK(sd_tree_lock);
int *ljy_vmalloc_area = NULL;
EXPORT_SYMBOL(ljy_vmalloc_area);
#define op_code_off (sizeof(int))
#define page_mem_off (4096*3)
int  op_index = 0;
int spt_divide_thread_run = 0;
DECLARE_WAIT_QUEUE_HEAD(pone_divide_thread_run);
int pone_thread_num = 0;
int pone_thread_interval = 2;

typedef struct pone_que_stat
{
	unsigned long long page_num;
	unsigned long long slice_mm[512];
}pone_que_stat;

pone_que_stat *pone_que_stat_ptr = NULL;

void pone_que_stat_init(void)
{
	if(!pone_que_stat_ptr)
	{
		pone_que_stat_ptr = kmalloc(pone_thread_num * sizeof(pone_que_stat),GFP_KERNEL);
		if(pone_que_stat_ptr)
		{
			memset(pone_que_stat_ptr ,0, pone_thread_num*sizeof(pone_que_stat));
		}
		else
		{
			PONE_DEBUG("malloc pone que stat ptr err\r\n");
		}
	}
}

int pone_que_stat_lookup(unsigned long long page_mm)
{
	int stat_id = page_mm %pone_thread_num;
	int i = 0;

	for(i = 0;i < 512 ;i++)
	{
		if(0 == pone_que_stat_ptr[stat_id].slice_mm[i])
		{
			pone_que_stat_ptr[stat_id].slice_mm[i] = page_mm;
			break;
		}
		else if(pone_que_stat_ptr[stat_id].slice_mm[i] == page_mm)
		{
			break;
		}
	}
	if( i == 512)
	{
		PONE_DEBUG("max mm in one que\r\n");
	}
	pone_que_stat_ptr[stat_id].page_num++;
	return stat_id;
}

void show_pone_que_stat(void)
{
	int i ,j;

	if(pone_que_stat_ptr)
	{
		for(i = 0 ; i < pone_thread_num; i++)
		{
			printk("que index %d,page_num %lld\r\n",i ,pone_que_stat_ptr[i].page_num);
			for(j = 0 ;j < 512 ; j++)
			{
				if(pone_que_stat_ptr[i].slice_mm[j]!= 0)
				{
					printk("under que slice_mm 0x%llx\r\n",pone_que_stat_ptr[i].slice_mm[j]);
				}
			}
		}
	}

}

void record_tree(struct page *page,char op)
{
	if(ljy_vmalloc_area != NULL)
	{
		char *begin_op = ljy_vmalloc_area+1;
		char *begin_page = (char*)ljy_vmalloc_area + page_mem_off;
		void *page_addr = kmap_atomic(page);

		begin_op[op_index] = op;
		memcpy(begin_page + op_index*4096,page_addr,4096);
		kunmap_atomic(page_addr);
		op_index++;
		*ljy_vmalloc_area = op_index;
	}
	else
	{
		printk("err ljy_vmalloc_area NULL\r\n");
	}
}



struct task_struct *spt_thread_id[128] = {NULL};
struct task_struct *spt_divid_thread = NULL;

unsigned long long data_map_key_cnt = 0;
unsigned long long data_unmap_key_cnt = 0;

int thread_map_cnt[64] = {0};
int thread_zero_cnt[64] = {0};
struct page *get_page_ptr(char *pdata)
{
	return (struct page*)(pdata);
}

char *tree_get_key_from_data(char *pdata)
{
	struct page *page = NULL;
	void *page_addr = NULL;
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
	if(thread_map_cnt[g_thrd_id] == 0)
	{
		thread_zero_cnt[g_thrd_id]++;
	}
	thread_map_cnt[g_thrd_id]++;

	page_addr =  kmap_atomic(page);
#if 0
	if(page->page_mem != NULL)
	{	
		if(0 != memcmp(page->page_mem,page_addr,PAGE_SIZE))
		{
			printk("page mem change err\r\n");
		}

	}	
	else
	{
		printk("page mem ptr is null\r\n");
	}
#endif
	return page_addr;

}
void tree_free_key(char *key)
{
	atomic64_add(1,(atomic64_t*)&data_unmap_key_cnt);
	kunmap_atomic(key);
	return;
}
unsigned long long page_free_cnt;

void tree_free_data(char *pdata)
{
	struct page *page = NULL;
	if(NULL != pdata)
	{
		page = get_page_ptr(pdata);
		if(NULL  != page)
		{	
#if 0
			if(page->page_mem != NULL)
			{
				kfree(page->page_mem);
				page->page_mem = NULL;
			}
			else
			{
				printk("page mem null \r\n");
			}
#endif
			put_page(page);
            atomic64_add(1,(atomic64_t*)&page_free_cnt);

		}
	}
	return;
}

char *tree_construct_data_from_key(char *pkey)
{
    char *pdata;
	struct page *key_page = NULL;
	key_page = alloc_pages(GFP_ATOMIC,0);
	if(key_page == NULL)
	{
		printk("key_page alloc failed\r\n");
	}
    pdata = kmap_atomic(key_page);
    memcpy(pdata, pkey, DATA_SIZE);
	kunmap_atomic(pdata);
	return (char *)key_page;
}

#ifdef SLICE_OP_CLUSTER_QUE

static int splitter_process_thread(void *data)
{
	int cpu = smp_processor_id();
	long thread_idx = (long)data;
	lfrwq_reader *watch_reader = watch_que_reader[thread_idx];
	lfrwq_reader *reader =  que_reader[thread_idx];
	lfrwq_reader *deamon_reader = &per_cpu(int_slice_deamon_que_reader,cpu);
	int ret = 0;
	int w_ret = 0;
	int d_ret = 0;
	printk("spt thread run\r\n");
	__set_current_state(TASK_RUNNING);
	do
	{
		ret = process_state_que(slice_cluster_que[thread_idx],reader,1);
		if(ret!= -2)
		{
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		}

	}while(!kthread_should_stop());

	return 0;
}
#else
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
#endif


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


void splitter_divide_thread(void *arg)
{
	unsigned long long start_jiffies = 0;
	unsigned long long end_jiffies = 0;
	unsigned long long cost_time = 0;
	__set_current_state(TASK_RUNNING);
	while(!kthread_should_stop())
	{
		if(spt_divide_thread_run)
		{
			start_jiffies = get_jiffies_64();
			spt_divided_scan(arg);
			end_jiffies = get_jiffies_64();
			cost_time = jiffies_to_msecs(end_jiffies - start_jiffies);
			if(cost_time > 10000)
			{
				msleep(10000);
			}
			else
			{
				msleep(10000 - cost_time);
			}
		}
		else
		{
			wait_event_freezable(pone_divide_thread_run,spt_divide_thread_run);
		}
	}
}

int pone_case_init(void)
{
	int thread_num = 0;
	int cpu = 0;
	int i = 0;
	int thrd_cnt = 0;
	
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
	
	pone_thread_num = 5;
	#ifdef SLICE_OP_CLUSTER_QUE
		for(i = 0 ; i < pone_thread_num;i++)
		{
			slice_cluster_que[i] = lfrwq_init(8192*64,1024,2);
			slice_cluster_watch_que[i] = lfrwq_init(8192,512,2);

			if((NULL == slice_cluster_que[i])|| (NULL == slice_cluster_watch_que[i]))
			{
				printk("init que err\r\n");
				return -1;
			}
			que_reader[i] = kmalloc(sizeof(lfrwq_reader),GFP_KERNEL);
			if(NULL == que_reader[i])
			{
				printk("alloc que reader err\r\n");
				return -1;
			}
			
			memset(que_reader[i],0,sizeof(lfrwq_reader));
			que_reader[i]->local_idx = -1;
			watch_que_reader[i] = kmalloc(sizeof(lfrwq_reader),GFP_KERNEL);
			if(NULL == watch_que_reader[i])
			{
				printk("alloc watch que reader err \r\n");
				return -1;
			}
			
			memset(watch_que_reader[i],0,sizeof(lfrwq_reader));
			watch_que_reader[i]->local_idx = -1;

		}

	#endif
	
	pone_que_stat_init();	
	for_each_online_cpu(cpu)
	{
		if(cpu == 0)
		{
			continue;
		}
		if(((cpu-1) %pone_thread_interval) != 0 )
		{
			continue;
		}
		if(thrd_cnt >= pone_thread_num)
		{
			break;
		}
		spt_thread_id[cpu] = kthread_create(splitter_process_thread,thrd_cnt,"spthrd_%d",cpu);
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
		thrd_cnt++;
	}
	pone_thread_num = thrd_cnt;

	spt_divid_thread = kthread_create(splitter_divide_thread ,pgclst,"spthrd_divide");

	if(IS_ERR(spt_divid_thread))
	{
		printk("err create spt_divide_thread \r\n");
		return -1;
	}
	else
	{
		wake_up_process(spt_divid_thread);
	}
	return 0;
}
unsigned long long insert_sd_tree_ok =0;
char * insert_sd_tree(unsigned long slice_idx)
{
	struct page *page = pfn_to_page(slice_idx);
	void *r_data = NULL;
	int cpu = smp_processor_id();
	if(page)
	{
//		spin_lock(&sd_tree_lock);
		spt_thread_start(g_thrd_id);
		//record_tree(page,1);
		r_data = insert_data(pgclst,(char*)page);
		if(cpu != smp_processor_id())
		{
			printk("cpu error !!!!!!!!!!!!!!\r\n");
		}
		spt_thread_exit(g_thrd_id);
		
//		spin_unlock(&sd_tree_lock);
		if(r_data !=NULL)
		{
			atomic64_add(1,(atomic64_t*)&insert_sd_tree_ok);
		}
	}
	return r_data;
}
unsigned long long delete_sd_tree_ok =0;
unsigned long long delete_sd_tree_no_found  =0;
unsigned long long delete_merge_page_cnt = 0;
unsigned long long data_cmp_cnt = 0;
unsigned long long data_cmp_err = 0;
unsigned long long data_cmp_ptr_null = 0;
void slice_data_cmp(void *data,unsigned int lineno)
{
#if 0
	struct page *page = data;
	void *page_addr = kmap_atomic(page);

	if(page->page_mem != NULL)
	{
		atomic64_add(1,(atomic64_t*)&data_cmp_cnt);
		if(0 != memcmp(page->page_mem,page_addr,PAGE_SIZE))
		{
			atomic64_add(1,(atomic64_t*)&data_cmp_err);
			printk("page mem change err line no %d\r\n",lineno);
		}
	}
	else
	{
		atomic64_add(1,(atomic64_t*)&data_cmp_ptr_null);
		printk("page mem ptr is null line no %d\r\n",lineno);
	}
	kunmap_atomic(page_addr);
#endif
}
extern void printk_debug_map_cnt_id(int);
extern void print_debug_path(int id);

struct page* page_delete_failed[128] = {NULL};

void  redelete_sd_tree(struct page *page)
{
	int ret = -1;
	if(page)
	{
		preempt_disable();
		spt_thread_start(g_thrd_id);
		ret =  delete_data(pgclst,page);
		spt_thread_exit(g_thrd_id);
		preempt_enable();
		printk("redelete page %p ,ret %d .err_code %d\r\n",page,ret ,spt_get_errno());
	}
}
int delete_sd_tree(unsigned long slice_idx,int op)
{
	struct page *page = pfn_to_page(slice_idx);
	int ret = -1;
	int cpu = smp_processor_id();
	void *page_addr = NULL;
	int i = 0;
	unsigned long long no_found_id = 0;
	if(page)
	{
try_again:

		preempt_disable();
	//	spin_lock(&sd_tree_lock);
		spt_thread_start(g_thrd_id);
		//record_tree(page,2);
		ret = delete_data(pgclst,page);
		
		spt_thread_exit(g_thrd_id);
	//	spin_unlock(&sd_tree_lock);
		if(ret < 0)
		{
			if(-10000 == ret)
			{
				printk_debug_map_cnt_id(g_thrd_id);
				
				no_found_id = atomic64_add_return(1 ,(atomic64_t*)&delete_sd_tree_no_found);
				//if(-10000 == ret)
				//	print_debug_path(g_thrd_id);
				
				page_addr = kmap_atomic(page);
				printk("\r\n");
				if(-10000 == ret)
				for(i = 0 ; i < 4096 ;i++)
				{
					if(i%32 == 0)
						printk("\r\n");
					printk("%02x ",*((unsigned char*)page_addr +i));
				}
				printk("\r\n");
				kunmap_atomic(page_addr);
				slice_data_cmp(page,__LINE__);
				
				printk("delete_sd_tree %p,op is %d,err ret is %d\r\n",page,op,ret);
				printk("org_slice %p count %d,mapcount %d\r\n",page,atomic_read(&page->_count),atomic_read(&page->_mapcount));
				printk("delete thread erro code is %d\r\n",spt_get_errno());
				
				for(i =0 ; i < 10; i++)
				{
	           	    preempt_enable();
					msleep(2);
					preempt_disable();
					spt_thread_start(g_thrd_id);
					ret = delete_data(pgclst,page);
					spt_thread_exit(g_thrd_id);
					if(ret < 0)
					{
						printk("rdelete sd tree ret %d\r\n",ret);
					}
					else
					{
						printk("rdelete ok cnt is %d\r\n",i);
						print_debug_path(g_thrd_id);
						atomic64_sub(1 ,(atomic64_t*)&delete_sd_tree_no_found);
						break;
					}
				}
				if( 10 == i) /*redelete fail*/
				{
					if(no_found_id <= 128)
					{
						page_delete_failed[no_found_id -1] = page;							
					}
				}
			}
			else
			{
				 ret = spt_get_errno();
				 if(ret == SPT_MASKED)
				 {
					spt_thread_start(g_thrd_id);
					spt_thread_exit(g_thrd_id);
					preempt_enable();
					goto try_again;
				 }
				 else if (ret == SPT_WAIT_AMT)
				 {
					spt_thread_start(g_thrd_id);
					spt_thread_exit(g_thrd_id);
					preempt_enable();
					goto try_again;
				 }
				 else if(ret == SPT_NOMEM)
				 {
					BUG();
				 }
				 else
				 {
					BUG();
				}	
			}
		}
		else
		{
			if(ret > 0)
			{
				atomic64_add(1,(atomic64_t *)&delete_merge_page_cnt);
			}
			atomic64_add(1,(atomic64_t*)&delete_sd_tree_ok);
		}
		
		preempt_enable();
	}
	return ret;
}


#ifdef SLICE_OP_CLUSTER_QUE
int lfrwq_in_cluster_que(void *data,unsigned long que_id)
{
	return lfrwq_inq(slice_cluster_que[que_id%pone_thread_num],data);
}

int lfrwq_in_cluster_watch_que(void *data,unsigned long que_id)
{
	return lfrwq_inq(slice_cluster_watch_que[que_id%pone_thread_num],data);
}
void splitter_wakeup_cluster(void)
{
	int i;
	int ret = 0;
	for(i = 0;i < pone_thread_num ; i++)
	{
		ret += lfrwq_set_r_max_idx(slice_cluster_que[i],lfrwq_get_w_idx(slice_cluster_que[i]));
		//ret += lfrwq_set_r_max_idx(slice_cluster_watch_que[i],lfrwq_get_w_idx(slice_cluster_watch_que[i]));
		if(ret)
		{
			if(spt_thread_id[1 + pone_thread_interval*i]->state != TASK_RUNNING)
			wake_up_process(spt_thread_id[1+pone_thread_interval*i]);		
		}
		ret = 0;

	}
	ret = 0;
	
	ret += lfrwq_set_r_max_idx(slice_deamon_que,lfrwq_get_w_idx(slice_deamon_que));
	if(ret)
	{
		splitter_thread_wakeup();
	}
}

void wakeup_splitter_thread_by_que(long que_id)
{
	que_id = que_id%pone_thread_num;
	lfrwq_set_r_max_idx(slice_cluster_que[que_id],lfrwq_get_w_idx(slice_cluster_que[que_id]));
	if(spt_thread_id[1 + pone_thread_interval*que_id]->state != TASK_RUNNING)
		wake_up_process(spt_thread_id[1+pone_thread_interval*que_id]);		
}

#endif
