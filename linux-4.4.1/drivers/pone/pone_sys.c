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


extern unsigned long long process_que_num;
extern unsigned long slice_watch_que_debug;
extern unsigned long slice_que_debug;
extern unsigned long long slice_alloc_num ;
extern unsigned long long slice_in_que_ok;
extern unsigned long long slice_in_que_err;
extern unsigned long long slice_protect_err;
extern unsigned long long slice_in_watch_que_ok;
extern unsigned long long slice_in_watch_que_err;
extern unsigned long long slice_new_insert_num ;
extern unsigned long long slice_change_ref_err;
extern unsigned long long slice_merge_num ;
extern unsigned long long slice_free_num ;
extern unsigned long long slice_mem_watch_change ;
extern unsigned long long slice_mem_fix_change;
extern unsigned long long slice_file_cow;
extern unsigned long long slice_file_watch_chg;
extern unsigned long long slice_file_fix_chg;
extern unsigned long long slice_file_chgref_num;
extern struct pone_hash_head *pone_hash_table;

#ifdef CONFIG_SYSFS

#define PONE_ATTR_RO(_name) \
		static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define PONE_ATTR(_name) \
		static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)


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
	len += sprintf(buf +len ,"slice que process count is %lld\r\n",process_que_num);
	
	len += sprintf(buf +len ,"slice alloc count is %lld\r\n",slice_alloc_num);
	len += sprintf(buf +len ,"slice in que ok  count is %lld\r\n",slice_in_que_ok);
	len += sprintf(buf +len ,"slice in que err count is %lld\r\n",slice_in_que_err);
	len += sprintf(buf +len ,"slice protect err  count is %lld\r\n",slice_protect_err);
	len += sprintf(buf +len ,"slice in watch que ok  count is %lld\r\n",slice_in_watch_que_ok);
	len += sprintf(buf +len ,"slice in watch que err count is %lld\r\n",slice_in_watch_que_err);
	len += sprintf(buf +len ,"slice new insert count is %lld\r\n",slice_new_insert_num);
	len += sprintf(buf +len ,"slice change ref err  count is %lld\r\n",slice_change_ref_err);
	len += sprintf(buf +len ,"slice merge count is %lld\r\n",slice_merge_num);
	len += sprintf(buf +len ,"slice free count is %lld\r\n",slice_free_num);
	len += sprintf(buf +len ,"slice mem watch chg is %lld\r\n",slice_mem_watch_change);
	len += sprintf(buf +len ,"slice mem fix chg is %lld\r\n",slice_mem_fix_change);
	len += sprintf(buf +len ,"slice file cow  is %lld\r\n",slice_file_cow);
	len += sprintf(buf +len ,"slice file watch chg is %lld\r\n",slice_file_watch_chg);
	len += sprintf(buf +len ,"slice file fix chg is %lld\r\n",slice_file_fix_chg);
	len += sprintf(buf +len ,"slice file chg ref is %lld\r\n",slice_file_chgref_num);
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

static ssize_t pone_debug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,"%ld",pone_file_watch);

}

static ssize_t pone_debug_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf,size_t count)
{
	int err;
	unsigned long store_tmp = 0;	
	err =  kstrtoul(buf,10,&store_tmp);
	printk("store_tmp is %ld\r\n",store_tmp);
	pone_file_watch = store_tmp;
	return count;
}
PONE_ATTR(pone_debug);


static ssize_t pone_merge_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return  pone_hash_table_show(buf);
}

PONE_ATTR_RO(pone_merge);

static struct attribute *pone_attrs[] = {
		&pone_info_attr.attr,
		&pone_debug_attr.attr,
		&pone_merge_attr.attr,
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

