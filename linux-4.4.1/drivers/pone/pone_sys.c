/*************************************************************************
	> File Name: pone_sys.c
	> Author: ma6174
	> Mail: ma6174@163.com 
	> Created Time: Tue 10 Jan 2017 01:55:04 AM PST
 ************************************************************************/

#include<linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/sched.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <pone/slice_state.h>
#include <pone/pone.h>
#include <pone/slice_state_adpter.h>
#include <pone/lf_rwq.h>
#include "lf_order.h"
#include "vector.h"
#include "chunk.h"

extern struct page *release_merge_page;
extern unsigned long long process_que_num;
extern unsigned long slice_watch_que_debug;
extern unsigned long slice_que_debug;

extern unsigned long long slice_alloc_num ;
extern unsigned long long slice_change_volatile_ok ;

extern unsigned long long slice_out_que_num;
extern unsigned long long slice_protect_err;
extern unsigned long long virt_page_release_merge_ok;
extern unsigned long long virt_page_release_merge_err;
extern unsigned long long slice_que_change_watch;
extern unsigned long long slice_mem_que_free;

extern unsigned long long slice_out_watch_que_num;
extern unsigned long long slice_insert_sd_tree_err;
extern unsigned long long slice_new_insert_num ;
extern unsigned long long slice_change_ref_err;
extern unsigned long long slice_merge_num ;

extern unsigned long long slice_fix_free_num;
extern unsigned long long slice_volatile_free_num;
extern unsigned long long slice_other_free_num;
extern unsigned long long slice_watch_free_num;
extern unsigned long long slice_sys_free_num;

extern unsigned long long slice_mem_watch_reuse ;

extern unsigned long long slice_deamon_find_volatile;
extern unsigned long long slice_deamon_find_watch;
extern unsigned long long slice_deamon_in_que_fail;


unsigned long long slice_alloc_num_bak ;
unsigned long long slice_change_volatile_ok_bak ;

unsigned long long slice_out_que_num_bak;
unsigned long long slice_protect_err_bak;
unsigned long long virt_page_release_merge_ok_bak;
unsigned long long virt_page_release_merge_err_bak;
unsigned long long slice_que_change_watch_bak;
unsigned long long slice_mem_que_free_bak;

unsigned long long slice_out_watch_que_num_bak;
unsigned long long slice_insert_sd_tree_err_bak;
unsigned long long slice_new_insert_num_bak ;
unsigned long long slice_change_ref_err_bak;
unsigned long long slice_merge_num_bak ;

unsigned long long slice_fix_free_num_bak;
unsigned long long slice_volatile_free_num_bak;
unsigned long long slice_other_free_num_bak;
unsigned long long slice_watch_free_num_bak;
unsigned long long slice_sys_free_num_bak;

unsigned long long slice_mem_watch_reuse_bak ;

unsigned long long slice_deamon_find_volatile_bak;
unsigned long long slice_deamon_find_watch_bak;
unsigned long long slice_deamon_in_que_fail_bak;

unsigned long long page_free_cnt_bak;
unsigned long long data_map_key_cnt_bak;
unsigned long long data_unmap_key_cnt_bak;
unsigned long long delete_sd_tree_ok_bak ;
unsigned long long delete_sd_tree_no_found_bak ;
unsigned long long delete_merge_page_cnt_bak;
unsigned long long insert_sd_tree_ok_bak ;
unsigned long long slice_pre_fix_check_cnt_bak ;

unsigned long long slice_copy_pte_cnt_bak ;
unsigned long long slice_copy_pte_cnt1_bak ;

extern struct task_struct *spt_deamon_thread;
extern int debug_statistic(cluster_head_t *head);
extern void debug_cluster_travl(cluster_head_t *head);

extern struct pone_hash_head *pone_hash_table;
extern unsigned long long page_free_cnt;
extern unsigned long long data_map_key_cnt;
extern unsigned long long data_unmap_key_cnt;
extern unsigned long long delete_sd_tree_ok ;
extern unsigned long long delete_sd_tree_no_found ;
extern unsigned long long delete_merge_page_cnt;
extern unsigned long long insert_sd_tree_ok ;
extern unsigned long long slice_pre_fix_check_cnt ;

extern unsigned long long slice_copy_pte_cnt ;
extern unsigned long long slice_copy_pte_cnt1 ;
extern void* page_insert;
extern char page_in_tree[];
extern int deamon_scan_period;

extern void printk_debug_map_cnt(void);
extern int lfrwq_len(lfrwq_t *qh);
#ifdef CONFIG_SYSFS

#define PONE_ATTR_RO(_name) static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define PONE_ATTR(_name)  static struct kobj_attribute _name##_attr =	__ATTR(_name, 0644, _name##_show, _name##_store)

void show_page_mem(unsigned char *ptr)
{
	int i = 0;
	int j = 0;

	for(i = 0 ;i < 128 ;i++)
	{
			
		printk("\r\n");
		for(j = 0;j < 32;j++)
		{
			printk("%02x ",*(ptr+i*32 +j));
		}
	}
}

void show_page_err_info(void)
{
	char *page = NULL;
	show_page_mem(page_in_tree);
	printk("-----------------------------------------------------------\r\n");
	if(page_insert)
	{
		page = kmap_atomic(page_insert);
		show_page_mem(page);
		kunmap_atomic(page);
	}
}

extern int pone_case_init(void);
int pone_hash_table_show(char *buf)
{
	int i = 0;
	int len = 0;
	int user_count_sum = 0;
	struct pone_desc *pos = NULL;
	for(i = 0 ; i < 0x10000 ; i++)
	{
		if(hlist_empty(&pone_hash_table[i].head))
		{
			continue;
		}
		else
		{
			len += sprintf(buf,"-----------crc %x info-----------\r\n",i);
			hlist_for_each_entry(pos,&pone_hash_table[i].head,hlist);
			{
				len += sprintf(buf,"slice idx %ld.user_count %d\r\n",pos->slice_idx,pos->user_count);
				user_count_sum += pos->user_count;
			}
		}

	}
	len += sprintf(buf,"total user count is %d\r\n",user_count_sum);
	return len;
}


int lfrwq_info_get(lfrwq_t *qh, char *buf)
{
	int len = 0;
	len += sprintf(buf+len, "read index: %lld\r\n",qh->r_idx);
	len += sprintf(buf+len, "write index: %lld\r\n",qh->w_idx);
	len += sprintf(buf+len, "read  max idex: %lld\r\n",qh->r_max_idx_period);
	return len;
}

int lfrwq_reader_info_get(lfrwq_reader *reader ,char *buf)
{
	int len = 0;

	len += sprintf(buf+len, "local pmt: %d\r\n",reader->local_pmt);
	len += sprintf(buf+len, "r_cnt: %d\r\n",reader->r_cnt);
	len += sprintf(buf+len, "local_blk: %d\r\n",reader->local_blk);
	len += sprintf(buf+len, "local_idx: %lld\r\n",reader->local_idx);
	
	return len;
}

int slice_file_info_get(char *buf)
{
	int i = 0;
	int len = 0;
	if(check_space)
	{
		for(i = 0;i <10;i++)
		{
			struct page *page = find_get_entry(check_space,i);
			if(radix_tree_exceptional_entry(page))
			{
				page = NULL;
			}

			if(page)
			{
				len += sprintf(buf+len,"tree index %d page index %ld\r\n",i,page_to_pfn(page));
			}
		}
		return len;
	}
	return 0;
}

extern void debug_lower_cluster_info_show(void);
extern void show_pone_que_stat(void);
extern void show_order_que_info(void);
static ssize_t pone_info_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int len = 0;
	int cpu;
	lfrwq_reader *reader = NULL;
#if 0
	if(slice_que)	
	{	
		len += sprintf(buf +len ,"slice_que info\r\n");
		len += lfrwq_info_get(slice_que,buf + len);
	}

	if(slice_watch_que)
	{
		len += sprintf(buf +len ,"slice_watch_que info\r\n");
		len += lfrwq_info_get(slice_watch_que,buf + len);
	}
	
	len += sprintf(buf +len ,"slice_que reader_info\r\n");

	for_each_online_cpu(cpu)
	{
		
		len += sprintf(buf +len ,"cpu idex :%d\r\n",cpu);
		reader = &per_cpu(int_slice_que_reader,cpu);
		len += lfrwq_reader_info_get(reader,buf+len);
	}

	len += sprintf(buf +len ,"slice_watch_que reader_info\r\n");

	for_each_online_cpu(cpu)
	{
		len += sprintf(buf +len ,"cpu idex :%d\r\n",cpu);
		reader = &per_cpu(int_slice_watch_que_reader,cpu);
		len += lfrwq_reader_info_get(reader,buf+len);
	}
#endif
	
	len += sprintf(buf +len ,"slice alloc cnt: %lld\r\n",slice_alloc_num);
	len += sprintf(buf +len ,"slice change volatile ok cnt: %lld\r\n",slice_change_volatile_ok);

	len += sprintf(buf +len ,"slice out que cnt: %lld\r\n",slice_out_que_num);
	len += sprintf(buf +len ,"virt page merge count : %d\r\n",virt_page_release_merge_ok);
	len += sprintf(buf +len ,"slice que change watch  cnt: %lld\r\n",slice_que_change_watch);
	len += sprintf(buf +len ,"slice mem  que free cnt: %lld\r\n",slice_mem_que_free);
	len += sprintf(buf +len ,"slice protect err cnt: %lld\r\n",slice_protect_err);
	len += sprintf(buf +len ,"virt page merge count err : %d\r\n",virt_page_release_merge_err);
	
	len += sprintf(buf +len ,"slice out watch que cnt: %lld\r\n",slice_out_watch_que_num);
	len += sprintf(buf +len ,"slice new insert cnt: %lld\r\n",slice_new_insert_num);
	len += sprintf(buf +len ,"slice merge cnt: %lld\r\n",slice_merge_num);
	len += sprintf(buf +len ,"slice insert sd tree err cnt: %lld\r\n",slice_insert_sd_tree_err);
	len += sprintf(buf +len ,"slice change ref err  cnt: %lld\r\n",slice_change_ref_err);
	
	
	
	len += sprintf(buf +len ,"slice fix free cnt: %lld\r\n",slice_fix_free_num);
	len += sprintf(buf +len ,"slice volatile free cnt: %lld\r\n",slice_volatile_free_num);
	len += sprintf(buf +len ,"slice other free cnt: %lld\r\n",slice_other_free_num);
	len += sprintf(buf +len ,"slice watch free cnt: %lld\r\n",slice_watch_free_num);
	len += sprintf(buf +len ,"slice sys free cnt: %lld\r\n",slice_sys_free_num);

	len += sprintf(buf +len ,"slice mem watch reuse cnt: %lld\r\n",slice_mem_watch_reuse);
	len += sprintf(buf +len ,"data map key  cnt: %lld\r\n",data_map_key_cnt);
	len += sprintf(buf +len ,"data unmap key cnt: %lld\r\n",data_unmap_key_cnt);
	len += sprintf(buf +len ,"delete sd tree ok cnt: %lld\r\n",delete_sd_tree_ok);
	len += sprintf(buf +len ,"delete sd tree no found cnt: %lld\r\n",delete_sd_tree_no_found);
	len += sprintf(buf +len ,"delete merge page cnt: %lld\r\n",delete_merge_page_cnt);
	len += sprintf(buf +len ,"insert sd tree ok cnt: %lld\r\n",insert_sd_tree_ok);
	len += sprintf(buf +len ,"slice pre fix check cnt: %lld\r\n",slice_pre_fix_check_cnt);

	len += sprintf(buf +len ,"slice deamon find volatile  cnt: %lld\r\n",slice_deamon_find_volatile);
	len += sprintf(buf +len ,"slice deamon fine watch cnt: %lld\r\n",slice_deamon_find_watch);
	len += sprintf(buf +len ,"slice deamon in que fail  cnt: %lld\r\n",slice_deamon_in_que_fail);
	len += sprintf(buf +len ,"deamon task state: %d\r\n",spt_deamon_thread->state);
	
	len += sprintf(buf +len ,"slice copy pte  count  : %lld\r\n",slice_copy_pte_cnt);
	len += sprintf(buf +len ,"slice copy pte1  count  : %lld\r\n",slice_copy_pte_cnt1);
	
	len += sprintf(buf +len ,"tree page free  cnt: %lld\r\n",page_free_cnt);
	len += sprintf(buf +len , "page recycle count %d\r\n",page_count(release_merge_page)-1);

	return len;

}


PONE_ATTR_RO(pone_info);
unsigned long pone_file_watch = 0xFFFFFFFFFFFFFFFF;
extern void debug_mm_pte(void);
extern cluster_head_t *plow_clst;
extern unsigned long long debug_refind[48];
extern unsigned long long debug_refind_max[48];
extern unsigned long long map_cnt_max[48];
extern int thread_map_cnt[64];
extern int thread_zero_cnt[64];
extern struct page* page_delete_failed[128];
extern void redelete_sd_tree(struct page *page);
static ssize_t pone_debug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	//debug_mm_pte();
	debug_cluster_travl(pgclst);
	
	int i = 0;
	for(i = 0; i <48;i++)
	{
		if(debug_refind[i] != 0 || debug_refind_max[i] != 0)
		{
			printk("idx %d,num %lld,max %lld\r\n",i,debug_refind[i],debug_refind_max[i]);
		}
	}
	for(i = 0; i <48;i++)
	{
		if(map_cnt_max[i] != 0)
		{
			printk("idx %d,map_cnt_max %lld\r\n",i,map_cnt_max[i]);
		}
	}
	
	for(i = 0; i <48;i++)
	{
		printk("zero cnt %d,map_cnr %d\r\n",thread_zero_cnt[i],thread_map_cnt[i]);
	}
	debug_statistic(pgclst);
	
	debug_cluster_travl(plow_clst);

	for(i = 0; i < 128 ;i++)
	{
		if(page_delete_failed[i]!= 0)
		{	
			redelete_sd_tree(page_delete_failed[i]);
		}
	}

	return sprintf(buf,"%ld",pone_file_watch);

}

static ssize_t pone_debug_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf,size_t count)
{
	int err;
	unsigned long store_tmp = 0;	
	err =  kstrtoul(buf,10,&store_tmp);
	printk("store_tmp is %ld\r\n",store_tmp);
#if 0
	//pone_file_watch = store_tmp;
	if(1 ==store_tmp)
	{
		slice_que_debug = 0;

	}
	if(store_tmp >1)
	{
		slice_watch_que_debug =0;
		slice_que_debug = 0;
	}
#endif


	return count;
}
PONE_ATTR(pone_debug);
extern void add_slice_volatile_cnt_test(unsigned int nid,unsigned long slice_id);
extern int slice_debug_area_show(void);
extern void print_host_virt_mem_pool(void);
extern void show_slice_volatile_cnt(void);
extern void show_pone_time_stat(void);
extern void spt_threadinfo_show(void);
extern unsigned long long deamon_sleep_period_in_que_fail;
extern unsigned long long deamon_sleep_period_in_loop;
static ssize_t pone_sd_tree_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{	
	//slice_debug_area_show();
	printk_debug_map_cnt();
	print_host_virt_mem_pool();	
	if(release_merge_page)
	{
		printk("release page count %d\r\n",page_count(release_merge_page));
		printk("release page state %lld\r\n",get_slice_state_by_id(page_to_pfn(release_merge_page)));
		printk("release page anon %d\r\n",PageAnon(release_merge_page));
	}
	printk("deamon_sleep_period_in_que_fail is %lld\r\n",deamon_sleep_period_in_que_fail);
	printk("deamon_sleep_period_in_loop is %lld\r\n",deamon_sleep_period_in_loop);

	//show_page_err_info();
	//add_slice_volatile_cnt_test(0,1000);
	show_slice_volatile_cnt();
	show_pone_time_stat();
	spt_threadinfo_show();
    debug_lower_cluster_info_show();
	show_pone_que_stat();
	return sprintf(buf,"check dmesg buffer11111");
}

PONE_ATTR_RO(pone_sd_tree);


static ssize_t pone_deamon_scan_period_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf,size_t count)
{
	int err;
	err =  kstrtoul(buf,10,&deamon_scan_period);
	printk("deamon_scan_period is %ld\r\n",deamon_scan_period);
	return count;
}

static ssize_t pone_deamon_scan_period_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,"%ld",deamon_scan_period);
}

PONE_ATTR(pone_deamon_scan_period);

extern int spt_divide_thread_run ;
extern struct wait_queue_head_t  pone_divide_thread_run;
static ssize_t pone_divide_thread_enable_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf,size_t count)
{
	int err;
	err =  kstrtoul(buf,10,&spt_divide_thread_run);
	printk("spt_divide_thread_run is %ld\r\n",spt_divide_thread_run);
	if(spt_divide_thread_run)
	{	
		wake_up_interruptible(&pone_divide_thread_run);

	}
	return count;
}

static ssize_t pone_divide_thread_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,"%d",spt_divide_thread_run);
}

PONE_ATTR(pone_divide_thread_enable);
extern int pone_run;

static ssize_t pone_run_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf,size_t count)
{
	int err;
	err =  kstrtoul(buf,10,&pone_run);
	printk("pone_run is %ld\r\n",pone_run);
	return count;
}

static ssize_t pone_run_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,"%d",pone_run);
}

PONE_ATTR(pone_run);

extern int pone_page_recycle_enable ;

static ssize_t pone_recycle_run_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf,size_t count)
{
	int err;
	err =  kstrtoul(buf,10,&pone_page_recycle_enable);
	printk("pone_recycle_run is %ld\r\n",pone_page_recycle_enable);
	return count;
}

static ssize_t pone_recycle_run_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,"%d",pone_page_recycle_enable);
}

PONE_ATTR(pone_recycle_run);


static ssize_t pone_stat_begin_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	slice_alloc_num_bak  = slice_alloc_num;
	slice_change_volatile_ok_bak = slice_change_volatile_ok;

	slice_out_que_num_bak = slice_out_que_num;
	slice_protect_err_bak = slice_protect_err;
	virt_page_release_merge_ok_bak = virt_page_release_merge_ok;;
	virt_page_release_merge_err_bak = virt_page_release_merge_err;
	slice_que_change_watch_bak = slice_que_change_watch ;
	slice_mem_que_free_bak = slice_mem_que_free;

	slice_out_watch_que_num_bak = slice_out_watch_que_num;
	slice_insert_sd_tree_err_bak = slice_insert_sd_tree_err;
	slice_new_insert_num_bak  = slice_new_insert_num;
	slice_change_ref_err_bak = slice_change_ref_err;
	slice_merge_num_bak  = slice_merge_num ;

	slice_fix_free_num_bak = slice_fix_free_num;
	slice_volatile_free_num_bak = slice_volatile_free_num;
	slice_other_free_num_bak = slice_other_free_num;
	slice_watch_free_num_bak = slice_watch_free_num;
	slice_sys_free_num_bak = slice_sys_free_num;

	slice_mem_watch_reuse_bak = slice_mem_watch_reuse;;

	slice_deamon_find_volatile_bak = slice_deamon_find_volatile;
	slice_deamon_find_watch_bak = slice_deamon_find_watch;
	slice_deamon_in_que_fail_bak  = slice_deamon_in_que_fail;
	
	page_free_cnt_bak = page_free_cnt;
	data_map_key_cnt_bak = data_map_key_cnt;
	data_unmap_key_cnt_bak = data_unmap_key_cnt;
	delete_sd_tree_ok_bak  = delete_sd_tree_ok;
	delete_sd_tree_no_found_bak = delete_sd_tree_no_found;
	delete_merge_page_cnt_bak = delete_merge_page_cnt;
	insert_sd_tree_ok_bak = insert_sd_tree_ok;
	slice_pre_fix_check_cnt_bak = slice_pre_fix_check_cnt;

	slice_copy_pte_cnt_bak = slice_copy_pte_cnt;
	slice_copy_pte_cnt1_bak = slice_copy_pte_cnt1;
	
	return sprintf(buf,"%s","begin stat\r\n");
}
PONE_ATTR_RO(pone_stat_begin);


static ssize_t pone_stat_end_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int len = 0;
	len += sprintf(buf +len ,"slice alloc cnt: %lld\r\n",slice_alloc_num - slice_alloc_num_bak);
	len += sprintf(buf +len ,"slice change volatile ok cnt: %lld\r\n",slice_change_volatile_ok - slice_change_volatile_ok_bak);

	len += sprintf(buf +len ,"slice out que cnt: %lld\r\n",slice_out_que_num - slice_out_que_num);
	len += sprintf(buf +len ,"virt page merge count : %d\r\n",virt_page_release_merge_ok - virt_page_release_merge_ok_bak);
	len += sprintf(buf +len ,"slice que change watch  cnt: %lld\r\n",slice_que_change_watch - slice_que_change_watch_bak);
	len += sprintf(buf +len ,"slice mem  que free cnt: %lld\r\n",slice_mem_que_free - slice_mem_que_free_bak);
	len += sprintf(buf +len ,"slice protect err cnt: %lld\r\n",slice_protect_err - slice_protect_err_bak);
	len += sprintf(buf +len ,"virt page merge count err : %d\r\n",virt_page_release_merge_err - virt_page_release_merge_err_bak);
	
	len += sprintf(buf +len ,"slice out watch que cnt: %lld\r\n",slice_out_watch_que_num - slice_out_watch_que_num_bak);
	len += sprintf(buf +len ,"slice new insert cnt: %lld\r\n",slice_new_insert_num - slice_new_insert_num_bak);
	len += sprintf(buf +len ,"slice merge cnt: %lld\r\n",slice_merge_num - slice_merge_num_bak);
	len += sprintf(buf +len ,"slice insert sd tree err cnt: %lld\r\n",slice_insert_sd_tree_err - slice_insert_sd_tree_err_bak);
	len += sprintf(buf +len ,"slice change ref err  cnt: %lld\r\n",slice_change_ref_err - slice_change_ref_err_bak);
	
	len += sprintf(buf +len ,"slice fix free cnt: %lld\r\n",slice_fix_free_num - slice_fix_free_num_bak);
	len += sprintf(buf +len ,"slice volatile free cnt: %lld\r\n",slice_volatile_free_num - slice_volatile_free_num_bak);
	len += sprintf(buf +len ,"slice other free cnt: %lld\r\n",slice_other_free_num - slice_other_free_num_bak);
	len += sprintf(buf +len ,"slice watch free cnt: %lld\r\n",slice_watch_free_num - slice_watch_free_num_bak);
	len += sprintf(buf +len ,"slice sys free cnt: %lld\r\n",slice_sys_free_num - slice_sys_free_num_bak);

	len += sprintf(buf +len ,"slice mem watch reuse cnt: %lld\r\n",slice_mem_watch_reuse - slice_mem_watch_reuse_bak);
	len += sprintf(buf +len ,"data map key  cnt: %lld\r\n",data_map_key_cnt - data_map_key_cnt_bak);
	len += sprintf(buf +len ,"data unmap key cnt: %lld\r\n",data_unmap_key_cnt - data_unmap_key_cnt_bak);
	len += sprintf(buf +len ,"delete sd tree ok cnt: %lld\r\n",delete_sd_tree_ok - delete_sd_tree_ok_bak);
	len += sprintf(buf +len ,"delete sd tree no found cnt: %lld\r\n",delete_sd_tree_no_found - delete_sd_tree_no_found_bak);
	len += sprintf(buf +len ,"delete merge page cnt: %lld\r\n",delete_merge_page_cnt - delete_merge_page_cnt_bak);
	len += sprintf(buf +len ,"insert sd tree ok cnt: %lld\r\n",insert_sd_tree_ok - insert_sd_tree_ok_bak);
	len += sprintf(buf +len ,"slice pre fix check cnt: %lld\r\n",slice_pre_fix_check_cnt - slice_pre_fix_check_cnt_bak);

	len += sprintf(buf +len ,"slice deamon find volatile  cnt: %lld\r\n",slice_deamon_find_volatile - slice_deamon_find_volatile_bak);
	len += sprintf(buf +len ,"slice deamon fine watch cnt: %lld\r\n",slice_deamon_find_watch - slice_deamon_find_watch_bak);
	len += sprintf(buf +len ,"slice deamon in que fail  cnt: %lld\r\n",slice_deamon_in_que_fail - slice_deamon_in_que_fail_bak);
	
	len += sprintf(buf +len ,"slice copy pte  count  : %lld\r\n",slice_copy_pte_cnt - slice_copy_pte_cnt_bak);
	len += sprintf(buf +len ,"slice copy pte1  count  : %lld\r\n",slice_copy_pte_cnt1- slice_copy_pte_cnt1_bak);
	
	return len;

}

PONE_ATTR_RO(pone_stat_end);

static struct attribute *pone_attrs[] = {
		&pone_run_attr.attr,
		&pone_info_attr.attr,
		&pone_debug_attr.attr,
		&pone_sd_tree_attr.attr,
		&pone_deamon_scan_period_attr.attr,
		&pone_divide_thread_enable_attr.attr,
		&pone_recycle_run_attr.attr,
		&pone_stat_begin_attr.attr,
		&pone_stat_end_attr.attr,
		NULL,
};


static struct attribute_group pone_attr_group = {
		.attrs = pone_attrs,
			.name = "pone",
};

void pone_sysfs_init(void)
{
	int err;
	err = sysfs_create_group(mm_kobj, &pone_attr_group);
	if (err) {
			
			printk("sysfs pone create err\r\n");
	
		}
}

#endif

