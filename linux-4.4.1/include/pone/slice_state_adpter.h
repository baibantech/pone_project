#ifndef __SLICE_STATE_ADPTER_H__
#define __SLICE_STATE_ADPTER_H__
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/rmap.h>
#include <linux/slab.h>
#include <pone/slice_state.h>
#include <pone/lf_rwq.h>
#include <pone/pone.h>
#define SLICE_ERR -1
#define SLICE_OK 0
#define SLICE_STATUS_ERR 1
#define SLICE_LOCK_ERR 2
#define SLICE_REVERSE_LOCK_ERR 3

extern int collect_sys_slice_info(slice_state_control_block *cblock);

extern int make_slice_wprotect(unsigned long slice_idx);

extern int free_slice(unsigned long slice_idx);

extern int inc_reverse_count(unsigned long slice_idx);

extern int change_reverse_ref(unsigned long slice_idx,unsigned long new_slie);

extern bool free_slice_check(unsigned long slice_idx);
extern void slice_que_reader_init(void);
extern void slice_merge_timer_init(unsigned long ms);
extern void process_que_interrupt(void);
extern int slice_file_write_proc(struct address_space *space,unsigned long offset);
struct page* slice_file_replace_proc(struct address_space *mapping,unsigned long index,struct page *page);
DECLARE_PER_CPU(lfrwq_reader,int_slice_que_reader);
DECLARE_PER_CPU(lfrwq_reader,int_slice_watch_que_reader);
DECLARE_PER_CPU(lfrwq_reader,int_slice_deamon_que_reader);
DECLARE_PER_CPU(int,in_que_cnt);
DECLARE_PER_CPU(int,in_watch_que_cnt);
DECLARE_PER_CPU(int,volatile_cnt);
extern struct address_space *check_space ;
#endif
