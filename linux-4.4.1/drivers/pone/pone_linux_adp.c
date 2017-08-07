/*************************************************************************
	> File Name: pone_linux_adp.c
	> Author: lijiyong0303
	> Mail: lijiyong0303@163.com 
	> Created Time: Wed 02 Aug 2017 10:13:24 AM CST
 ************************************************************************/

/* descripe the linux adp interface for pone*/

#include <linux/kernel.h>

#include <pone/pone_linux_adp.h>
#include <pone/slice_state.h>

pone_linux_adp_func   pone_anonymous_new_page = NULL;
pone_linux_adp_func   pone_wp_new_page = NULL;
pone_linux_adp_func   pone_page_add_mapcount = NULL;
pone_linux_adp_func   pone_page_dec_mapcount = NULL;
pone_linux_adp_func   pone_page_free_check = NULL;
pone_linux_adp_func   pone_watched_page  = NULL;
pone_linux_adp_func_2  pone_page_mark_volatile_cnt = NULL;

EXPORT_SYMBOL(pone_anonymous_new_page);
EXPORT_SYMBOL(pone_wp_new_page);
EXPORT_SYMBOL(pone_page_add_mapcount);
EXPORT_SYMBOL(pone_page_dec_mapcount);
EXPORT_SYMBOL(pone_page_free_check);
EXPORT_SYMBOL(pone_watched_page);
EXPORT_SYMBOL(pone_page_mark_volatile_cnt);


void pone_linux_adp_init(void)
{
	pone_anonymous_new_page = pone_slice_alloc_process;
	pone_wp_new_page = pone_slice_alloc_process;
	pone_page_add_mapcount = pone_slice_add_mapcount_process;
	pone_page_dec_mapcount = pone_slice_dec_mapcount_process;;
	pone_page_free_check = pone_slice_free_check_process;
	pone_watched_page  = pone_slice_watched_page;
	pone_page_mark_volatile_cnt = pone_slice_mark_volatile_cnt;
}
