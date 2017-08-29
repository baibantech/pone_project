#ifndef _SPT_THREAD_H
#define _SPT_THREAD_H

#define SPT_BWMAP_ALL_ONLINE 0xfffffffffffffffful

typedef struct
{
    unsigned int thrd_id;
    unsigned int vec_cnt;
    unsigned int vec_list_cnt;
    unsigned int data_cnt;
    unsigned int data_list_cnt;
    unsigned int vec_free_in;
    unsigned int vec_alloc_out;
    unsigned int data_free_in;
    unsigned int data_alloc_out;
    unsigned int rsv_cnt;
    unsigned int rsv_list;
}spt_thrd_data;

typedef struct 
{
    unsigned int thrd_total;
    volatile unsigned int tick;
    volatile unsigned long long black_white_map;
    volatile unsigned long long online_map;
    spt_thrd_data thrd_data[0];
}spt_thrd_t;


extern spt_thrd_t *g_thrd_h;

spt_thrd_t *spt_thread_init(int thread_num);

int spt_thread_start(int thread);

void spt_thread_exit(int thread);

void spt_thread_wait(int n, int thread);

#endif
