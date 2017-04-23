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
#include "vector.h"
#include "chunk.h"
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
			atomic_sub(1,&page->_mapcount);
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

int pone_case_init(void)
{
	int thread_num = 0;
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
		if(r_data !=NULL)
		{
			atomic64_add(1,(atomic64_t*)&insert_sd_tree_ok);
		}
		spt_thread_exit(g_thrd_id);
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
		per_cpu(process_enter_check,cpu) = 1;
		spt_thread_start(g_thrd_id);
		ret = delete_data(pgclst,page);
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
		spt_thread_exit(g_thrd_id);
		per_cpu(process_enter_check,cpu) = 0;
	}
	return ret;

}
