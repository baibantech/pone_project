/*************************************************************************
	> File Name: virt_release.c
	> Author: lijiyong
	> Mail: lijiyong0303@163.com 
	> Created Time: Wed 10 May 2017 03:57:38 AM EDT
 ************************************************************************/

#include<linux/kernel.h>
#include <linux/highmem.h>
#include <linux/rmap.h>
#include <asm/tlbflush.h>
#include <pone/slice_state.h>
#include <pone/virt_release.h>

extern unsigned long long get_slice_state_by_id(unsigned long slice_idx);
extern pmd_t *mm_find_pmd(struct mm_struct *mm,unsigned long long address);
char* release_dsc = "page can release xxx";
struct page *release_merge_page = NULL;


EXPORT_SYMBOL(release_merge_page);

struct virt_mem_pool *guest_mem_pool = NULL;

EXPORT_SYMBOL(guest_mem_pool);


int guest_page_clear_ok = 0;
EXPORT_SYMBOL(guest_page_clear_ok);

int guest_page_no_need_clear = 0; 
EXPORT_SYMBOL(guest_page_no_need_clear);


struct virt_mem_pool *mem_pool_addr[MEM_POOL_MAX] = {0};
unsigned long mark_release_count= 0;
unsigned long mark_alloc_count = 0;
int is_virt_mem_pool_page(struct mm_struct *mm, unsigned long address)
{
	int i = 0;
	for(i = 0; i < MEM_POOL_MAX; i++)
	{
		if(mem_pool_addr[i] != NULL)
		{
			unsigned long hva = mem_pool_addr[i]->hva;
			struct mm_struct *pool_mm = mem_pool_addr[i]->args.mm;
			if(mm == pool_mm)
			{
				if(address >= hva)
				{
					if(address < (hva+ 0x10000000))
					{
						return 1;
					}
				}
			}
		}
	}
	return 0;

}

int virt_mark_page_release(struct page *page)
{
	int pool_id ;
	unsigned long long alloc_id;
	unsigned long long state;
	unsigned long long idx ;
	struct virt_release_mark *mark ;
	if(!guest_mem_pool)
	{
#ifdef GUEST_KERNEL
		clear_highpage(page);
		set_guest_page_clear_ok();
#endif
		return -1;
	}
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_r_begin);
	pool_id = guest_mem_pool->pool_id;
	alloc_id = atomic64_add_return(1,(atomic64_t*)&guest_mem_pool->alloc_idx)-1;
	idx = alloc_id%guest_mem_pool->desc_max;
	state = guest_mem_pool->desc[idx];
	
	mark =kmap_atomic(page);
	if(0 == state)
	{
		if(0 != atomic64_cmpxchg((atomic64_t*)&guest_mem_pool->desc[idx],0,page_to_pfn(page)))
		{
			pool_id = MEM_POOL_MAX +1;
			idx = guest_mem_pool->desc_max +1;
			atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_release_err_conflict);
			
		}
		else
		{
			atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_release_ok);
		}
		mark->pool_id = pool_id;
		mark->alloc_id = idx;
		barrier();
		strcpy(mark->desc,guest_mem_pool->mem_ind);
		barrier();
		kunmap_atomic(mark);

		atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_r_end);
		return 0;
	}
	else
	{
		mark->pool_id = MEM_POOL_MAX +1;
		mark->alloc_id = guest_mem_pool->desc_max +1;
		barrier();
		strcpy(mark->desc,guest_mem_pool->mem_ind);
		barrier();
		kunmap_atomic(mark);
	}
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_release_err_state);
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_r_end);
	return -1;
}

EXPORT_SYMBOL(virt_mark_page_release);
int virt_mark_page_alloc(struct page *page)
{
	int pool_id ;
	unsigned long long alloc_id;
	unsigned long long state;
	unsigned long long idx ;
	volatile struct virt_release_mark *mark ;
		
	if(!guest_mem_pool)
	{
		return 0;
	}

	atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_a_begin);
	mark =kmap_atomic(page);

	if(0 == strcmp(mark->desc,guest_mem_pool->mem_ind))
	{
		if(mark->pool_id == guest_mem_pool->pool_id)
		{
			if(mark->alloc_id < guest_mem_pool->desc_max)
			{
				idx = mark->alloc_id;
				state = guest_mem_pool->desc[mark->alloc_id];
				if(state == page_to_pfn(page))
				{
					if(state == atomic64_cmpxchg((atomic64_t*)&guest_mem_pool->desc[idx],state,0))
					{
						atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_alloc_ok);
						atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_a_end);
					}
					else
					{
						while(mark->desc[0] != '0')
						{
							barrier();
						}
					}
				}
			}
		}
		memset(mark,0,sizeof(struct virt_release_mark));
		barrier();
		kunmap_atomic(mark);
		return 0;
	}
	else
	{
		if(guest_page_no_need_clear)
		{
			void *page_mem = kmap_atomic(release_merge_page);
			if(0 != memcmp(mark,page_mem,PAGE_SIZE))
			{
				atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_alloc_err_conflict);
			}
			kunmap_atomic(page_mem);
		}
	}
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->mark_alloc_err_state);
	atomic64_add(1,(atomic64_t*)&guest_mem_pool->debug_a_end);
	kunmap_atomic(mark);
	return -1;
}
EXPORT_SYMBOL(virt_mark_page_alloc);

void init_guest_mem_pool(void *addr,int len)
{
	struct virt_mem_pool *pool = addr;
	memset(addr,0,len);
	pool->magic= 0xABABABABABABABAB;
	pool->pool_id = -1;
	pool->desc_max = (len - sizeof(struct virt_mem_pool))/sizeof(unsigned long long);
}

int virt_mem_guest_init(void)
{
    void __iomem *ioaddr = ioport_map(0xb000,0);
    struct page *page1 = NULL;
	unsigned long long  io_reserve_mem = 0;	
	char *ptr = NULL;

	io_reserve_mem = ioread32(ioaddr);
	printk("io reserve mem add is 0x%llx\r\n",io_reserve_mem);
	if(io_reserve_mem != 0xFFFFFFFF)
	{
		ptr = ioremap(io_reserve_mem <<12 , 0x10000000);
		printk("ptr remap addr is %p\r\n",ptr);
		if(ptr != NULL)
		{
			
			init_guest_mem_pool(ptr,0x10000000); 
			print_virt_mem_pool(ptr);
			
			iowrite32(io_reserve_mem, ioaddr);
			print_virt_mem_pool(ptr);
			if(guest_page_clear_ok)
				guest_page_no_need_clear  = 1;
			barrier();	
			guest_mem_pool = ptr;
		}
	}
    return 0;
}



int virt_mem_release_init(void)
{
	void *page_addr = NULL;
	int i = i;
	unsigned long slice_id ;
	unsigned int nid;
	if(!release_merge_page)
	{
		release_merge_page = alloc_page(GFP_KERNEL);
		if(release_merge_page)
		{
			page_addr = kmap(release_merge_page);
			memset(page_addr,0,PAGE_SIZE);
			kunmap(release_merge_page);
			nid = slice_idx_to_node(page_to_pfn(release_merge_page));
			slice_id = slice_nr_in_node(nid,page_to_pfn(release_merge_page));
			if(0 != change_slice_state(nid,slice_id,SLICE_NULL,SLICE_ENQUE))
			{
				printk("BUG in virt mem release\r\n");
				return -1;
			}

	
			for(i = 0;i<MEM_POOL_MAX; i++)
			{
				mem_pool_addr[i] = NULL;
			}

#ifdef GUEST_KERNEL
			virt_mem_guest_init();	
#endif
			return 0;
		}

	}
	
	return -1;
}
void walk_guest_mem_pool(void)
{
	int i = 0;
	void *page_addr = NULL;
	if(NULL == guest_mem_pool)
	{
		return ;
	}
	
	for(i = 0; i <guest_mem_pool->desc_max; i++)
	{
		unsigned long long gfn = guest_mem_pool->desc[i];
		if(gfn !=0)
		{
			page_addr = kmap_atomic(pfn_to_page(gfn));
			if(0 == strcmp(page_addr,guest_mem_pool->mem_ind))
			{
			
			}
			else
			{
				printk("page_addr mem is %d\r\n",*(int*)page_addr);
			}
			kunmap_atomic(page_addr);
		}
	}
}
 
void print_virt_mem_pool(struct virt_mem_pool *pool)
{
	printk("magic is 0x%llx\r\n",pool->magic);
	printk("pool_id is %d\r\n",pool->pool_id);
	printk("mem_ind  is %s\r\n",pool->mem_ind);
	printk("hva is 0x%llx\r\n",pool->hva);
	printk("args mm  is %p\r\n",pool->args.mm);
	printk("args vma  is %p\r\n",pool->args.vma);
	printk("args task  is %p\r\n",pool->args.task);
	printk("args kvm  is %p\r\n",pool->args.kvm);
	printk("alloc id is %llx\r\n",pool->alloc_idx);
	printk("desc_max is %llx\r\n",pool->desc_max);
	printk("debug r begin is %lld\r\n",pool->debug_r_begin);
	printk("debug r end is %lld\r\n",pool->debug_r_end);
	printk("debug a begin is %lld\r\n",pool->debug_a_begin);
	printk("debug a begin is %lld\r\n",pool->debug_a_end);
	printk("mark release ok  is %lld\r\n",pool->mark_release_ok);
	printk("mark release err conflict  is %lld\r\n",pool->mark_release_err_conflict);
	printk("mark release err state  is %lld\r\n",pool->mark_release_err_state);
	printk("mark alloc ok  is %lld\r\n",pool->mark_alloc_ok);
	printk("mark alloc err conflict  is %lld\r\n",pool->mark_alloc_err_conflict);
	printk("mark alloc err state  is %lld\r\n",pool->mark_alloc_err_state);
}

int walk_virt_page_release(struct virt_mem_pool *pool);
void print_host_virt_mem_pool(void)
{
	int i = 0;
	struct virt_mem_pool *pool;
	printk("print host virt mem pool info\r\n");
	for(i = 0; i < MEM_POOL_MAX; i++)
	{
		if(mem_pool_addr[i] != NULL)
		{
			unsigned long hva = mem_pool_addr[i]->hva;
			struct vm_area_struct *vma = mem_pool_addr[i]->args.vma;
			struct page *begin_page = follow_page(vma,hva,FOLL_TOUCH);

			if(NULL == begin_page)
			{
				printk("get host mem pool page err\r\n");
				return ;
			}
			
			pool =  (struct virt_mem_pool*)kmap(begin_page);
			print_virt_mem_pool(pool);
			//walk_virt_page_release(pool);
			kunmap(begin_page);
		}

	}

}


EXPORT_SYMBOL(print_virt_mem_pool);

int mem_pool_reg(unsigned long gfn,struct kvm *kvm,struct mm_struct *mm,struct task_struct *task)
{
	unsigned long hva = 0;
	struct vm_area_struct *vma = NULL;
	struct page *begin_page = NULL;
	struct virt_mem_pool *pool = NULL;
	int ret;
	int i = 0;
	printk("gfn is %lx\r\n",gfn);

	if(NULL == kvm)
	{
		hva = gfn;
	}
	else
	{
		hva = gfn_to_hva(kvm,gfn);
	}

	printk("hva is %lx\r\n",hva);
	vma = find_vma(mm,hva);
#if 0
	begin_page = follow_page(vma,hva,FOLL_TOUCH);
#endif
	ret = get_user_pages_fast(hva,1,1,&begin_page);
	if(NULL == begin_page)
	{
		return -1;
	}
	
	pool =  (struct virt_mem_pool*)kmap(begin_page);
	print_virt_mem_pool(pool);
	if(pool->magic != 0xABABABABABABABABULL)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		kunmap(begin_page);
		put_page(begin_page);
		return -1;
	}
	if(pool->pool_id != -1)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		kunmap(begin_page);
		put_page(begin_page);
		return -1;
	}
	pool->args.mm = mm;
	pool->args.vma = vma;
	pool->args.task = task;
	pool->args.kvm = kvm;
	pool->hva = hva;
	for(i = 0; i < MEM_POOL_MAX ; i++)
	{
		if(0 == i)
		{
			continue;
		}
		if(mem_pool_addr[i] != 0)
		{
			continue;
		}
		pool->pool_id = i;
		strcpy(pool->mem_ind,release_dsc);
		mem_pool_addr[i] = kmalloc(sizeof(struct virt_mem_pool),GFP_KERNEL);
		if(mem_pool_addr[i])
		{
			memcpy(mem_pool_addr[i],pool,sizeof(struct virt_mem_pool));
			print_virt_mem_pool(mem_pool_addr[i]);
			kunmap(begin_page);
			put_page(begin_page);
			return 0;
		}
		
		printk("virt mem error in line%d\r\n ",__LINE__);
		kunmap(begin_page);
		put_page(begin_page);
		return -1;
	}
	printk("virt mem error in line%d\r\n ",__LINE__);
	kunmap(begin_page);
	put_page(begin_page);
	return -1;
}
EXPORT_SYMBOL(mem_pool_reg);
int is_in_mem_pool(struct mm_struct *mm)
{
	int i =0;
#if 1
	for(i =0 ; i < MEM_POOL_MAX; i++)
	{
		if(mem_pool_addr[i]!=NULL)
		{
			if(mem_pool_addr[i]->args.mm == mm)
			{
				return 1;
			}
		}
	}
#endif
	return 0;
}
int delete_mm_in_pool(struct mm_struct *mm)
{
	int i =0;
#if 0
	for(i =0 ; i < MEM_POOL_MAX; i++)
	{
		if(mem_pool_addr[i]!=NULL)
		{
			if(mem_pool_addr[i]->args.mm == mm)
			{
				kfree(mem_pool_addr[i]);
				mem_pool_addr[i] = NULL;
			}
		}
	}
#endif
}

int is_virt_page_release(struct virt_release_mark *mark)
{
	if((0 == strcmp(mark->desc,release_dsc))&&(mark->pool_id < MEM_POOL_MAX))
	{
		return 0;
	}
	return 1;
}


unsigned long long virt_mem_page_lock_err;
unsigned long long virt_mem_page_count_err;
unsigned long long virt_mem_page_state_conflict;

int process_virt_page_release(void *page_mem,struct page *org_page)
{
	int pool_id = 0;
	unsigned long long alloc_id = 0;
	struct virt_release_mark *mark = page_mem;
	unsigned long pool_va = 0;
	struct mm_struct *mm = NULL;
	struct vm_area_struct *vma = NULL;
	struct task_struct *task = NULL;

	unsigned long dsc_page_addr = 0;
	unsigned long dsc_off = 0;
	unsigned long dsc_page_off = 0;
	struct page *page = NULL;
	void *dsc_page = NULL;
	unsigned long long *dsc = NULL;
	unsigned long gfn = 0;
	unsigned long hva = 0;
	struct page *release_page = NULL;
	struct kvm *kvm = NULL;

	pmd_t *dsc_pmd;
	pte_t *dsc_ptep;
	spinlock_t *dsc_ptl;
	pte_t dsc_pte;
	
	pmd_t *r_pmd;
	pte_t *r_ptep;
	spinlock_t *r_ptl;
	pte_t r_pte;

	int ret = -1;
	if(!page_mem)
	{
		return -1;
	}
	pool_id = mark->pool_id;
	alloc_id = mark->alloc_id;
	if(pool_id > MEM_POOL_MAX)
	{
		if(pool_id != MEM_POOL_MAX +1)
		printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}
	if(NULL == mem_pool_addr[pool_id])
	{
		//printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}
	
	pool_va = mem_pool_addr[pool_id]->hva;
	vma = mem_pool_addr[pool_id]->args.vma;
	mm = mem_pool_addr[pool_id]->args.mm;
	task = mem_pool_addr[pool_id]->args.task;
	kvm = mem_pool_addr[pool_id]->args.kvm;
	if(alloc_id > mem_pool_addr[pool_id]->desc_max)
	{
		//printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}
	
	/*get desc page ,desc add*/
	dsc_off = sizeof(struct virt_mem_pool)+alloc_id*sizeof(unsigned long long);
	
	dsc_page_addr = (pool_va + dsc_off)&PAGE_MASK;
	dsc_page_off = pool_va +dsc_off - dsc_page_addr;
	#if 0	
	if(!down_read_trylock(&mm->mmap_sem))
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}
	
	
	dsc_pmd = mm_find_pmd(mm, dsc_page_addr);
	if (!dsc_pmd)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		up_read(&mm->mmap_sem);
		return -1;
	}

	dsc_ptl = pte_lockptr(mm,dsc_pmd);
	dsc_ptep = pte_offset_map(dsc_pmd,dsc_page_addr);
	spin_lock(dsc_ptl);
	
	dsc_pte = *dsc_ptep;
	if(!pte_present(dsc_pte))
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		pte_unmap_unlock(dsc_ptep,dsc_ptl);
		up_read(&mm->mmap_sem);
		return -1;
	}

	if(!pte_write(dsc_pte))
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		pte_unmap_unlock(dsc_ptep,dsc_ptl);
		up_read(&mm->mmap_sem);
		return -1;
	}
	page =  vm_normal_page(vma,dsc_page_addr,dsc_pte);
	if(!page)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		pte_unmap_unlock(dsc_ptep,dsc_ptl);
		up_read(&mm->mmap_sem);
		return -1;
	}
	
	dsc_page = kmap(page);
	dsc = dsc_page+dsc_page_off;
	pte_unmap_unlock(dsc_ptep,dsc_ptl);
#endif
	
	ret = get_user_pages_unlocked(task,mm,dsc_page_addr,1,1,0,&page);
	if(!page)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}

	dsc_page = kmap(page);
	dsc = dsc_page+dsc_page_off;

	/*load desc ,convert gpa to hpa*/
	gfn = *dsc;
	if(kvm)
	{
		hva = gfn_to_hva(kvm ,gfn);
	}
	else
	{
		hva = gfn;
	}
	
	if(!down_read_trylock(&mm->mmap_sem))
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		kunmap(page);
		put_page(page);
		return -1;
	}

	vma = find_vma(mm,hva);	

	r_pmd = mm_find_pmd(mm, hva);
	if (!r_pmd)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		up_read(&mm->mmap_sem);
		kunmap(page);
		put_page(page);
		return -1;
	}

	r_ptl = pte_lockptr(mm,r_pmd);
	r_ptep = pte_offset_map(r_pmd,hva);
	spin_lock(r_ptl);
	r_pte = *r_ptep;
	
	if(!pte_present(r_pte))
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		pte_unmap_unlock(r_ptep,r_ptl);
		up_read(&mm->mmap_sem);
		kunmap(page);
		put_page(page);
		return -1;
	}

	release_page =  vm_normal_page(vma,hva,r_pte);
	if(!release_page)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		pte_unmap_unlock(r_ptep,r_ptl);
		up_read(&mm->mmap_sem);
		kunmap(page);
		put_page(page);
		return -1;
	}
	
	if(release_page == org_page)
	{
			if(gfn == atomic64_cmpxchg((atomic64_t*)dsc,gfn,0))
			{
				/*replace org page to zero page*/
			
				unsigned long mmun_start;	/* For mmu_notifiers */
				unsigned long mmun_end;		/* For mmu_notifiers */
				pte_t entry;

				mmun_start = hva;
				mmun_end   = hva + PAGE_SIZE;
				mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

				flush_cache_page(vma, release_page, pte_pfn(*r_ptep));
				entry = ptep_clear_flush_notify(vma, hva, r_ptep);
				if(page_mapcount(release_page) != page_count(release_page))
				{
					set_pte_at(mm,hva,r_ptep,entry);
					atomic64_add(1,(atomic64_t*)&virt_mem_page_count_err);
				}
				else
				{
					get_page(release_merge_page);
					atomic_add(1,&release_merge_page->_mapcount);
					set_pte_at_notify(mm, hva, r_ptep, pte_wrprotect(mk_pte(release_merge_page, vma->vm_page_prot)));
					page_remove_rmap(release_page);
					ret = 0;
				}
				pte_unmap_unlock(r_ptep, r_ptl);
				kunmap(page);
				put_page(page);
				up_read(&mm->mmap_sem);
				mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
				return ret;
			}
			else
			{
				atomic64_add(1,(atomic64_t*)&virt_mem_page_state_conflict);
			}
	}
	printk("virt mem error in line%d\r\n ",__LINE__);
	pte_unmap_unlock(r_ptep,r_ptl);
	kunmap(page);
	put_page(page);
	up_read(&mm->mmap_sem);
	return -1;
}

int walk_virt_page_release(struct virt_mem_pool *pool)
{
	unsigned long long alloc_id = 0;
	unsigned long pool_va = 0;
	struct mm_struct *mm = NULL;
	struct vm_area_struct *vma = NULL;

	unsigned long dsc_page_addr = 0;
	unsigned long dsc_off = 0;
	unsigned long dsc_page_off = 0;
	struct page *page = NULL;
	void *dsc_page = NULL;
	unsigned long long *dsc = NULL;
	unsigned long gfn = 0;
	unsigned long hva = 0;
	struct page *release_page = NULL;
	struct kvm *kvm = NULL;

	pmd_t *dsc_pmd;
	pte_t *dsc_ptep;
	spinlock_t *dsc_ptl;
	pte_t dsc_pte;
	
	pmd_t *r_pmd;
	pte_t *r_ptep;
	spinlock_t *r_ptl;
	pte_t r_pte;
	
	if(pool->pool_id > MEM_POOL_MAX)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}
	
	pool_va = pool->hva;
	vma = pool->args.vma;
	mm = pool->args.mm;
	kvm = pool->args.kvm;
	for(alloc_id = 0 ; alloc_id < pool->desc_max;alloc_id++)	
	{
		/*get desc page ,desc add*/
		dsc_off = sizeof(struct virt_mem_pool)+alloc_id*sizeof(unsigned long long);
	
		dsc_page_addr = (pool_va + dsc_off)&PAGE_MASK;
		dsc_page_off = pool_va +dsc_off - dsc_page_addr;
	
		if(!down_read_trylock(&mm->mmap_sem))
		{
			printk("virt mem error in line%d\r\n ",__LINE__);
			return -1;
		}
	
	
		dsc_pmd = mm_find_pmd(mm, dsc_page_addr);
		if (!dsc_pmd)
		{
			printk("virt mem error in line%d\r\n ",__LINE__);
			up_read(&mm->mmap_sem);
			return -1;
		}

		dsc_ptl = pte_lockptr(mm,dsc_pmd);
		dsc_ptep = pte_offset_map(dsc_pmd,dsc_page_addr);
		spin_lock(dsc_ptl);
	
		dsc_pte = *dsc_ptep;
		if(!pte_present(dsc_pte))
		{
			printk("virt mem error in line%d\r\n ",__LINE__);
			pte_unmap_unlock(dsc_ptep,dsc_ptl);
			up_read(&mm->mmap_sem);
			return -1;
		}

		if(!pte_write(dsc_pte))
		{
			printk("virt mem error in line%d\r\n ",__LINE__);
			pte_unmap_unlock(dsc_ptep,dsc_ptl);
			up_read(&mm->mmap_sem);
			return -1;
		}
		page =  vm_normal_page(vma,dsc_page_addr,dsc_pte);
		if(!page)
		{
			printk("virt mem error in line%d\r\n ",__LINE__);
			pte_unmap_unlock(dsc_ptep,dsc_ptl);
			up_read(&mm->mmap_sem);
			return -1;
		}
	
		dsc_page = kmap(page);
		dsc = dsc_page+dsc_page_off;
		pte_unmap_unlock(dsc_ptep,dsc_ptl);
	
	
		/*load desc ,convert gpa to hpa*/
		gfn = *dsc;
		if(gfn == 0)
		{
			up_read(&mm->mmap_sem);
			kunmap(page);
			continue;
		}
		if(kvm)
		{
			hva = gfn_to_hva(kvm ,gfn);
		}
		else
		{	
			hva = gfn;
		}
	
		vma = find_vma(mm,hva);	

		r_pmd = mm_find_pmd(mm, hva);
		if (!r_pmd)
		{
			printk("virt mem error in line%d\r\n ",__LINE__);
			up_read(&mm->mmap_sem);
			kunmap(page);
			return -1;
		}

		r_ptl = pte_lockptr(mm,r_pmd);
		r_ptep = pte_offset_map(r_pmd,hva);
		spin_lock(r_ptl);
		r_pte = *r_ptep;
	
		if(!pte_present(r_pte))
		{
			printk("virt mem error in line%d\r\n ",__LINE__);
			pte_unmap_unlock(r_ptep,r_ptl);
			up_read(&mm->mmap_sem);
			kunmap(page);
			return -1;
		}

		release_page =  vm_normal_page(vma,hva,r_pte);
		if(!release_page)
		{
			printk("virt mem error in line%d\r\n ",__LINE__);
			pte_unmap_unlock(r_ptep,r_ptl);
			up_read(&mm->mmap_sem);
			kunmap(page);
			return -1;
		}
		printk("walk virt mem slice state is %lld\r\n",get_slice_state_by_id(page_to_pfn(release_page)));
		pte_unmap_unlock(r_ptep,r_ptl);
		kunmap(page);
		up_read(&mm->mmap_sem);
	}
	return 0;
}




