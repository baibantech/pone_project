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

struct page *get_page_ptr(char *pdata)
{
	return (struct page*)(pdata);
}

char *tree_get_key_from_data(char *pdata)
{
	struct page *page = NULL;
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
			__free_pages(page,0);
		}
	}
	return;
}

char *tree_construct_data_from_key(char *pkey)
{
    char *pdata;

    pdata = (char *)kmalloc(DATA_SIZE,GFP_KERNEL);
    if(pdata == NULL)
        return NULL;
    memcpy(pdata, pkey, DATA_SIZE);
    return (char *)pdata;
}

int pone_case_init(void)
{
	int thread_num = 0;
	set_data_size(PAGE_SIZE);
	
	thread_num = 2*num_possible_cpus();
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
