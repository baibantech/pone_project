/*************************************************************************
    > File Name: chunk.h
    > Author: yzx
    > Mail: yzx1211@163.com 
    > Created Time: 2015年11月03日 星期二 17时10分34秒
 ************************************************************************/
#ifndef _SPLITTER_CLUSTER_H
#define _SPLITTER_CLUSTER_H



/*Ϊ????Ч?ʣ?blk??СΪ2??????????*/
#define CLST_PG_NUM_MAX ((1<<14) ) /*cluster max = 64m*/
//#define CHUNK_SIZE (1<<16)    /*字节*/
#define PG_BITS 12
#define PG_SIZE (1<<PG_BITS)
#define BLK_BITS 5
#define BLK_SIZE (1<<BLK_BITS)

#define CLST_NDIR_PGS 320        //how much

#if 0
#define CLST_IND_PGS_BITS        6
#define CLST_DIND_PGS_BITS        5
#define CLST_TIND_PGS_BITS        4
#endif
//��??????Ѱַ????Ѱַ1G??Χ??Զ??64M?Ĵش?С???ơ?
#define CLST_IND_PG (CLST_NDIR_PGS)    //index
#define CLST_DIND_PG (CLST_IND_PG+1)    //index
#define CLST_N_PGS (CLST_DIND_PG+1)//pclst->pglist[] max


//#define CLST_TIND_PGS        (CLST_DIND_PGS+1)

#define spt_debug(f, a...)	{ \
					printk ("LFORD DEBUG (%s, %d): %s:", \
						__FILE__, __LINE__, __func__); \
				  	printk (f, ## a); \
					}

#if 0 //for test
static inline char* blk_id_2_ptr(cluster_head_t *pclst, unsigned int id)
{
    int ptrs_bit = pclst->pg_ptr_bits;
    int ptrs = (1 << ptrs_bit);
    u64 direct_pgs = CLST_N_PGS;
    u64 indirect_pgs = CLST_IND_PGS<<ptrs_bit;
    u64 double_pgs = CLST_DIND_PGS<<(ptrs_bit*2);
    int pg_id = id >> pclst->blk_per_pg_bits;
    int offset;
    char *page;

    if(pg_id < direct_pgs)
    {
    }
    else if((pg_id -= direct_pgs) < indirect_pgs)
    {
        offset = CLST_N_PGS + (pg_id >> ptrs_bit);
        page = (char *)pclst->pglist[offset];
        pg_id = pg_id & (ptrs- 1);
    }
    else if((pg_id -= indirect_pgs) < double_pgs)
    {
        offset = CLST_N_PGS + CLST_IND_PGS + (pg_id >> (ptrs_bit*2));
        page = (char *)pclst->pglist[offset];
        offset = (pg_id >> ptrs_bit) & (ptrs - 1);
        page += offset*sizeof(char *);
//        page = (char *)page[offset];
        pg_id = pg_id & (ptrs- 1);
    }
    else if(((pg_id -= double_pgs)>> (ptrs_bit*3)) < CLST_TIND_PGS)
    {
        offset = CLST_N_PGS + CLST_IND_PGS + CLST_DIND_PGS + (pg_id >> (ptrs_bit*3));
        page = (char *)pclst->pglist[offset];
        offset = (pg_id >> (ptrs_bit*2)) & (ptrs - 1);
        page += offset*sizeof(char *);
        //page = (char *)page[offset];
        offset = (pg_id >> ptrs_bit) & (ptrs - 1);
        page += offset*sizeof(char *);
        //page = (char *)page[offset];
        pg_id = pg_id & (ptrs- 1);
    }
    else
    {
        printf("warning: %s: id is too big", __func__);
        return 0;
    }
    
    offset = id & (pclst->blk_per_pg-1);
    return    pclst->pglist[pg_id] + (offset << DBLK_BITS); 
    
}

static inline char* db_id_2_ptr(cluster_head_t *pclst, unsigned int id)
{
#if 0
    char *pblk;
    unsigned int blk_id;

    blk_id = id/pclst->db_per_blk;
    pblk = blk_id_2_ptr(pclst, blk_id);

    return pblk+id%pclst->db_per_blk * DBLK_SIZE;
#endif
    return blk_id_2_ptr(pclst, id/pclst->db_per_blk) + id%pclst->db_per_blk * DBLK_SIZE;
    
}

static inline char* vec_id_2_ptr(cluster_head_t *pclst, unsigned int id)
{
#if 0
    char *pblk;
    unsigned int blk_id;

    blk_id = id/pclst->db_per_blk;
    pblk = blk_id_2_ptr(pclst, blk_id);

    return pblk+id%pclst->db_per_blk * DBLK_SIZE;
#endif
    return blk_id_2_ptr(pclst, id/pclst->vec_per_blk) + id%pclst->vec_per_blk * VBLK_SIZE;
    
}
#else
extern char* blk_id_2_ptr(cluster_head_t *pclst, unsigned int id);
extern char* db_id_2_ptr(cluster_head_t *pclst, unsigned int id);
extern char* vec_id_2_ptr(cluster_head_t *pclst, unsigned int id);
#endif

#define SPT_NULL 0x7fffff
#define SPT_INVALID 0x7ffffe

#define SPT_DIR_START 0
#define SPT_RIGHT 1
#define SPT_DOWN 2

#define SPT_OP_FIND 1
#define SPT_OP_DELETE 2
#define SPT_OP_INSERT 3


#define CLT_FULL 3
#define CLT_NOMEM 2
#define CLT_ERR 1
#define SPT_OK 0
#define SPT_ERR -1
#define SPT_NOMEM -2
#define SPT_WAIT_AMT -3
#define SPT_DO_AGAIN -4
#define SPT_MASKED -5
#define SPT_NOT_FOUND 1

unsigned int db_alloc(cluster_head_t *pclst, spt_dh **db);
cluster_head_t * cluster_init(int is_bottom, 
                              u64 startbit, 
                              u64 endbit, 
                              int thread_num,
                              spt_cb_get_key pf,
                              spt_cb_end_key pf2,
                              spt_cb_free pf_free,
                              spt_cb_construct pf_con);
int vec_free_to_buf(cluster_head_t *pclst, int id, int thread_id);
int db_free_to_buf(cluster_head_t *pcluster, int id, int thread_id);
unsigned int vec_alloc_combo(cluster_head_t *pclst, int thread_id, spt_vec **vec);
unsigned int data_alloc_combo(cluster_head_t *pclst, int thread_id, spt_dh **db);
void vec_free_to_buf_simple(cluster_head_t *pclst, int id, int thread_id);
void db_free_to_buf_simple(cluster_head_t *pclst, int id, int thread_id);
int fill_in_rsv_list(cluster_head_t *pclst, int nr, int thread_id);
int rsv_list_fill_cnt(cluster_head_t *pclst, int thread_id);
int fill_in_rsv_list_simple(cluster_head_t *pclst, int nr, int thread_id);
void cluster_destroy(cluster_head_t *pclst);
void free_data(char *p);
void default_end_get_key(char *p);

void test_vec_alloc_n_times(cluster_head_t *pclst);
void debug_data_print(char *pdata);
extern int g_data_size;

char* insert_data(cluster_head_t *pclst,char* pdata);
int delete_data(cluster_head_t *pclst,char* pdata);
void set_data_size(int size);

cluster_head_t *spt_cluster_init(u64 startbit, 
                              u64 endbit, 
                              int thread_num,
                              spt_cb_get_key pf,
                              spt_cb_end_key pf2,
                              spt_cb_free pf_free,
                              spt_cb_construct pf_con);


spt_thrd_t *spt_thread_init(int thread_num);
void spt_set_thrd_id(int val);

int spt_thread_start(int thread);
void spt_thread_exit(int thread);
#endif

