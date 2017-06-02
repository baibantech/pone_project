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
#include "vector.h"
#include "chunk.h"

extern unsigned long long process_que_num;
extern unsigned long slice_watch_que_debug;
extern unsigned long slice_que_debug;

extern unsigned long long slice_alloc_num ;
extern unsigned long long slice_change_volatile_ok ;
extern unsigned long long slice_in_que_ok;
extern unsigned long long slice_in_que_err;

extern unsigned long long slice_out_que_num;
extern unsigned long long slice_protect_err;
extern unsigned long long make_slice_protect_err_null;
extern unsigned long long make_slice_protect_err_nw;
extern unsigned long long make_slice_protect_err_map;
extern unsigned long long make_slice_protect_err_lock;
extern unsigned long long make_slice_protect_err_mapcnt;
extern unsigned long long rmap_get_anon_vma_err;
extern unsigned long long rmap_lock_num_err;
extern unsigned long long rmap_pte_null_err;
extern unsigned long long pte_err1;
extern unsigned long long pte_err2;
extern unsigned long long pte_err3;
extern unsigned long long pte_err4;
extern unsigned long long pte_err5;

extern unsigned long long slice_in_watch_que_ok;
extern unsigned long long slice_in_watch_que_err;
extern unsigned long long slice_mem_que_free;

extern unsigned long long slice_map_cnt_err;
extern unsigned long long slice_insert_sd_tree_err;
extern unsigned long long slice_new_insert_num ;
extern unsigned long long slice_change_ref_err;
extern unsigned long long slice_merge_num ;
extern unsigned long long slice_mem_watch_free;

extern unsigned long long slice_fix_free_num;
extern unsigned long long slice_volatile_free_num;
extern unsigned long long slice_other_free_num;
extern unsigned long long slice_sys_free_num;

extern unsigned long long slice_mem_watch_change ;
extern unsigned long long slice_mem_fix_change;

extern unsigned long long slice_volatile_in_que_ok;
extern unsigned long long slice_volatile_in_que_err;
extern unsigned long long slice_deamon_find_volatile;

extern struct task_struct *spt_deamon_thread;
extern int debug_statistic(cluster_head_t *head);
#if 0
extern unsigned long long slice_file_cow;
extern unsigned long long slice_file_watch_chg;
extern unsigned long long slice_file_fix_chg;
extern unsigned long long slice_file_chgref_num;
#endif

extern struct pone_hash_head *pone_hash_table;
extern unsigned long long page_free_cnt;
extern unsigned long long data_map_key_cnt;
extern unsigned long long data_unmap_key_cnt;
extern unsigned long long delete_sd_tree_ok ;
extern unsigned long long insert_sd_tree_ok ;
extern unsigned long long slice_pre_fix_check_cnt ;

extern unsigned long long virt_page_release_merge_ok;
extern unsigned long long virt_page_release_merge_err;

extern unsigned long long virt_mem_page_lock_err;
extern unsigned long long virt_mem_page_count_err;
extern unsigned long long virt_mem_page_state_conflict;

extern unsigned long long data_cmp_cnt;
extern unsigned long long data_cmp_err;
extern unsigned long long data_cmp_ptr_null;

extern void* page_insert;
extern char page_in_tree[];

#if 0
extern unsigned long long page_anon_num;
extern unsigned long long page_read_fault_num;
extern unsigned long long page_do_fault_num;
extern unsigned long long page_pte_fault_num;
extern unsigned long long page_pte_none_num;
#endif
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
	//len += sprintf(buf +len ,"slice que process count is %lld\r\n",process_que_num);
	//len += sprintf(buf +len ,"page anon  count is %lld\r\n",page_anon_num);
	//len += sprintf(buf +len ,"page read fault count is %lld\r\n",page_read_fault_num);
	//len += sprintf(buf +len ,"page do fault count is %lld\r\n",page_do_fault_num);
	//len += sprintf(buf +len ,"page pte fault count is %lld\r\n",page_pte_fault_num);
	//len += sprintf(buf +len ,"page pte nono  count is %lld\r\n",page_pte_none_num);
	
	len += sprintf(buf +len ,"slice alloc cnt: %lld\r\n",slice_alloc_num);
	len += sprintf(buf +len ,"slice in que ok cnt: %lld\r\n",slice_in_que_ok);
	len += sprintf(buf +len ,"slice change volatile ok cnt: %lld\r\n",slice_change_volatile_ok);
	len += sprintf(buf +len ,"slice in que err cnt: %lld\r\n",slice_in_que_err);

	len += sprintf(buf +len ,"slice out que cnt: %lld\r\n",slice_out_que_num);
	len += sprintf(buf +len ,"slice protect err cnt: %lld\r\n",slice_protect_err);
	
	len += sprintf(buf +len ,"slice protect err null cnt: %lld\r\n",make_slice_protect_err_null);
	len += sprintf(buf +len ,"slice protect err nw cnt: %lld\r\n",make_slice_protect_err_nw);
	len += sprintf(buf +len ,"slice protect err map cnt: %lld\r\n",make_slice_protect_err_map);
	len += sprintf(buf +len ,"slice protect err lock cnt: %lld\r\n",make_slice_protect_err_lock);
	len += sprintf(buf +len ,"slice protect err mapcnt cnt: %lld\r\n",make_slice_protect_err_mapcnt);
	
	len += sprintf(buf +len ,"rmap get anon vma  err  cnt: %lld\r\n",rmap_get_anon_vma_err);
	len += sprintf(buf +len ,"rmap lock num err  cnt: %lld\r\n",rmap_lock_num_err);
	len += sprintf(buf +len ,"rmap pte null err  cnt: %lld\r\n",rmap_pte_null_err);
	
	len += sprintf(buf +len ,"pte err1 cnt: %lld\r\n",pte_err1);
	len += sprintf(buf +len ,"pte err2 cnt: %lld\r\n",pte_err2);
	len += sprintf(buf +len ,"pte err3 cnt: %lld\r\n",pte_err3);
	len += sprintf(buf +len ,"pte err4 cnt: %lld\r\n",pte_err4);
	len += sprintf(buf +len ,"pte err5 cnt: %lld\r\n",pte_err5);

	len += sprintf(buf +len ,"slice in watch que ok  cnt: %lld\r\n",slice_in_watch_que_ok);
	len += sprintf(buf +len ,"slice in watch que err cnt: %lld\r\n",slice_in_watch_que_err);
	len += sprintf(buf +len ,"slice mem  que free cnt: %lld\r\n",slice_mem_que_free);
	
	
	len += sprintf(buf +len ,"slice new insert cnt: %lld\r\n",slice_new_insert_num);
	len += sprintf(buf +len ,"slice mapcount err cnt: %lld\r\n",slice_map_cnt_err);
	len += sprintf(buf +len ,"slice insert sd tree err cnt: %lld\r\n",slice_insert_sd_tree_err);
	len += sprintf(buf +len ,"slice change ref err  cnt: %lld\r\n",slice_change_ref_err);
	len += sprintf(buf +len ,"slice merge cnt: %lld\r\n",slice_merge_num);
	len += sprintf(buf +len ,"slice mem watch free cnt: %lld\r\n",slice_mem_watch_free);
	
	
	
	len += sprintf(buf +len ,"slice fix free cnt: %lld\r\n",slice_fix_free_num);
	len += sprintf(buf +len ,"slice volatile free cnt: %lld\r\n",slice_volatile_free_num);
	len += sprintf(buf +len ,"slice other free cnt: %lld\r\n",slice_other_free_num);
	len += sprintf(buf +len ,"slice sys free cnt: %lld\r\n",slice_sys_free_num);




	len += sprintf(buf +len ,"slice mem watch chg cnt: %lld\r\n",slice_mem_watch_change);
	len += sprintf(buf +len ,"slice mem fix chg cnt: %lld\r\n",slice_mem_fix_change);
	
#if 0
	len += sprintf(buf +len ,"slice file cow  is %lld\r\n",slice_file_cow);
	len += sprintf(buf +len ,"slice file watch chg is %lld\r\n",slice_file_watch_chg);
	len += sprintf(buf +len ,"slice file fix chg is %lld\r\n",slice_file_fix_chg);
	len += sprintf(buf +len ,"slice file chg ref is %lld\r\n",slice_file_chgref_num);
#endif

	len += sprintf(buf +len ,"data map key  cnt: %lld\r\n",data_map_key_cnt);
	len += sprintf(buf +len ,"data unmap key cnt: %lld\r\n",data_unmap_key_cnt);
	len += sprintf(buf +len ,"delete sd tree ok cnt: %lld\r\n",delete_sd_tree_ok);
	len += sprintf(buf +len ,"insert sd tree ok cnt: %lld\r\n",insert_sd_tree_ok);
	len += sprintf(buf +len ,"slice pre fix check cnt: %lld\r\n",slice_pre_fix_check_cnt);

	len += sprintf(buf +len ,"slice volatile in que  cnt: %lld\r\n",slice_volatile_in_que_ok);
	len += sprintf(buf +len ,"slice volatile in que err  cnt: %lld\r\n",slice_volatile_in_que_err);
	len += sprintf(buf +len ,"slice deamon find volatile  cnt: %lld\r\n",slice_deamon_find_volatile);
	len += sprintf(buf +len ,"deamon task state: %d\r\n",spt_deamon_thread->state);
	//len += sprintf(buf +len ,"que len: %d\r\n",lfrwq_len(slice_que));
	//len += sprintf(buf +len ,"watch que len: %d\r\n",lfrwq_len(slice_watch_que));
	len += sprintf(buf +len ,"deamon que len: %d\r\n",lfrwq_len(slice_deamon_que));
	len += sprintf(buf +len ,"virt page merge count : %d\r\n",virt_page_release_merge_ok);
	len += sprintf(buf +len ,"virt page merge count err : %d\r\n",virt_page_release_merge_err);
	
	len += sprintf(buf +len ,"virt page lock err : %d\r\n",virt_mem_page_lock_err);
	len += sprintf(buf +len ,"virt page state  err : %d\r\n",virt_mem_page_state_conflict);
	len += sprintf(buf +len ,"virt page count   err : %d\r\n",virt_mem_page_count_err);
	
#if 0
	len += sprintf(buf +len , "data cmp count : %d\r\n",data_cmp_cnt);
	len += sprintf(buf +len ,"data cmp err count : %d\r\n",data_cmp_err);
	len += sprintf(buf +len ,"data_cmp ptr null  count : %d\r\n",data_cmp_ptr_null);
#endif

	len += sprintf(buf +len ,"tree page free  cnt: %lld\r\n",page_free_cnt);

	len += slice_file_info_get(buf+len);
	return len;

}


PONE_ATTR_RO(pone_info);
unsigned long pone_file_watch = 0xFFFFFFFFFFFFFFFF;

#if 0
static ssize_t pone_debug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	unsigned long show_tmp = (slice_watch_que_debug <<8) + slice_que_debug;
	return sprintf(buf,"%llx",show_tmp);

}

static ssize_t pone_debug_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf,size_t count)
{
	int err;
	unsigned long store_tmp = 0;	
	err =  kstrtoul(buf,16,&store_tmp);
	printk("store_tmp is 0x%llx\r\n",store_tmp);
	slice_que_debug = store_tmp & 255;
	slice_watch_que_debug = store_tmp >> 8;
	return count;
}
#endif
extern void debug_mm_pte(void);

static ssize_t pone_debug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	debug_mm_pte();	
	return sprintf(buf,"%ld",pone_file_watch);

}

static ssize_t pone_debug_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf,size_t count)
{
	int err;
	unsigned long store_tmp = 0;	
	err =  kstrtoul(buf,10,&store_tmp);
	printk("store_tmp is %ld\r\n",store_tmp);
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

	return count;
}
PONE_ATTR(pone_debug);

extern int slice_debug_area_show(void);
extern void print_host_virt_mem_pool(void);
extern void walk_guest_mem_pool(void);
static ssize_t pone_sd_tree_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{	
	//slice_debug_area_show();
	print_host_virt_mem_pool();	
	debug_statistic(pgclst);
	printk_debug_map_cnt();
	walk_guest_mem_pool();
	//show_page_err_info();
	return sprintf(buf,"check dmesg buffer11111");
}


PONE_ATTR_RO(pone_sd_tree);

static struct attribute *pone_attrs[] = {
		&pone_info_attr.attr,
		&pone_debug_attr.attr,
		&pone_sd_tree_attr.attr,
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

