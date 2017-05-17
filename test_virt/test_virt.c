/*************************************************************************
	> File Name: test_case_main.c
	> Author: ma6174
	> Mail: ma6174@163.com 
	> Created Time: Tue 21 Feb 2017 07:35:16 PM PST
 ************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#define __USE_GNU 1
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include "virt_release.h"
#include "atomic_user.h"
char *release_dsc = "page can release xxx";
struct virt_mem_pool *control_pool = NULL;
void init_mem_pool(void *addr,int len)
{
	struct virt_mem_pool *pool = addr;
	memset(addr,0,len);
	pool->magic= 0xABABABABABABABAB;
	pool->pool_id = -1;
	pool->desc_max = (len - sizeof(struct virt_mem_pool))/sizeof(unsigned long long);
}

int mark_page_release(void *addr)
{
	int pool_id = control_pool->pool_id;
	unsigned long long alloc_id = atomic64_add_return(1,(atomic64_t*)&control_pool->alloc_idx)-1;
	unsigned long long state;
	unsigned long long idx = alloc_id%control_pool->desc_max;
	struct virt_release_mark *mark =addr;
	state = control_pool->desc[idx];
	if(0 == state)
	{
		if(0 != atomic64_cmpxchg((atomic64_t*)&control_pool->desc[idx],0,(unsigned long)addr))
		{
			return -1;
		}
		strcpy(mark->desc,release_dsc);
		mark->pool_id = pool_id;
		mark->alloc_id = idx;
		return 0;
	}
	return -1;
}


int main(int argc,char *argv[])
{
	void *addr = NULL;
	void *control_addr = NULL;
	int i = 0;
	int fd;
	int cmd; 
	printf("test begin\r\n");
#if 1
	fd = open("/dev/virt_release_mem",O_RDWR);
	if(fd <0)
	{
		printf("open virt release err\r\n");
	}
#endif
	
	control_addr = mmap(0 ,4096*2 ,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
	
	if(control_addr)
	{
		printf("mem pool contol addr is %p\r\n",control_addr);
	}
	init_mem_pool(control_addr,4096*2);
	
	ioctl(fd,VIRT_RELEASE_IOC_REG_MEM_POOL,control_addr);
	control_pool = control_addr;
#if 1	
	addr = mmap(0 ,4096*100 ,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
	sleep(10);
	printf("begin addr is %p\r\n",addr);
	printf("test malloc \r\n");
	
	for (i = 0; i <100 ;i++)
	{
		memset(addr+i*4096,i,4096);
	}
	sleep(5);

	printf("test set end  \r\n");
	mark_page_release(addr);
#endif		
	sleep(10);

	while(1)
	{
		sleep(10);
	}
	return; 
}
