#ifndef __SLICE_STATE_H__
#define __SLICE_STATE_H__
#include <pone/lf_rwq.h>
#define SLICE_STATE_BITS (3)
#define SLICE_STATE_MASK (0x07ULL)
#define SLICE_STATE_UNIT_BITS (sizeof(unsigned long long )*8)
#define SLICE_NUM_PER_UNIT (SLICE_STATE_UNIT_BITS/SLICE_STATE_BITS)
#define SLICE_NODE_NUM_MAX ((PAGE_SIZE - sizeof(long long)) /sizeof(slice_node_desc))

#define SLICE_ALLOC 1
#define SLICE_FREE  2
#define SLICE_CHANGE 4
#define SLICE_OUT_QUE 5
#define SLICE_OUT_WATCH_QUE 6
#define SLICE_OUT_DEAMON_QUE 7

#define  SLICE_MEM 1
#define  SLICE_FILE 2

#define SLICE_OP_CLUSTER_QUE 1

enum mem_slice_state
{
    SLICE_NULL,
	SLICE_IDLE,
    SLICE_ENQUE,
    SLICE_WATCH,
    SLICE_WATCH_CHG,
    SLICE_FIX,
    SLICE_VOLATILE,
    SLICE_STATE_MAX, 
};

typedef struct slice_node_desc
{
    long long slice_start;
    long long slice_num;
    volatile unsigned long long *slice_state_map;
	long long mem_size;
}slice_node_desc;

typedef struct slice_state_control_block
{
    long long node_num;
    slice_node_desc slice_node[0];
}slice_state_control_block;


typedef struct slice_reverse
{
    int type;
    void *reverse_info;
    unsigned long poff;
    struct list_head list;

}slice_reverse;

#ifdef SLICE_OP_CLUSTER_QUE
int lfrwq_in_cluster_watch_que(void *data,unsigned long  que);
int lfrwq_in_cluster_que(void *data,unsigned long que);
void splitter_wakeup_cluster(void);
#endif


extern slice_state_control_block *global_block;
extern lfrwq_t *slice_que;
extern lfrwq_t *slice_watch_que;
extern lfrwq_t *slice_deamon_que;

extern void slice_per_cpu_count_init(void); 
extern int slice_state_control_init(void);
extern int process_slice_state(unsigned long slice_idx,int op,void *data,unsigned long  que);

extern int process_slice_check(void);
extern int process_slice_file_check(unsigned long ino);

extern int slice_que_resource_init(void);
extern int process_state_que(lfrwq_t *qh,lfrwq_reader *reader,int op);
extern int change_slice_state(unsigned int nid,unsigned long long slice_id,unsigned long long old_state,unsigned long long new_state);
extern int is_pone_init(void);
extern void pre_fix_slice_check(void *data);
void set_deamon_run(void);
int need_wakeup_deamon(void);
void splitter_deamon_wakeup(void);
int slice_deamon_init(void);
extern unsigned long long get_slice_state_by_id(unsigned long slice_idx);
static inline int slice_idx_to_node(unsigned long slice_idx)
{
    int i ;
    for( i = 0; i < global_block->node_num ; i++)
    {
        long long slice_start = global_block->slice_node[i].slice_start;
        long long slice_num = global_block->slice_node[i].slice_num;

        if((slice_idx >= slice_start) && (slice_idx < slice_start + slice_num))
        return i;

    }
    return -1;
}

static inline unsigned long slice_nr_in_node(int nid,unsigned long slice_idx)
{
    return slice_idx - global_block->slice_node[nid].slice_start;
}
extern int ljy_printk_count ;
static inline int check_map_addr(void *addr,unsigned int nid,unsigned long long slice_id)
{
	if((unsigned long )addr < (unsigned long)(void*)global_block->slice_node[nid].slice_state_map)
	{
		return 1;
	}
	if((unsigned long)addr >(unsigned long)(void*)global_block->slice_node[nid].slice_state_map + global_block->slice_node[nid].mem_size)
	{
		return 1;
	}
	return 0;
}
static inline unsigned long long   get_slice_state(unsigned int nid,unsigned long long slice_id)
{
	unsigned long long state_unit = 0;
	int offset;
	state_unit = *(global_block->slice_node[nid].slice_state_map + (slice_id/SLICE_NUM_PER_UNIT));
    offset = slice_id%SLICE_NUM_PER_UNIT;
    return  (state_unit >> (offset * SLICE_STATE_BITS))&SLICE_STATE_MASK;
}

#endif

