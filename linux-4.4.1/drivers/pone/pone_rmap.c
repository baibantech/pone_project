/*************************************************************************
	> File Name: pone_rmap.c
	> Author: lijiyong0303
	> Mail: lijiyong0303@163.com 
	> Created Time: Wed 02 Aug 2017 03:12:12 PM CST
 ************************************************************************/

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/migrate.h>
#include <linux/hugetlb.h>
#include <linux/backing-dev.h>
#include <linux/page_idle.h>

#include <asm/tlbflush.h>

#include <trace/events/tlb.h>

extern pmd_t *mm_find_pmd(struct mm_struct *mm , unsigned long address);

/* the code copy from mm/rmap.c ------begin*/
static inline unsigned long
__vma_address(struct page *page, struct vm_area_struct *vma)
{
	pgoff_t pgoff = page_to_pgoff(page);
	return vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
}

static inline unsigned long
vma_address(struct page *page, struct vm_area_struct *vma)
{
	unsigned long address = __vma_address(page, vma);

	/* page should be within @vma mapping range */
	VM_BUG_ON_VMA(address < vma->vm_start || address >= vma->vm_end, vma);

	return address;
}

static struct anon_vma *rmap_walk_anon_lock(struct page *page,
					struct rmap_walk_control *rwc)
{
	struct anon_vma *anon_vma;

	if (rwc->anon_lock)
		return rwc->anon_lock(page);

	/*
	 * Note: remove_migration_ptes() cannot use page_lock_anon_vma_read()
	 * because that depends on page_mapped(); but not all its usages
	 * are holding mmap_sem. Users without mmap_sem are required to
	 * take a reference count to prevent the anon_vma disappearing
	 */
	anon_vma = page_anon_vma(page);
	if (!anon_vma)
		return NULL;

	anon_vma_lock_read(anon_vma);
	return anon_vma;
}
/* the code copy from mm/rmap.c ------end*/



unsigned long long pte_err1;
unsigned long long pte_err2;
unsigned long long pte_err3;
unsigned long long pte_err4;
unsigned long long pte_err5;
pte_t*  __page_try_check_address(struct page *page, struct mm_struct *mm,
					  unsigned long address, spinlock_t **ptlp, int sync)
{
	pmd_t *pmd;
	pte_t *pte;
	spinlock_t *ptl;

	if (unlikely(PageHuge(page))) {
		atomic64_add(1,(atomic64_t*)&pte_err1);
		return NULL;
	}

	pmd = mm_find_pmd(mm, address);
	if (!pmd)
	{
		atomic64_add(1,(atomic64_t*)&pte_err2);
		return NULL;
	}

	pte = pte_offset_map(pmd, address);
	/* Make a quick check before getting the lock */
	if (!sync && !pte_present(*pte)) {
		
		atomic64_add(1,(atomic64_t*)&pte_err3);
		pte_unmap(pte);
		return NULL;
	}

	ptl = pte_lockptr(mm, pmd);
	
	if(spin_trylock(ptl))
	{
		if (pte_present(*pte) && page_to_pfn(page) == pte_pfn(*pte)) {
			*ptlp = ptl;
			return pte;
		}
		atomic64_add(1,(atomic64_t*)&pte_err4);
		pte_unmap_unlock(pte, ptl);
	}
	atomic64_add(1,(atomic64_t*)&pte_err5);
	pte_unmap(pte);
	return NULL;
}

pte_t *__page_get_pte_address(struct page *page, struct mm_struct *mm,
					  unsigned long address)
{
	pmd_t *pmd;
	pte_t *pte;

	if (unlikely(PageHuge(page))) {
		return NULL;
	}

	pmd = mm_find_pmd(mm, address);
	if (!pmd)
	return NULL;

	pte = pte_offset_map(pmd, address);

	if (pte_present(*pte) && page_to_pfn(page) == pte_pfn(*pte)) {
		return pte;
	}

	pte_unmap(pte);
	return NULL;
}

struct anon_vma *page_try_lock_anon_vma_read(struct page *page)
{
	struct anon_vma *anon_vma = NULL;
	struct anon_vma *root_anon_vma;
	unsigned long anon_mapping;

	rcu_read_lock();
	anon_mapping = (unsigned long) READ_ONCE(page->mapping);
	if ((anon_mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_ANON)
		goto out;
	if (!page_mapped(page))
		goto out;

	anon_vma = (struct anon_vma *) (anon_mapping - PAGE_MAPPING_ANON);
	root_anon_vma = READ_ONCE(anon_vma->root);
	
	if(!atomic_inc_not_zero(&anon_vma->refcount))
	{
		anon_vma = NULL;
		goto out;
	}
	
	if (down_read_trylock(&root_anon_vma->rwsem)) {
		/*
		 * If the page is still mapped, then this anon_vma is still
		 * its anon_vma, and holding the mutex ensures that it will
		 * not go away, see anon_vma_free().
		 */
		

		if (!page_mapped(page)) {
			up_read(&root_anon_vma->rwsem);
			put_anon_vma(anon_vma);
			anon_vma = NULL;
		}
		goto out;
	}
	else
	{
		put_anon_vma(anon_vma);
		anon_vma = NULL;
	}

out:
	rcu_read_unlock();
	return anon_vma;
}
unsigned long long rmap_rwsem_count = 0;
unsigned long long rmap_rwsem_release_count = 0;
unsigned long long rmap_get_anon_vma_err = 0;
unsigned long long rmap_lock_num_err = 0;
unsigned long long rmap_pte_null_err = 0;
#if 0
static int rmap_walk_pone_anon(struct page *page, struct rmap_walk_control *rwc)
{
	struct anon_vma *anon_vma;
	pgoff_t pgoff = page_to_pgoff(page);
	struct anon_vma_chain *avc;
	int ret = SWAP_FAIL;
	int lock_num = 0;
	int i = 0;
	pte_t  *pte[16] = { NULL};
	spinlock_t *ptl[16] = {NULL};
	struct mm_struct *walk_mm[16] = {NULL};
	unsigned long walk_address[16] = {0};
	int cnt = 0;
#if 1
	anon_vma = rmap_walk_anon_lock(page, rwc);
#else

	anon_vma = page_lock_anon_vma_read(page);
#endif
	if (!anon_vma)
	{
		atomic64_add(1,(atomic64_t*)&rmap_get_anon_vma_err);
		return ret;
	}
	anon_vma_interval_tree_foreach(avc, &anon_vma->rb_root, pgoff, pgoff) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long address = vma_address(page, vma);
		if(16 == lock_num)
		{
			atomic64_add(1,(atomic64_t*)&rmap_lock_num_err);
			goto free_lock;
		}
		mmu_notifier_invalidate_range_start(vma->vm_mm,address,address+PAGE_SIZE);
		ret  = __page_try_check_address(page, vma->vm_mm,address, &ptl[lock_num],0);
		if(0 != ret)
		{
			mmu_notifier_invalidate_range_end(vma->vm_mm,address,address+PAGE_SIZE);
			//atomic64_add(1,(atomic64_t*)&rmap_pte_null_err);
			goto free_lock;
		}
		walk_mm[lock_num] = vma->vm_mm;
		walk_address[lock_num] = address;
		lock_num++;
	}
	
	anon_vma_interval_tree_foreach(avc, &anon_vma->rb_root, pgoff, pgoff) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long address = vma_address(page, vma);
		cnt++;
		ret = rwc->rmap_one(page, vma, address, rwc->arg);
		if (ret != 0)
		{
			goto free_lock;
		}
	}
	if(cnt == 0 || lock_num == 0)
{
			atomic64_add(1,(atomic64_t*)&rmap_pte_null_err);
	
}

	ret = SWAP_SUCCESS;
	
free_lock:
	for (i = 0 ; i < lock_num; i++)
	{
		if(ptl[i]!=NULL)
		spin_unlock(ptl[i]);

		if(walk_mm[i]!= NULL)
			mmu_notifier_invalidate_range_end(walk_mm[i], walk_address[i], walk_address[i]+PAGE_SIZE);
	}

	anon_vma_unlock_read(anon_vma);
#if 1
	put_anon_vma(anon_vma);
#endif
	return ret;
}
#else
static int rmap_walk_pone_anon(struct page *page, struct rmap_walk_control *rwc)
{
	struct anon_vma *anon_vma;
	pgoff_t pgoff;
	struct anon_vma_chain *avc;
	int ret = SWAP_FAIL;

	anon_vma = rmap_walk_anon_lock(page, rwc);
	if (!anon_vma)
	{
		atomic64_add(1,(atomic64_t*)&rmap_get_anon_vma_err);
		return ret;
	}

	pgoff = page_to_pgoff(page);
	anon_vma_interval_tree_foreach(avc, &anon_vma->rb_root, pgoff, pgoff) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long address = vma_address(page, vma);

		if (rwc->invalid_vma && rwc->invalid_vma(vma, rwc->arg))
			continue;

		ret = rwc->rmap_one(page, vma, address, rwc->arg);
		if (ret != 0)
			break;
		if (rwc->done && rwc->done(page))
			break;
	}
	anon_vma_unlock_read(anon_vma);
	put_anon_vma(anon_vma);
	return ret;

}

#endif


static int rmap_walk_pone_file(struct page *page, struct rmap_walk_control *rwc)
{
	int ret = SWAP_FAIL;	
	ret = rwc->rmap_one(page, NULL, -1, rwc->arg);
	return ret;
}

int rmap_walk_pone(struct page *page, struct rmap_walk_control *rwc)
{
    if(PageKsm(page))
	{
		printk("page class err \r\n");
		return SWAP_FAIL;
	}
	else if (PageAnon(page)){
        return rmap_walk_pone_anon(page, rwc);
    }
    else{
        return rmap_walk_pone_file(page,rwc);
    }
}
EXPORT_SYMBOL(rmap_walk_pone);

long  pone_get_slice_que_id(struct page *page)
{
	struct anon_vma *anon_vma;
	pgoff_t pgoff;
	struct anon_vma_chain *avc;
	long  que_id = -1;
	anon_vma  = page_try_lock_anon_vma_read(page);
	if (!anon_vma)
	{
		return -1;
	}

	pgoff = page_to_pgoff(page);
	anon_vma_interval_tree_foreach(avc, &anon_vma->rb_root, pgoff, pgoff) {
		struct vm_area_struct *vma = avc->vma;
		que_id = (unsigned long)(void*)vma->vm_mm /sizeof(unsigned long);
		break;		
	}
	
	anon_vma_unlock_read(anon_vma);
	put_anon_vma(anon_vma);
	return que_id;
}
