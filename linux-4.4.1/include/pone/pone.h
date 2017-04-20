#ifndef __PONE_H__
#define __PONE_H__
#include <linux/kernel.h>
#include <linux/spinlock.h>
struct pone_desc
{
    spinlock_t lock;
    unsigned short key;
    int    user_count;
    struct hlist_node hlist;
    struct list_head ref_list;
    unsigned long slice_idx;
};

struct pone_hash_head
{
	rwlock_t rwlock;
	struct hlist_head head;
};

int pone_hash_table_init(int size);

struct pone_desc *pone_insert(char *key,int key_len,unsigned long slice_idx);

void pone_delete_desc(struct pone_desc *pone);

int pone_delete(char *key,int key_len,unsigned long slice_idx);

#endif
