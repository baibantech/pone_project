#ifndef _SPLITTER_VECTOR_H
#define _SPLITTER_VECTOR_H

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/percpu.h>
#include <linux/list.h>
#include "spt_thread.h"
#define SPT_VEC_RIGHT 0
#define SPT_VEC_DATA 1
#define SPT_VEC_SIGNPOST 2
#define SPT_VEC_SYS_FLAG_DATA 1


#define SPT_VEC_VALID 0
#define SPT_VEC_INVALID 1
#define SPT_VEC_RAW 2


#define SPT_VEC_SIGNPOST_BIT 15

#define SPT_VEC_SIGNPOST_MASK ((1ul<<SPT_VEC_SIGNPOST_BIT)-1)

#if 0
#define SPT_VEC_POS_INDEX_BIT 8
#define SPT_VEC_POS_INDEX_MASK 0x3F00
#define SPT_VEC_POS_MULTI_MASK 0xFF

#define spt_get_pos_index(x) ((x&SPT_VEC_POS_INDEX_MASK)>>SPT_VEC_POS_INDEX_BIT)
#define spt_get_pos_multi(x) (x&SPT_VEC_POS_MULTI_MASK)
#define spt_set_pos(pos,index,multi) (pos = (index << SPT_VEC_POS_INDEX_BIT)|(multi))


#define POW256(x) (1<<(8*x))
#endif

#define spt_set_vec_invalid(x) (x.status = SPT_VEC_INVALID)
#define spt_set_right_flag(x) (x.type = SPT_VEC_RIGHT)
#define spt_set_data_flag(x) (x.type = SPT_VEC_DATA)

#define SPT_PTR_MASK (0x00000000fffffffful)
#define SPT_PTR_VEC (0ul)
#define SPT_PTR_DATA (1ul)

#define SPT_BUF_TICK_BITS 18
#define SPT_BUF_TICK_MASK ((1<<SPT_BUF_TICK_BITS)-1)

#define SPT_PER_THRD_RSV_CNT 5
#define SPT_BUF_VEC_WATERMARK 200
#define SPT_BUF_DATA_WATERMARK 100

#define spt_data_free_flag(x) ((x)->rsv&0x1)
#define spt_set_data_free_flag(x,y) ((x)->rsv |= y)
#define spt_set_data_not_free(x) ((x)->rsv &= 0xfffe)


#define SPT_SORT_ARRAY_SIZE (4096*8)
#define SPT_DVD_CNT_PER_TIME (100)
#define SPT_DVD_THRESHOLD_VA (1000000)
#define SPT_DVD_MOVE_TIMES (SPT_DVD_THRESHOLD_VA/(2*SPT_DVD_CNT_PER_TIME))
#define SPT_DATA_HIGH_WATER_MARK (1600000)

typedef char*(*spt_cb_get_key)(char *);
typedef void (*spt_cb_free)(char *);
typedef void (*spt_cb_end_key)(char *);
typedef char*(*spt_cb_construct)(char *);



typedef struct
{
    int idx;
    int size;//数据大小
    int cnt;//实际数据个数
    char *array[0];//pdh->pdata
}spt_sort_info;

typedef struct
{
//    unsigned long long flag:1;
    unsigned long long tick:18;
    unsigned long long id:23;
    unsigned long long next:23;
}spt_buf_list;

typedef struct chunk_head
{
    unsigned short vec_head;
    unsigned short vec_free_head;
    unsigned short blk_free_head;
    unsigned short dblk_free_head;
}CHK_HEAD;

typedef struct block_head
{
    unsigned int magic;
    unsigned int next;
}block_head_t;

typedef struct db_head_t
{
    unsigned int magic;
    unsigned int next;
}db_head_t;

typedef struct spt_data_hd
{
    volatile int ref;/*引用计数*/
    u16 size;
    u16 rsv;
//    int rank;/*for test*/
    char *pdata;
}spt_dh;


typedef struct spt_vec_t
{
    union
    {
        volatile unsigned long long val;
        struct 
        {
            volatile unsigned long long status:      2;
            volatile unsigned long long type:       1;
            volatile unsigned long long pos:        15;
            volatile unsigned long long down:       23;
            volatile unsigned long long rd:         23;    
        };
        struct 
        {
            volatile unsigned long long dummy_flag:         3;
            volatile unsigned long long ext_sys_flg:        6;
            volatile unsigned long long ext_usr_flg:        6;
            volatile unsigned long long idx:                24;
            volatile long long dummy_rd:           23;
        };
    };
}spt_vec;

typedef struct cluster_head
{
    struct list_head c_list;
    int vec_head;
    volatile unsigned int vec_free_head;
    volatile unsigned int dblk_free_head;
    volatile unsigned int blk_free_head;

    spt_vec *pstart;
    u64 startbit;
    u64 endbit;
    int is_bottom;
    volatile unsigned int data_total;

    unsigned int pg_num_max;
    unsigned int pg_num_total;
    unsigned int pg_cursor;
    unsigned int blk_per_pg_bits;
    unsigned int pg_ptr_bits;
    unsigned int blk_per_pg;
    unsigned int db_per_blk;
    unsigned int vec_per_blk;    
    
    unsigned int free_blk_cnt;
    unsigned int free_vec_cnt;
    unsigned int free_dblk_cnt;
    unsigned int used_vec_cnt;
    unsigned int used_dblk_cnt;
    unsigned int buf_db_cnt;
    unsigned int buf_vec_cnt;
    unsigned int thrd_total;
    
    spt_thrd_data *thrd_data;

    int status;
    unsigned int debug;
    spt_cb_get_key get_key;
    spt_cb_get_key get_key_in_tree;
    //void (*freedata)(char *p, u8 flag);
    spt_cb_free freedata;
    spt_cb_end_key get_key_end;
    spt_cb_end_key get_key_in_tree_end;
    spt_cb_construct construct_data;
    volatile char *pglist[0];
}cluster_head_t;

typedef struct
{
    cluster_head_t *pdst_clst;
    cluster_head_t *puclst;
    int divided_times;
    int down_is_bottom;
    char **up_vb_arr;
    char **down_vb_arr;
}spt_divided_info;


typedef struct dh_extent_t
{
    unsigned int hang_vec;
    cluster_head_t *plower_clst;
    char *data;
}spt_dh_ext;


typedef struct vec_head
{
    unsigned int magic;
    unsigned int next;
}vec_head_t;

typedef struct vec_cmpret
{
    /*smalldata first set小数在比较向量范围内，如果全零则为比较范围外的下一位?*/
    /*pos位之前比较结果为相同的位可以为零也可以不为零*/
    u64 smallfs;    
//    u64 bigfs;         /*bigdata first set 相对于开始比较的位的位置，没用?*/
    u64 pos;        /*比较到哪一位开始不等，如果两数相等，则指向比较范围外的下一位?*/
    u32 finish;
}vec_cmpret_t;

typedef struct spt_query_info
{
    spt_vec *pstart_vec;
    u64 signpost;
    /****
    对于插入操作 
    若此数据是新数据，返回时将data置空；
    若是重复数据不改变data指针，由调用者释放内存。
    ****/
    char *data;
    u64 endbit;
    u32 startid;
    u8 op;
    u8 data_type;
    u8 free_flag;//删除时标记是否释放数据内存。
    char cmp_result;
    u32 db_id;//查询\插入\删除的返回值
    u32 ref_cnt;//返回值，副本数量
    u32 vec_id;//返回值，vec_id bit之前的都相等
    spt_cb_get_key get_key;
    spt_cb_end_key get_key_end;
}query_info_t;


typedef struct spt_insert_info
{
    spt_vec *pkey_vec;
    u64 key_val;
    u64 signpost;
    u64 startbit;
    u64 fs;
    u64 cmp_pos;
    u64 endbit;//not include
    u32 dataid;
    u32 key_id;
    //for debug
    char *pcur_data;
    vec_cmpret_t cmpres;
}insert_info_t;



typedef struct spt_free_vector_node
{
    int id;
    struct spt_free_vector_node    *next;
}spt_free_vec_node;

typedef struct spt_del_data_node_st
{
    int id;
    struct spt_del_data_node_st    *next;
}spt_del_data_node;


typedef struct spt_seek_path_node
{    
    char                            direction;
    int                                vec_id;
    char*                            pvec;
    struct spt_seek_path_node*        next;
}spt_path_node;
#if 0
typedef struct spt_seek_path
{
    int                up_first;/*上一层的第一个向量(向量关系的上一层，不是数据关系)*/
    int             up_2_del;/*拐点*/
    int                del_first;
    int                del_2_down;/*拐点*/
    int                down;/*数据关系紧挨着的下一层的第一个向量*/
//    spt_path_node*        next;
}spt_path;
#else
typedef struct spt_seek_path2
{
    int up_2_del;/*拐点*/
    int del_2_down;/*拐点*/
    int down;/*数据关系紧挨着的下一层的第一个向量*//*应该解释为和当前数据有直接向量关联关系*/ 
    int direction;
}spt_path2;

typedef struct spt_seek_path
{
    spt_vec *pkey_vec[2];
//    u64 key_val[2];
    u64 signpost[2];
}spt_path;

#endif

typedef struct spt_stack_st
{
    void **p_top;    /* 栈顶指针 */
    void **p_bottom; /* 栈底指针 */
    int stack_size;    /* 栈大小 */
}spt_stack;

typedef  struct spt_vec_full_t
{
    int down;
    int right;
    int data;
    long long pos;
}spt_vec_f;

typedef struct spt_traversal_info_st
{
    spt_vec_f vec_f;
    long long signpost;
}travl_info;

typedef struct spt_xy_st
{
    long long x;
    long long y;
}spt_xy;

typedef struct spt_outer_travl_info_st
{
    int pre;
    int cur;
    int data;
    spt_xy xy_pre;
    struct spt_outer_travl_info_st *next;
}travl_outer_info;

typedef struct spt_travl_q_st
{
    travl_outer_info *head;
    travl_outer_info *tail;
}travl_q;

typedef struct spt_test_file_head
{
    int data_size;
    int data_num;
    char rsv[24];
}test_file_h;

typedef struct spt_dbg_info_st
{
    u64 oom_no_pgs;
    u64 oom_no_db;
    u64 oom_no_vec;

    u64 find_data;
    u64 refind_start;
    u64 refind_forward;

    u64 insert_ok;
    u64 insert_fail;
    u64 delete_ok;
    u64 delete_fail;

    u64 ret_wait;
    u64 ret_nomem;
    u64 thread_exit;
    u64 thread_get_token;
}spt_dbg_info;

//#define DBLK_BITS 3
#define DATA_SIZE g_data_size
#define RSV_SIZE 2
#define DBLK_SIZE (sizeof(spt_dh))
#define VBLK_BITS 3
#define VBLK_SIZE (1<<VBLK_BITS)
#define DATA_BIT_MAX (DATA_SIZE*8)

//#define vec_id_2_ptr(pchk, id)    ((char *)pchk+id*VBLK_SIZE);

unsigned int vec_alloc(cluster_head_t *pclst, spt_vec **vec);
void vec_free(cluster_head_t *pcluster, int id);
void vec_list_free(cluster_head_t *pcluster, int id);
void db_free(cluster_head_t *pcluster, int id);

extern spt_thrd_t *g_thrd_h;
extern spt_dbg_info g_dbg_info;
int spt_get_errno(void);
extern cluster_head_t *pgclst;
extern unsigned int sd_thrd_errno[128];
DECLARE_PER_CPU(u32,local_thrd_id);
DECLARE_PER_CPU(int,local_thrd_errno);
DECLARE_PER_CPU(int,process_enter_check);
//#define g_thrd_id     per_cpu(local_thrd_id,smp_processor_id())
//#define g_thrd_errno  per_cpu(local_thrd_errno,smp_processor_id())
#define g_thrd_id     smp_processor_id()
#define g_thrd_errno  sd_thrd_errno[smp_processor_id()]
#endif
