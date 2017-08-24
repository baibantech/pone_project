/*************************************************************************
	> File Name: timepoint.c
	> Author: lijiyong0303
	> Mail: lijiyong0303@163.com 
	> Created Time: Fri 23 Jun 2017 03:15:53 PM CST
 ************************************************************************/

#include<linux/init.h>
#include<linux/kernel.h>
#include<linux/mm.h>
#include "pone_time.h"

#define PONE_TIMEPOINT_PTR(x)  (&x)

PONE_TIMEPOINT_DEFINE(slice_out_que);
PONE_TIMEPOINT_DEFINE(slice_protect_ok);
PONE_TIMEPOINT_DEFINE(slice_protect_func);
PONE_TIMEPOINT_DEFINE(slice_mm_notify_start);
PONE_TIMEPOINT_DEFINE(slice_mm_notify_end);
PONE_TIMEPOINT_DEFINE(slice_in_watch_que);

PONE_TIMEPOINT_DEFINE(slice_out_watch_que);
PONE_TIMEPOINT_DEFINE(slice_insert_tree);
PONE_TIMEPOINT_DEFINE(slice_merge_tree);
PONE_TIMEPOINT_DEFINE(slice_changeref_ok);
PONE_TIMEPOINT_DEFINE(slice_changeref_func);
PONE_TIMEPOINT_DEFINE(slice_changeref_start);
PONE_TIMEPOINT_DEFINE(slice_changeref_end);

PONE_TIMEPOINT_DEFINE(slice_page_recycle);
PONE_TIMEPOINT_DEFINE(lf_order_que_write);
PONE_TIMEPOINT_DEFINE(lf_order_que_read);
PONE_TIMEPOINT_DEFINE(dvd_data_delete);
PONE_TIMEPOINT_DEFINE(dvd_data_insert);
PONE_TIMEPOINT_DEFINE(dvd_vb_del_ins);

pone_time_point* pone_time_array[] =
{
#if 0
	PONE_TIMEPOINT_PTR(slice_out_que),
	PONE_TIMEPOINT_PTR(slice_protect_ok),
	PONE_TIMEPOINT_PTR(slice_protect_func),
	PONE_TIMEPOINT_PTR(slice_mm_notify_start),
	PONE_TIMEPOINT_PTR(slice_mm_notify_end),

	PONE_TIMEPOINT_PTR(slice_in_watch_que),
	
	
	PONE_TIMEPOINT_PTR(slice_out_watch_que),
	PONE_TIMEPOINT_PTR(slice_insert_tree),
	PONE_TIMEPOINT_PTR(slice_merge_tree),
	PONE_TIMEPOINT_PTR(slice_changeref_ok),
	PONE_TIMEPOINT_PTR(slice_changeref_func),
	PONE_TIMEPOINT_PTR(slice_changeref_start),
	PONE_TIMEPOINT_PTR(slice_changeref_end),
	PONE_TIMEPOINT_PTR(slice_page_recycle),
#endif
	PONE_TIMEPOINT_PTR(lf_order_que_write),
	PONE_TIMEPOINT_PTR(lf_order_que_read),
	PONE_TIMEPOINT_PTR(dvd_data_delete),
	PONE_TIMEPOINT_PTR(dvd_data_insert),
	PONE_TIMEPOINT_PTR(dvd_vb_del_ins),
	
};

void show_pone_time_stat(void)
{
	int i = 0;
	int num = sizeof(pone_time_array)/sizeof(pone_time_point *);
	pone_time_point * ptr = NULL;
	for (i = 0; i < num; i++)
	{
		ptr = pone_time_array[i];
		if(ptr)
		{
			printk("\r\n--------------------------------\r\n");
			printk("time point : %s\r\n", ptr->name);	
			printk("time total : %lld\r\n", ptr->time_total);	
			printk("time cnt   : %lld\r\n", ptr->time_cnt);
			if(ptr->time_cnt)
			printk("time av    : %lld\r\n", ptr->time_total/ptr->time_cnt);	
			printk("time max   : %lld\r\n", ptr->time_max);	
			printk("time min   : %lld\r\n", ptr->time_min);	
		}
	}
}


