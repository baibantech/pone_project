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
struct page *release_merge_page = NULL;

static DEFINE_SPINLOCK(pool_reg_lock);

EXPORT_SYMBOL(release_merge_page);

unsigned long long release_dsc = 0xABCDEF00AABBCC55ULL;

struct virt_mem_pool *mem_pool_addr[MEM_POOL_MAX] = {0};
unsigned long mark_release_count= 0;
unsigned long mark_alloc_count = 0;

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
			release_merge_page->mapping =(long)release_merge_page->mapping + PAGE_MAPPING_ANON;

			nid = slice_idx_to_node(page_to_pfn(release_merge_page));
			slice_id = slice_nr_in_node(nid,page_to_pfn(release_merge_page));
			if(0 != change_slice_state(nid,slice_id,SLICE_NULL,SLICE_CHG))
			{
				printk("BUG in virt mem release\r\n");
				return -1;
			}

			for(i = 0;i<MEM_POOL_MAX; i++)
			{
				mem_pool_addr[i] = NULL;
			}
			return 0;
		}
	}
	return -1;
}
 
void print_virt_mem_pool(struct virt_mem_pool *pool)
{
	printk("magic is 0x%llx\r\n",pool->magic);
	printk("pool_id is %d\r\n",pool->pool_id);
	printk("mem_ind  is %llx\r\n",pool->mem_ind);
	printk("hva is 0x%llx\r\n",pool->hva);
	printk("args mm  is %p\r\n",pool->args.mm);
	printk("args vma  is %p\r\n",pool->args.vma);
	printk("args task  is %p\r\n",pool->args.task);
	printk("args kvm  is %p\r\n",pool->args.kvm);
	printk("alloc id is %llx\r\n",pool->alloc_idx);
	printk("desc_max is %llx\r\n",pool->desc_max);
#if 0
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
#endif
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
			struct mm_struct *mm = mem_pool_addr[i]->args.mm;
			struct task_struct *task = mem_pool_addr[i]->args.task; 
			struct page *begin_page = NULL;
			get_user_pages_unlocked(task,mm,hva,1,1,0,&begin_page);
			if(NULL == begin_page)
			{
				printk("get host mem pool idx %d page err\r\n",i);
				continue;
			}
			
			pool =  (struct virt_mem_pool*)kmap(begin_page);
			printk("pool idx is %d\r\n",i);
			printk("pool page addr is %p\r\n",begin_page);
			printk("begin page state is %lld\r\n",get_slice_state_by_id(page_to_pfn(begin_page)));
			printk("pool hva %llx\r\n",hva);
			printk("pool mm %p\r\n",mm);
			printk("pool task %p\r\n",task);
			printk("pool vma %p\r\n",mem_pool_addr[i]->args.vma);
			printk("pool kvm %p\r\n",mem_pool_addr[i]->args.kvm);


			print_virt_mem_pool(pool);
			//walk_virt_page_release(pool);
			kunmap(begin_page);
			put_page(begin_page);
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
	//printk("gfn is %lx\r\n",gfn);

	if(NULL == kvm)
	{
		hva = gfn;
	}
	else
	{
		hva = gfn_to_hva(kvm,gfn);
	}

	//printk("hva is %lx\r\n",hva);
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
	//print_virt_mem_pool(pool);
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
	spin_lock(&pool_reg_lock);

	for(i = 1 ; i < MEM_POOL_MAX ;i++)
	{
		if(mem_pool_addr[i])
		{
			if(mm == mem_pool_addr[i]->args.mm)
			{
				PONE_DEBUG("mm same\r\n");
			}
			if(task == mem_pool_addr[i]->args.task)
			{
				PONE_DEBUG("task same\r\n");
			}
			if(kvm  == mem_pool_addr[i]->args.kvm)
			{
				PONE_DEBUG("kvm same\r\n");
			}
			if(vma  == mem_pool_addr[i]->args.vma)
			{
				PONE_DEBUG("vma same\r\n");
			}
			if(hva == mem_pool_addr[i]->hva)
			{
				PONE_DEBUG("hva same\r\n");
			}
		}
	}


	for(i = 1; i < MEM_POOL_MAX ; i++)
	{
		if(mem_pool_addr[i] != 0)
		{
			continue;
		}
		pool->pool_id = i;
		pool->mem_ind = release_dsc;
		pool->pool_max = MEM_POOL_MAX;
		mem_pool_addr[i] = kmalloc(sizeof(struct virt_mem_pool),GFP_KERNEL);
		if(mem_pool_addr[i])
		{
			memcpy(mem_pool_addr[i],pool,sizeof(struct virt_mem_pool));
			printk("page recycle reg pool id %d \r\n",pool->pool_id);
			//printk("pool page addr %p \r\n",begin_page);
			//print_virt_mem_pool(mem_pool_addr[i]);
			kunmap(begin_page);
			put_page(begin_page);
			spin_unlock(&pool_reg_lock);
			return 0;
		}
		
		printk("virt mem error in line%d\r\n ",__LINE__);
		kunmap(begin_page);
		put_page(begin_page);
		spin_unlock(&pool_reg_lock);
		return -1;
	}
	printk("virt mem error in line%d\r\n ",__LINE__);
	kunmap(begin_page);
	put_page(begin_page);
	spin_unlock(&pool_reg_lock);
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
	spin_lock(&pool_reg_lock);
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
	spin_unlock(&pool_reg_lock);
	return 0;
}
int pone_page_recycle_enable = 1;

int is_virt_page_release(struct virt_release_mark *mark)
{
	if(!pone_page_recycle_enable)
	{
		return 1;
	}
	if((mark->desc == release_dsc)&&(mark->pool_id < MEM_POOL_MAX))
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

	int ret = VIRT_MEM_FAIL;
	
	pool_id = mark->pool_id;
	alloc_id = mark->alloc_id;
	if(pool_id > MEM_POOL_MAX)
	{
		if(pool_id != MEM_POOL_MAX +1)
		PONE_DEBUG("virt mem error \r\n");
		return VIRT_MEM_FAIL;
	}
	if(NULL == mem_pool_addr[pool_id])
	{
		//printk("virt mem error in line%d\r\n ",__LINE__);
		return VIRT_MEM_FAIL;
	}
	
	pool_va = mem_pool_addr[pool_id]->hva;
	vma = mem_pool_addr[pool_id]->args.vma;
	mm = mem_pool_addr[pool_id]->args.mm;
	task = mem_pool_addr[pool_id]->args.task;
	kvm = mem_pool_addr[pool_id]->args.kvm;
	if(alloc_id > mem_pool_addr[pool_id]->desc_max)
	{
		//printk("virt mem error in line%d\r\n ",__LINE__);
		return VIRT_MEM_FAIL;
	}
	
	/*get desc page ,desc add*/
	dsc_off = sizeof(struct virt_mem_pool)+alloc_id*sizeof(unsigned long long);
	
	dsc_page_addr = (pool_va + dsc_off)&PAGE_MASK;
	dsc_page_off = pool_va +dsc_off - dsc_page_addr;
	
	ret = get_user_pages_unlocked(task,mm,dsc_page_addr,1,1,0,&page);
	if(!page)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		return VIRT_MEM_FAIL;
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
		return VIRT_MEM_RETRY;
	}

	vma = find_vma(mm,hva);	

	r_pmd = mm_find_pmd(mm, hva);
	if (!r_pmd)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		up_read(&mm->mmap_sem);
		kunmap(page);
		put_page(page);
		return VIRT_MEM_FAIL;
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
		return VIRT_MEM_FAIL;
	}

	release_page =  vm_normal_page(vma,hva,r_pte);
	if(!release_page)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		pte_unmap_unlock(r_ptep,r_ptl);
		up_read(&mm->mmap_sem);
		kunmap(page);
		put_page(page);
		return VIRT_MEM_FAIL;
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
					ret = VIRT_MEM_RETRY;
				}
				else
				{
					get_page(release_merge_page);
					atomic_add(1,&release_merge_page->_mapcount);
					set_pte_at_notify(mm, hva, r_ptep, pte_wrprotect(mk_pte(release_merge_page, vma->vm_page_prot)));
					page_remove_rmap(release_page);
					ret = VIRT_MEM_OK;;
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
	pte_unmap_unlock(r_ptep,r_ptl);
	kunmap(page);
	put_page(page);
	up_read(&mm->mmap_sem);
	return VIRT_MEM_FAIL;
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
		PONE_DEBUG("virt mem error \r\n");
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




