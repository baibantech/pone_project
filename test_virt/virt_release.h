#ifndef __VIRT_RELEASE_H__
#define __VIRT_RELEASE_H__
/*************************************************************************
	> File Name: virt_release.h
	> Author: lijiyong
	> Mail: lijiyong0303@163.com 
	> Created Time: Wed 10 May 2017 03:17:12 AM EDT
 ************************************************************************/

#define MEM_POOL_MAX 32
#define VIRT_RELEASE_IOCTL_MAGIC 0xF6


#define VIRT_RELEASE_IOC_REG_MEM_POOL _IO(VIRT_RELEASE_IOCTL_MAGIC,1) 
struct virt_mem_args
{
	unsigned long mm;
	unsigned long vma;
	unsigned long task;
	unsigned long kvm;
};

struct virt_mem_pool
{
	unsigned long long magic;
	int  pool_id;
	unsigned long long hva;
	struct virt_mem_args args;
	unsigned long long desc_max;
	unsigned long long alloc_idx;
	unsigned long long desc[0];
};

struct virt_release_mark
{
	char desc[64];
	int pool_id;
	unsigned long long alloc_id;
};

#endif
