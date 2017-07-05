#ifndef __VIRT_RELEASE_H__
#define __VIRT_RELEASE_H__
/*************************************************************************
	> File Name: virt_release.h
	> Author: lijiyong
	> Mail: lijiyong0303@163.com 
	> Created Time: Wed 10 May 2017 03:17:12 AM EDT
 ************************************************************************/

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mmu_notifier.h>
#include <linux/swap.h>
#define MEM_POOL_MAX 32
struct virt_mem_args
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct task_struct *task;
	struct kvm *kvm;
};

struct virt_mem_pool
{
	unsigned long long magic;
	int  pool_id;
	char mem_ind[64];
	unsigned long long hva;
	struct virt_mem_args args;
	unsigned long long desc_max;
	unsigned long long alloc_idx;
	unsigned long long debug_r_begin;
	unsigned long long debug_r_end;
	unsigned long long debug_a_begin;
	unsigned long long debug_a_end;
	unsigned long long mark_release_ok;
	unsigned long long mark_release_err_conflict;
	unsigned long long mark_release_err_state;
	unsigned long long mark_alloc_ok;
	unsigned long long mark_alloc_err_conflict;
	unsigned long long mark_alloc_err_state;

	unsigned long long desc[0];
};

struct virt_release_mark
{
	char desc[64];
	int pool_id;
	unsigned long long alloc_id;
};

extern int guest_page_clear_ok;
extern int guest_page_no_need_clear;

static inline void set_guest_page_clear_ok(void)
{
	if(!guest_page_clear_ok)
		guest_page_clear_ok = 1;
}

extern struct virt_mem_pool   *mem_pool_addr[MEM_POOL_MAX] ;

extern int mem_pool_reg(unsigned long gfn,struct kvm *kvm,struct mm_struct *mm,struct task_struct *task);

extern int is_virt_page_release(struct virt_release_mark *mark);

extern int process_virt_page_release(void *page_mem,struct page *org_page);
 
extern int virt_mem_release_init(void);

extern int is_in_mem_pool(struct mm_struct *mm);

extern int virt_mark_page_release(struct page *page);

extern int virt_mark_page_alloc(struct page *page);

extern void print_virt_mem_pool(struct virt_mem_pool *pool);
#endif
