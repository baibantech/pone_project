#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/rmap.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/migrate.h>
#include <linux/hugetlb.h>
#include <linux/backing-dev.h>

#include <asm/tlbflush.h>
#include <linux/mmdebug.h>
#include <pone/slice_state.h>
#include <pone/slice_state_adpter.h>
#include <pone/pone.h>
#include <pone/lf_rwq.h>
#include "vector.h"
#include "chunk.h"
#include "splitter_adp.h"
struct timer_list slice_merge_timer = {0};
extern unsigned long long slice_file_watch_chg;
extern unsigned long long slice_file_fix_chg;
extern unsigned long long slice_file_cow;
struct address_space *check_space = NULL;

int collect_sys_slice_info(slice_state_control_block *cblock)
{
    int nid ;
    int max_node = 0 ;
    printk("enter information collect\r\n");
    
    if(!cblock)
    {
        return -1;
    }
    memset(cblock,0, PAGE_SIZE);

    for_each_online_node(nid) {

        printk("node id is %d\r\n",NODE_DATA(nid)->node_id);
        printk("node start pfn is %lld",NODE_DATA(nid)->node_start_pfn);
        printk("node spanned pfn is %lld\r\n",NODE_DATA(nid)->node_spanned_pages);
        
        if(max_node < nid)
        {
            max_node = nid;
            if(max_node >= SLICE_NODE_NUM_MAX)
            {
                kfree(cblock);
                return -1 ;
            }
        } 
        cblock->slice_node[nid].slice_start = NODE_DATA(nid)->node_start_pfn;  
        cblock->slice_node[nid].slice_num  = NODE_DATA(nid)->node_spanned_pages;        
    }
    
    cblock->node_num = max_node +1 ;
    
    printk("max node num is %d\r\n",max_node + 1);
    return 0;
}

extern unsigned long long slice_file_protect_num;
unsigned long long make_slice_protect_err_null = 0;
unsigned long long make_slice_protect_err_nw = 0;
unsigned long long make_slice_protect_err_map = 0;
unsigned long long make_slice_protect_err_lock = 0;

int make_slice_wprotect_one(struct page *page, struct vm_area_struct *vma,
                    unsigned long addr, void *arg)
{
    pte_t *ptep;

    if(PageAnon(page))
	{
		struct mm_struct *mm = vma->vm_mm;
 
		ptep =  __page_get_pte_address(page, mm, addr);
		if (!ptep)
		{
			atomic64_add(1,(atomic64_t*)&make_slice_protect_err_null);
			return -1;
		}
		
		if (pte_write(*ptep) || pte_dirty(*ptep)) {
			pte_t entry;
	
			unsigned long mmun_start;	/* For mmu_notifiers */
			unsigned long mmun_end;		/* For mmu_notifiers */

			mmun_start = addr;
			mmun_end   = addr + PAGE_SIZE;
			mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);
			
			
			
			flush_cache_page(vma, addr, page_to_pfn(page));
			entry = ptep_clear_flush(vma, addr, ptep);

			if (pte_dirty(entry))
			set_page_dirty(page);
        
			entry = pte_mkclean(pte_wrprotect(entry));
			set_pte_at_notify(mm, addr, ptep, entry);
			
			mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
		}
		else
		{
			atomic64_add(1,(atomic64_t*)&make_slice_protect_err_nw);
			return -1;
		}
		
		//printk("slice proc name %s\r\n",mm->owner->comm);	
	}
	else
	{
		atomic64_add(1,(atomic64_t*)&slice_file_protect_num);	
	}
	return 0;
}
int make_slice_wprotect(unsigned long slice_idx)
{
    struct page *page = pfn_to_page(slice_idx);
    int ret = SLICE_ERR;

    struct rmap_walk_control rwc = {
        .rmap_one = make_slice_wprotect_one,
        .anon_lock = page_try_lock_anon_vma_read,
    };
    
    if (!page_rmapping(page))/*map_count*/
    {
		atomic64_add(1,(atomic64_t*)&make_slice_protect_err_map);
        return SLICE_STATUS_ERR;
    }

    if (!trylock_page(page))
    {
		atomic64_add(1,(atomic64_t*)&make_slice_protect_err_lock);
        return SLICE_LOCK_ERR;
    }

    ret = rmap_walk_pone(page, &rwc);
    
    unlock_page(page);

	if(0 != ret)
	{
		return SLICE_STATUS_ERR;
	}
    
    return SLICE_OK;
}

int free_slice(unsigned long slice_idx)
{
    struct page* page = pfn_to_page(slice_idx);
    if(0 != atomic_read(&page->_count))
	{
		printk("page count is err is free_slice count is %d\r\n",atomic_read(&page->_count));
	}
	free_hot_cold_page(page,0);
	return 0;
}

#if 0
int delete_fix_slice(unsigned long slice_idx)
{
    char *addr;
    int ret = -1;

    struct page *page = pfn_to_page(slice_idx);

    addr = kmap_atomic(page);
    ret = pone_delete(addr, PAGE_SIZE,slice_idx);
    kunmap_atomic(addr); 
	
	return ret;
}
#endif
int inc_reverse_count(unsigned long slice_idx)
{
    struct page *page = pfn_to_page(slice_idx);
    if(PageAnon(page))
    {
        struct anon_vma *anon_vma = page_get_anon_vma(page);
        if(anon_vma)
        {
            return SLICE_OK;
        }
    }
    return SLICE_ERR;
}

#if 0
int get_reverse_ref_one(struct page *page, struct vm_area_struct *vma,
                    unsigned long addr, void *arg)
{
    struct list_head *head = *arg;

    ref = kmalloc(sizeof(slice_reflect),GFP_KERNEL);
    if(NULL == ref)
    {
        return NULL;
    }
    memset(ref,0,sizeof(slice_reflect));
    INIT_LIST_HEAD(&ref->next);
    ref->type = SLICE_MEM;

    ptep =  page_check_address(page, mm, address, &ptl, 0);
    if (!ptep)
    {
        kfree(ref);
        return -1;
    }
    ref->ref_info = vma;

    if(NULL == head)
    {
        *arg = &ref->next;
    }
    else
    {
        list_add(&ref->next,head);
    }

    pte_unmap_unlock(ptep, ptl);

    return 0;
}
#endif

extern unsigned long long slice_file_chgref_num;
int change_reverse_ref_one(struct page *page, struct vm_area_struct *vma,
                    unsigned long addr, void *arg)
{
    pte_t *ptep;
    struct page *new_page = arg;
    if(PageAnon(page))
	{
		struct mm_struct *mm = vma->vm_mm;

		ptep =  __page_get_pte_address(page, mm, addr);
		if (!ptep)
		{
			return -1;
		}

		if(pte_write(*ptep))
		{
			return -1;
		}     
    
		unsigned long mmun_start;	/* For mmu_notifiers */
		unsigned long mmun_end;		/* For mmu_notifiers */

		mmun_start = addr;
		mmun_end   = addr + PAGE_SIZE;
		mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);
		
		get_page(new_page);
		page_add_anon_rmap(new_page, vma, addr);

		flush_cache_page(vma, addr, pte_pfn(*ptep));
		ptep_clear_flush(vma, addr, ptep);
		set_pte_at_notify(mm, addr, ptep, pte_wrprotect(mk_pte(new_page, vma->vm_page_prot)));

		page_remove_rmap(page);
    
		if (!page_mapped(page))
			try_to_free_swap(page);
		put_page(page);

		mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
	}
	else
	{
		struct address_space *space = page->mapping;
		if(check_space == NULL)
			check_space = space;
		printk("change ref old slice %ld,new slice %ld\r\n",page_to_pfn(page),page_to_pfn(new_page));
		printk("old page index %ld\r\n",page->index);
		if(0 == replace_pone_page_cache(page,page->index,space,new_page,GFP_KERNEL,1)){	
			atomic64_add(1,(atomic64_t*)&slice_file_chgref_num);
			page->mapping = NULL;
			printk("old page count is %d\r\n",*(int*)&page->_count);
		}else {
			return -1;
		}
	}

    return 0;
}

int  change_reverse_ref(unsigned long slice_idx,unsigned long new_slice)
{
    struct page *page = pfn_to_page(slice_idx);
    struct page *new_page = pfn_to_page(new_slice);
    int ret = SLICE_ERR;

    struct rmap_walk_control rwc = {
        .rmap_one =  change_reverse_ref_one,
        .arg = (void*)new_page,
        .anon_lock = page_try_lock_anon_vma_read,
    };

    if (!page_rmapping(page))/*map_count*/
    {
        return SLICE_STATUS_ERR;;
    }

    if (!trylock_page(page))
    {
        return SLICE_LOCK_ERR;
    }

    ret = rmap_walk_pone(page, &rwc);
   
    unlock_page(page);
	
	if(0 != ret)
	{
		return SLICE_STATUS_ERR;
	}
    return SLICE_OK;
}
#if 0    
struct pone_desc* insert_sd_tree(unsigned long slice_idx)
{
    char *addr;
    struct pone_desc  *ret;

    struct page *page = pfn_to_page(slice_idx);

    addr = kmap_atomic(page);
    ret = pone_insert(addr, PAGE_SIZE,slice_idx);
    kunmap_atomic(addr); 

    return ret;
}
#endif
static void slice_merge_timer_process(unsigned long data)
{
	int ret = 0;
#ifdef SLICE_OP_CLUSTER_QUE
	splitter_wakeup_cluster();
#else
	ret += lfrwq_set_r_max_idx(slice_que,lfrwq_get_w_idx(slice_que));

	ret += lfrwq_set_r_max_idx(slice_watch_que,lfrwq_get_w_idx(slice_watch_que));
	
	ret += lfrwq_set_r_max_idx(slice_deamon_que,lfrwq_get_w_idx(slice_deamon_que));
	
	if(ret)
	{
		splitter_thread_wakeup();
	}
#endif
	if(need_wakeup_deamon())
	{
		splitter_deamon_wakeup();
	}

	slice_merge_timer.data = data;
    slice_merge_timer.expires = jiffies + msecs_to_jiffies(data);
    add_timer(&slice_merge_timer);
}

void slice_merge_timer_init(unsigned long time_ms)
{
    init_timer(&slice_merge_timer);
    slice_merge_timer.function = slice_merge_timer_process;
    slice_merge_timer.data = time_ms;
    slice_merge_timer.expires = jiffies + msecs_to_jiffies(time_ms);
    add_timer(&slice_merge_timer);
    return;
}

DEFINE_PER_CPU(lfrwq_reader , int_slice_que_reader);
DEFINE_PER_CPU(lfrwq_reader , int_slice_watch_que_reader);
DEFINE_PER_CPU(lfrwq_reader , int_slice_deamon_que_reader);
DEFINE_PER_CPU(int,process_enter_check);
DEFINE_PER_CPU(int,in_que_cnt);
DEFINE_PER_CPU(int,in_watch_que_cnt);
DEFINE_PER_CPU(int,volatile_cnt);
unsigned long long process_que_num = 0;
void slice_que_reader_init(void)
{
	int cpu ;
	lfrwq_reader *reader = NULL;
	for_each_online_cpu(cpu)
	{
		reader = &per_cpu(int_slice_que_reader,cpu);
		memset(reader,0,sizeof(lfrwq_reader));
		reader->local_idx = -1;
	}
	for_each_online_cpu(cpu)
	{
		reader = &per_cpu(int_slice_watch_que_reader,cpu);
		memset(reader,0,sizeof(lfrwq_reader));
		reader->local_idx = -1;
	}
	
	return ;
}
void slice_per_cpu_count_init(void)
{
	int cpu ;
	int *check = NULL;
	
	for_each_online_cpu(cpu)
	{
		check  = &per_cpu(process_enter_check,cpu);
		*check = 0;
	}
	for_each_online_cpu(cpu)
	{
		per_cpu(local_thrd_id,cpu) = cpu;
		
	}
	for_each_online_cpu(cpu)
	{
		per_cpu(in_que_cnt,cpu) = 0;
	}	
	for_each_online_cpu(cpu)
	{
		per_cpu(in_watch_que_cnt,cpu) = 0;
	
	}
	for_each_online_cpu(cpu)
	{
		per_cpu(volatile_cnt,cpu) = 0;
	}
}


void process_que_interrupt(void)
{
	int cpu = smp_processor_id();
	int check  = per_cpu(process_enter_check,cpu);
	lfrwq_reader *reader = NULL;
	if(check)
	{
		return;
	}
	
	process_que_num++;
	if(cpu%2)
	{
		reader = &per_cpu(int_slice_watch_que_reader,cpu);
		process_state_que(slice_watch_que,reader,2);
		reader = &per_cpu(int_slice_que_reader,cpu);
		process_state_que(slice_que,reader,1);
	}
	else
	{
		reader = &per_cpu(int_slice_que_reader,cpu);
		process_state_que(slice_que,reader,1);
		reader = &per_cpu(int_slice_watch_que_reader,cpu);
		process_state_que(slice_watch_que,reader,2);
	}
	return;
}

#if 1

int  slice_file_write_proc(struct address_space *space,unsigned long offset)
{
	unsigned long long cur_state;
	unsigned int nid;
	unsigned long long slice_id;
	unsigned long long slice_idx;
	struct page *page = NULL;
	struct page *new_page = NULL;
	/*before this function page is locked,so make_slice_wprotect for this page must trylock*/
	int fgp_flags = FGP_LOCK;


	if(!is_pone_init())
	{
		return 0;
	}
	
	page = pagecache_get_page(space,offset,fgp_flags,mapping_gfp_mask(space));
	if(page)
	{
		wait_for_stable_page(page);
	}
	else
	{
		return 0;
	}
	
	slice_idx = page_to_pfn(page);

	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	do
	{
		barrier();
		cur_state = get_slice_state(nid,slice_id);
		if(SLICE_VOLATILE == cur_state ||SLICE_IDLE == cur_state || SLICE_ENQUE == cur_state)
		{
			unlock_page(page);
			page_cache_release(page);
			break;	
		}
		else if(SLICE_WATCH == cur_state)
		{
			if(0 == change_slice_state(nid,slice_id,SLICE_WATCH,SLICE_WATCH_CHG))
			{
				atomic64_add(1,(atomic64_t*)&slice_file_watch_chg);
				unlock_page(page);
				page_cache_release(page);
				break;
			}
		}
		else if(SLICE_FIX == cur_state)
		{
			printk("offset is %ld\r\n",offset);
			printk("map_count is %d\r\n",*(int*)&page->_mapcount);
			printk("page_count is %d\r\n",*(int*)&page->_count);

			if(0 == atomic_add_unless(&page->_mapcount,-1,-1))/*no more map use this page*/{
				/*this page rwrite by linux sys*/
				if(0 == change_slice_state(nid,slice_id,SLICE_FIX,SLICE_VOLATILE))
				{
					atomic64_add(1,(atomic64_t*)&slice_file_fix_chg);
					delete_sd_tree(slice_idx,0xff);
					page->index = offset;
					page->mapping = space;
					unlock_page(page);
					page_cache_release(page);
					printk("page_count is %d\r\n",*(int*)&page->_count);
					return 0;
				}
				else
				{
					continue;
				}
			}
			else { 	
				printk("write proc alloc new_page\r\n");

				new_page = page_cache_alloc(space);
				if(new_page)
				{
					__set_page_locked(new_page);
					if(0 == replace_pone_page_cache(page,offset,space,new_page,GFP_KERNEL,0))/*spinlock loop*/
					{
	//					
	//					put_page(page);
						printk("page_count is %d\r\n",*(int*)&page->_count);
						new_page->mapping = space;
						new_page->index = offset;
						atomic64_add(1,(atomic64_t*)&slice_file_cow);
						copy_highpage(new_page,page);
						unlock_page(page);
						unlock_page(new_page);
						page_cache_release(page);
					}
					else
					{
						unlock_page(page);
						unlock_page(new_page);
						page_cache_release(page);
						put_page(new_page);
					}
				}
				else
				{
					unlock_page(page);
					page_cache_release(page);
				}
				
				break;
			}
		}
		
	}while(1);
	
	return 0;
}

struct page* slice_file_replace_proc(struct address_space *mapping,unsigned long index,struct page *page)
{
	unsigned long long cur_state;
	unsigned int nid;
	unsigned long long slice_id;
	unsigned long long slice_idx;
	struct page *new_page = NULL;
	if(!is_pone_init())
	{
		return page;
	}
	if(!PageLocked(page))
	{
		printk("page no locked \r\n");
		return page;
	}
	
	slice_idx = page_to_pfn(page);

	nid = slice_idx_to_node(slice_idx);
	slice_id = slice_nr_in_node(nid,slice_idx);
	do
	{
		barrier();
		cur_state = get_slice_state(nid,slice_id);
		if(SLICE_VOLATILE == cur_state ||SLICE_IDLE == cur_state || SLICE_ENQUE == cur_state)
		{
			return page;	
		}
		else if(SLICE_WATCH == cur_state)
		{
			if(0 == change_slice_state(nid,slice_id,SLICE_WATCH,SLICE_WATCH_CHG))
			{
				atomic64_add(1,(atomic64_t*)&slice_file_watch_chg);
				return page;
			}
		}
		else if(SLICE_FIX == cur_state)
		{
			printk("offset is %ld\r\n",index);
			printk("map_count is %d\r\n",*(int*)&page->_mapcount);
			printk("page_count is %d\r\n",*(int*)&page->_count);

			if(0 == atomic_add_unless(&page->_mapcount,-1,-1))/*no more map use this page*/{
				/*this page rwrite by linux sys*/
				if(0 == change_slice_state(nid,slice_id,SLICE_FIX,SLICE_VOLATILE))
				{
					atomic64_add(1,(atomic64_t*)&slice_file_fix_chg);
					delete_sd_tree(slice_idx,0xEE);
					page->index = index;
					page->mapping = mapping;
					printk("page_count is %d\r\n",*(int*)&page->_count);
					return page;
				}
				else
				{
					continue;
				}
			}
			else { 	
				printk("write proc alloc new_page\r\n");

				new_page = page_cache_alloc(mapping);
				if(new_page)
				{
					__set_page_locked(new_page);
					if(0 == replace_pone_page_cache(page,index,mapping,new_page,GFP_KERNEL,0))/*spinlock loop*/
					{
						printk("page_count is %d\r\n",*(int*)&page->_count);
						new_page->mapping = mapping;
						new_page->index = index;
						atomic64_add(1,(atomic64_t*)&slice_file_cow);
						copy_highpage(new_page,page);
						unlock_page(page);
						page_cache_release(page);
						page_cache_get(new_page);
						return new_page;
					}
					else
					{
						unlock_page(new_page);
						put_page(new_page);
					}
				}
				else
				{
				}
				
				break;
			}
		}
		
	}while(1);
	
	return NULL;
}



#endif
