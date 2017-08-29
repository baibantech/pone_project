/*************************************************************************
	> File Name: pone_time.h
	> Author: lijiyong0303
	> Mail: lijiyong0303@163.com 
	> Created Time: Mon 26 Jun 2017 10:03:54 AM CST
 ************************************************************************/
#ifndef  __PONE_TIME_H__
#define __PONE_TIME_H__

#include <linux/init.h>
#include <linux/kernel.h>

typedef struct pone_time_point_tag
{
	char *name ;
	unsigned long long time_total;
	unsigned long long time_cnt;
	unsigned long long time_max;
	unsigned long long time_min;
}pone_time_point;

#define PONE_TIMEPOINT_DEFINE(x) \
	pone_time_point x = \
	{	.name = #x,\
		.time_total = 0,\
		.time_cnt = 0,\
		.time_max = 0 ,\
		.time_min = 0xFFFFFFFF,\
	}

#define PONE_TIMEPOINT_DEC(x)\
	extern pone_time_point x

#define PONE_TIMEPOINT_SET(x,time) \
{\
	unsigned long long cur_time; \
	unsigned long long result = time; \
	atomic64_add(result ,(atomic64_t*)&x.time_total);\
	atomic64_add(1,(atomic64_t*)&x.time_cnt);\
	do \
	{	\
		cur_time = x.time_max; \
		if(result > cur_time) \
		{\
			if(cur_time == atomic64_cmpxchg((atomic64_t*)&x.time_max,cur_time,result))\
			{\
				break;\
			}\
		}\
		else \
		{\
			break;\
		}\
	}while(1);\
\
	do \
	{ \
		cur_time = x.time_min; \
		if(result < cur_time)\
		{\
			if(cur_time == atomic64_cmpxchg((atomic64_t*)&x.time_min,cur_time,result))\
			{\
				break; \
			}\
		}\
		else \
		{\
			break;\
		}\
	}while(1); \
}


PONE_TIMEPOINT_DEC(slice_out_que);
PONE_TIMEPOINT_DEC(slice_protect_ok);
PONE_TIMEPOINT_DEC(slice_protect_func);
PONE_TIMEPOINT_DEC(slice_mm_notify_start);
PONE_TIMEPOINT_DEC(slice_mm_notify_end);
PONE_TIMEPOINT_DEC(slice_in_watch_que);


PONE_TIMEPOINT_DEC(slice_out_watch_que);
PONE_TIMEPOINT_DEC(slice_insert_tree);
PONE_TIMEPOINT_DEC(slice_merge_tree);
PONE_TIMEPOINT_DEC(slice_changeref_ok);
PONE_TIMEPOINT_DEC(slice_changeref_func);
PONE_TIMEPOINT_DEC(slice_changeref_start);
PONE_TIMEPOINT_DEC(slice_changeref_end);
PONE_TIMEPOINT_DEC(slice_page_recycle);
PONE_TIMEPOINT_DEC(lf_order_que_write);
PONE_TIMEPOINT_DEC(lf_order_que_read);
PONE_TIMEPOINT_DEC(dvd_data_insert);
PONE_TIMEPOINT_DEC(dvd_data_delete);
PONE_TIMEPOINT_DEC(dvd_vb_del_ins);

#endif
