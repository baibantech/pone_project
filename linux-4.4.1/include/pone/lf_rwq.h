#ifndef _FREERWQ_H
#define _FREERWQ_H

#include <linux/atomic.h>

typedef struct {
	volatile u64 r_idx;
    volatile u64 w_idx;
    volatile u64 r_max_idx_period;
    int len;
    u32 q_pow;
    u32 blk_len;
    u32 blk_pow;
    u32 readers;
    u32 blk_cnt;
    int r_permit;
    int r_pmt_sgest;//suggest
    u64 dbg_r_total;
    u64 dbg_p_total;
    u64 dbg_get_pmt_total;
    
    volatile u64 *r_cnt;
    volatile u64 q[0];
} lfrwq_t;

typedef struct {
    u32 local_pmt;
    u32 r_cnt;
    u32 local_blk;
    u64 local_idx;
    
}lfrwq_reader;

typedef int (*lf_inq)(lfrwq_t* qh, void *data);

//int lfrwq_deq(lfrwq_t* qh, u32 r_permit, processfn *callback);

int lfrwq_get_token(lfrwq_t* qh);

int lfrwq_return_token(lfrwq_t* qh);

u64 lfrwq_deq(lfrwq_t* qh, void **ppdata);

int lfrwq_inq(lfrwq_t* qh, void *data);

int lfrwq_inq_m(lfrwq_t* qh, void *data);

int lfrwq_get_rpermit(lfrwq_t* qh);

void lfrwq_add_rcnt(lfrwq_t* qh, u32 total, u32 cnt_idx);

lfrwq_t* lfrwq_init(u32 q_len, u32 blk_len, u32 readers);
int lfrwq_soft_inq(lfrwq_t *qh,u64 w_idx);
u64 lfrwq_deq_by_idx(lfrwq_t *qh,u64 idx,void **ppdata);
u64 lfrwq_get_blk_idx(lfrwq_t* qh,u64 idx);


u64 lfrwq_alloc_r_idx(lfrwq_t *qh);

u64 lfrwq_get_w_idx(lfrwq_t *qh);
void lfrwq_set_r_max_idx(lfrwq_t *qh,u64 w_idx);
u64 lfrwq_get_r_max_idx(lfrwq_t *qh);


#define lfrwq_debug(f, a...)	{ \
					printk ("LFRWQ DEBUG (%s, %d): %s:", \
						__FILE__, __LINE__, __func__); \
				  	printk (f, ## a); \
					}



#endif

