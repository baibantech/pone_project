/*************************************************************************
	> File Name: pone_linux_adp.h
	> Author: lijiyong0303
	> Mail: lijiyong0303@163.com 
	> Created Time: Wed 02 Aug 2017 02:48:14 PM CST
 ************************************************************************/
#ifndef __PONE_LINUX_ADP_H__
#define __PONE_LINUX_ADP_H__

#include <linux/init.h>
#include <linux/kernel.h>
typedef int (*pone_linux_adp_func)(void *page);
typedef int (*pone_linux_adp_func_2)(void *page,void *page1);
extern pone_linux_adp_func    pone_anonymous_new_page ;
extern pone_linux_adp_func    pone_wp_new_page ;
extern pone_linux_adp_func    pone_page_add_mapcount;
extern pone_linux_adp_func	  pone_page_dec_mapcount;
extern pone_linux_adp_func    pone_page_free_check;

extern pone_linux_adp_func    pone_watched_page;
extern pone_linux_adp_func_2  pone_page_mark_volatile_cnt;

#define PONE_OK   0
#define PONE_ERR  1
#define PONE_NO_INIT 2

static inline int  PONE_RUN(pone_linux_adp_func func, struct page *page)
{
	if(func)
	{
		return func(page);
	}
	return PONE_NO_INIT;
}

static inline int  PONE_RUN_2(pone_linux_adp_func_2 func, struct page *page,struct page *page1)
{
	if(func)
	{
		return func(page,page1);
	}
	return PONE_NO_INIT;
}

#define PONE_DEBUG(format,...) \
	printk("FILE: "__FILE__",LINE: %d: "format"/r/n",__LINE__, ##__VA_ARGS__)

#endif

