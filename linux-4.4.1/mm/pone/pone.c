#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/slab.h>
#include <linux/rmap.h>
#include <linux/jhash.h>

#include <pone/slice_state.h>
#include <pone/pone.h>

struct pone_hash_head*  pone_hash_table = NULL;
 
static u32 calc_checksum(void *addr,int len)
{
    u32 checksum;
    checksum = jhash2(addr, len / 4, 13);
    return checksum;
}

int pone_hash_table_init(int size)
{
    int i = 0;
    if(NULL == pone_hash_table)
    {
        pone_hash_table =kmalloc(sizeof(struct pone_hash_head)*size,GFP_KERNEL);
        if(pone_hash_table)
        {
            for(i = 0 ; i <size ; i++)
            {
                rwlock_init(&pone_hash_table[i].rwlock);
                pone_hash_table[i].head.first = NULL;
            }
            return 0;
        }     
    }

    return -1;
}


int pone_cmp(void *key,unsigned int key_len,unsigned long slice_idx)
{
    char *addr;
    unsigned int  ret;
    struct page *page = pfn_to_page(slice_idx);
    addr = kmap_atomic(page);
    ret = memcmp(key,addr,key_len);
    kunmap_atomic(addr);
    return ret; 
}

int pone_reverse_add(struct pone_desc *dsc,unsigned long slice_idx)
{
    struct page *page = pfn_to_page(slice_idx);
    pgoff_t pgoff = page_to_pgoff(page); 

	slice_reverse *ref = kmalloc(sizeof(slice_reverse),GFP_ATOMIC);
	if(NULL == ref)
	{
		return -1;
	}
	memset(ref,0,sizeof(slice_reverse));
	INIT_LIST_HEAD(&ref->list);
	if(PageAnon(page)){
		ref->type = SLICE_MEM;
	}
	else {
		ref->type = SLICE_FILE;
	}
	ref->poff = pgoff; 
	ref->reverse_info = page_rmapping(page);
	list_add(&ref->list,&dsc->ref_list);
	dsc->user_count++;
	return 0;
}

struct pone_desc* pone_insert(char *key,int key_len,unsigned long slice_idx)
{
    unsigned int crc = calc_checksum(key,key_len);
    
    unsigned short hash_key = crc;
    
    struct pone_desc *pone = NULL;
    struct pone_desc *pos = NULL;
    struct pone_desc *pre = NULL;
    
	pone = kmalloc(sizeof(struct pone_desc),GFP_ATOMIC);
    if(NULL == pone)
    {
        return NULL;
    } 

    pone->slice_idx = slice_idx;
    pone->key = hash_key;
    pone->user_count = 1;
	INIT_LIST_HEAD(&pone->ref_list);

	//printk("hash_key is 0x%x\r\n",hash_key);
	write_lock(&pone_hash_table[hash_key].rwlock);
     
	if(pone_hash_table[hash_key].head.first == NULL)
    {
        hlist_add_head(&pone->hlist,&pone_hash_table[hash_key].head);
	}
	else
	{
		hlist_for_each_entry(pos,&pone_hash_table[hash_key].head,hlist)
		{
			int ret = pone_cmp(key,key_len,pos->slice_idx);
			if(0 == ret)
			{
				//printk("slice  %lld mem is same with slice %lld\r\n ",slice_idx,pos->slice_idx);
				pone_reverse_add(pos,slice_idx);
				kfree(pone);
				pone = pos;
				break;
			}

			if(ret  > 0)
			{
				hlist_add_before(&pone->hlist,&pos->hlist);
				break;
			}
			pre = pos;
		}

		if(NULL == pos)
		{
			hlist_add_behind(&pone->hlist,&pre->hlist);
		}
	}
    write_unlock(&pone_hash_table[hash_key].rwlock);
    return pone;
}

void  pone_delete_desc(struct pone_desc *pone)
{
    unsigned short hash_key = pone->key;
    write_lock(&pone_hash_table[hash_key].rwlock);
    pone->user_count--;
    if(0 == pone->user_count)
    {
        hlist_del(&pone->hlist);
        kfree(pone);
    }
    write_unlock(&pone_hash_table[hash_key].rwlock);
    return ;
}

int pone_delete(char *key,int key_len,unsigned long slice_idx)
{
    unsigned int crc = calc_checksum(key,key_len);
    unsigned short hash_key = crc;
    struct pone_desc *pos = NULL;

    write_lock(&pone_hash_table[hash_key].rwlock);

    hlist_for_each_entry(pos,&pone_hash_table[hash_key].head,hlist)
    {
        int ret = pone_cmp(key,key_len,pos->slice_idx);
        if(0 == ret)
        {
            if(pos->slice_idx != slice_idx)
            {
                printk("status err in pone_delete\r\n");
                return -1;
            }

            pos->user_count = 0;
            if(0 == pos->user_count)
            {
                //printk("delete pone slice %lld\r\n",slice_idx);
				hlist_del(&pos->hlist);
                kfree(pos);
            }
            break;
        }
    }

    write_unlock(&pone_hash_table[hash_key].rwlock);
    return 0; 
}
