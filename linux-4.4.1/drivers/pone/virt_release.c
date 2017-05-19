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
#include <pone/virt_release.h>

extern pmd_t *mm_find_pmd(struct mm_struct *mm,unsigned long long address);
char* release_dsc = "page can release xxx";
struct page *release_merge_page = NULL;

EXPORT_SYMBOL(release_merge_page);

int page_recycle_enable = 0;

EXPORT_SYMBOL(page_recycle_enable);

struct virt_mem_pool *guest_mem_pool = NULL;

EXPORT_SYMBOL(guest_mem_pool);

struct virt_mem_pool *mem_pool_addr[MEM_POOL_MAX] = {0};

int virt_mark_page_release(struct page *page)
{
	int pool_id ;
	unsigned long long alloc_id;
	unsigned long long state;
	unsigned long long idx ;
	struct virt_release_mark *mark ;
	if(!guest_mem_pool)
	{
		return 0;
	}
	
	pool_id = guest_mem_pool->pool_id;
	alloc_id = atomic64_add_return(1,(atomic64_t*)&guest_mem_pool->alloc_idx)-1;
	idx = alloc_id%guest_mem_pool->desc_max;
	state = guest_mem_pool->desc[idx];
	if(0 == state)
	{
		mark =kmap_atomic(page);
		if(0 != atomic64_cmpxchg((atomic64_t*)&guest_mem_pool->desc[idx],0,page_to_pfn(page)))
		{
			pool_id = -1;
			idx = -1;
			
		}
		strcpy(mark->desc,guest_mem_pool->mem_ind);
		mark->pool_id = pool_id;
		mark->alloc_id = idx;
		kunmap(page);
		return 0;
	}
	return -1;
}

int virt_mark_page_alloc(struct page *page)
{
	int pool_id ;
	unsigned long long alloc_id;
	unsigned long long state;
	unsigned long long idx ;
	struct virt_release_mark *mark ;
	if(!guest_mem_pool)
	{
		return 0;
	}

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
						kunmap(page);
						return 0;
					}
				}
			}
		}
	}
		
	while(mark->desc[0] != '0')
	{

	}
	kunmap(page);
	return -1;
}

int virt_mem_release_init(void)
{
	void *page_addr = NULL;
	int i = 0;
	if(!release_merge_page)
	{
		release_merge_page = alloc_page(GFP_KERNEL);
		if(release_merge_page)
		{
			page_addr = kmap(release_merge_page);
			memset(page_addr,0,PAGE_SIZE);
			kunmap(release_merge_page);
	
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
	printk("mem_ind  is %s\r\n",pool->mem_ind);
	printk("hva is 0x%llx\r\n",pool->hva);
	printk("args mm  is %p\r\n",pool->args.mm);
	printk("args vma  is %p\r\n",pool->args.vma);
	printk("args task  is %p\r\n",pool->args.task);
	printk("args kvm  is %p\r\n",pool->args.kvm);
	printk("desc_max is %llx\r\n",pool->desc_max);
}


EXPORT_SYMBOL(print_virt_mem_pool);

int mem_pool_reg(unsigned long gfn,struct kvm *kvm,struct mm_struct *mm,struct task_struct *task)
{
	unsigned long hva = 0;
	struct vm_area_struct *vma = NULL;
	struct page *begin_page = NULL;
	struct virt_mem_pool *pool = NULL;
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

	begin_page = follow_page(vma,hva,FOLL_TOUCH);

	if(NULL == begin_page)
	{
		return -1;
	}
	
	pool =  (struct virt_mem_pool*)kmap(begin_page);
	print_virt_mem_pool(pool);
	if(pool->magic != 0xABABABABABABABABULL)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}
	if(pool->pool_id != -1)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}
	pool->args.mm = mm;
	pool->args.vma = vma;
	pool->args.task = task;
	pool->args.kvm = kvm;
	pool->hva = hva;
	for(i = 0; i < MEM_POOL_MAX ; i++)
	{
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
			print_virt_mem_pool(pool);
			return 0;
		}
		
		printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}
	printk("virt mem error in line%d\r\n ",__LINE__);
	return -1;
}
EXPORT_SYMBOL(mem_pool_reg);
int is_in_mem_pool(struct mm_struct *mm)
{
	int i =0;
#if 0
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
}

int is_virt_page_release(struct virt_release_mark *mark)
{
	return strcmp(mark->desc,release_dsc);
}
int process_virt_page_release(void *page_mem,struct page *org_page)
{
	int pool_id = 0;
	unsigned long long alloc_id = 0;
	struct virt_release_mark *mark = page_mem;
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
	
	if(!page_mem)
	{
		return -1;
	}
	pool_id = mark->pool_id;
	alloc_id = mark->alloc_id;
	if(pool_id > MEM_POOL_MAX)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		return -1;
	}
	
	pool_va = mem_pool_addr[pool_id]->hva;
	vma = mem_pool_addr[pool_id]->args.vma;
	mm = mem_pool_addr[pool_id]->args.mm;
	kvm = mem_pool_addr[pool_id]->args.kvm;
	alloc_id = alloc_id%mem_pool_addr[pool_id]->desc_max;
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
		return -1;
	}

	if(!pte_write(r_pte))
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		pte_unmap_unlock(r_ptep,r_ptl);
		up_read(&mm->mmap_sem);
		return -1;
	}
	release_page =  vm_normal_page(vma,hva,r_pte);
	if(!release_page)
	{
		printk("virt mem error in line%d\r\n ",__LINE__);
		pte_unmap_unlock(r_ptep,r_ptl);
		up_read(&mm->mmap_sem);
		return -1;
	}
	

	if(release_page == org_page)
	{
		if(gfn == atomic64_cmpxchg((atomic64_t*)dsc,gfn,0))
		{
			/*replace org page to zero page*/
			
			unsigned long mmun_start;	/* For mmu_notifiers */
			unsigned long mmun_end;		/* For mmu_notifiers */

			mmun_start = hva;
			mmun_end   = hva + PAGE_SIZE;
			mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);
			get_page(release_merge_page);
			page_add_anon_rmap(release_merge_page, vma, hva);

			flush_cache_page(vma, release_page, pte_pfn(*r_ptep));
			ptep_clear_flush_notify(vma, hva, r_ptep);
			set_pte_at_notify(mm, hva, r_ptep, pte_wrprotect(mk_pte(release_merge_page, vma->vm_page_prot)));

			page_remove_rmap(release_page);
			put_page(release_page);

			pte_unmap_unlock(r_ptep, r_ptl);
			kunmap(page);
			up_read(&mm->mmap_sem);
			mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
			return 0;
		}

	}
	printk("virt mem error in line%d\r\n ",__LINE__);
	pte_unmap_unlock(r_ptep,r_ptl);
	kunmap(page);
	up_read(&mm->mmap_sem);
	return -1;
}


