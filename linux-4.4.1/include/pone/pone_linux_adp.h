/*************************************************************************
	> File Name: pone_linux_adp.h
	> Author: lijiyong0303
	> Mail: lijiyong0303@163.com 
	> Created Time: Wed 02 Aug 2017 02:48:14 PM CST
 ************************************************************************/
#include <linux/init.h>
#include <linux/kernel.h>
typedef int (*pone_linux_adp_func)(void *page);
extern pone_linux_adp_func    pone_anonymous_new_page ;
extern pone_linux_adp_func    pone_wp_new_page ;
extern pone_linux_adp_func    pone_page_add_mapcount;
extern pone_linux_adp_func	  pone_page_dec_mapcount;
extern pone_linux_adp_func    pone_page_free_check;
extern pone_linux_adp_func	  pone_page_reuse_check;
