#include "vector.h"
#include "chunk.h"
#include "spt_dep.h"
#include <linux/slab.h>

void *spt_malloc(unsigned long size)
{
    return kmalloc(size,GFP_ATOMIC);
}

void spt_free(void *ptr)
{
    kfree(ptr);
}

char* spt_alloc_zero_page(void)
{
    void *p;
    p = kmalloc(4096,GFP_ATOMIC);
    if(p != 0)
    {
        memset(p, 0, 4096);
        smp_mb();
    }
    return p;
}

void spt_free_page(void *page)
{
    kfree(page);
}

void *spt_realloc(void *mem_address, unsigned long newsize)
{
    return krealloc(mem_address, newsize, GFP_ATOMIC);
}


