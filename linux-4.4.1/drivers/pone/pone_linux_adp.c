/*************************************************************************
	> File Name: pone_linux_adp.c
	> Author: lijiyong0303
	> Mail: lijiyong0303@163.com 
	> Created Time: Wed 02 Aug 2017 10:13:24 AM CST
 ************************************************************************/

/* descripe the linux adp interface for pone*/

#include <linux/kernel.h>

#include <pone/pone_linux_adp.h>

pone_linux_adp_func   pone_anonymous_new_page = NULL;
pone_linux_adp_func   pone_wp_new_page = NULL;
pone_linux_adp_func   pone_page_add_mapcount = NULL;
pone_linux_adp_func   pone_page_dec_mapcount = NULL;
pone_linux_adp_func   pone_page_free_check = NULL;
pone_linux_adp_func   pone_page_reuse_check = NULL;

EXPORT_SYMBOL(pone_anonymous_new_page);
EXPORT_SYMBOL(pone_wp_new_page);
EXPORT_SYMBOL(pone_page_add_mapcount);
EXPORT_SYMBOL(pone_page_dec_mapcount);
EXPORT_SYMBOL(pone_page_free_check);
EXPORT_SYMBOL(pone_page_reuse_check);
