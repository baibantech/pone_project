#if 0
#define _GNU_SOURCE
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <vector.h>
#include <chunk.h>
#include <unistd.h>

#include <sched.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/ipc.h>  
#include <sys/shm.h>  
#include <errno.h>
#include <signal.h>
#include<sys/wait.h>
#include <sys/stat.h> /* For mode constants */
#include <fcntl.h> /* For O_* constants */
#endif

#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/highmem.h>
#include "vector.h"
#include "chunk.h"
#include "pone_time.h"
extern void slice_data_cmp(void *page,unsigned int lineno);
//DEFINE_PER_CPU(u32 ,local_thrd_id);
//DEFINE_PER_CPU(int, local_thrd_errno);
unsigned int sd_thrd_errno[128] = {0};
cluster_head_t *pgclst;
spt_thrd_t *g_thrd_h;
cluster_head_t *ptopclst;
cluster_head_t *plow_clst;
spt_dbg_info g_dbg_info = {0};
#define DBG_DATA_NUM 10
u64 g_data[DBG_DATA_NUM]={0xee,0xbe,0xba,0xb6,0xae,
                          0xca,0x91,0x8a,0x36,0x06};
int dbg_switch=0;

u64 g_test[DBG_DATA_NUM]={0xe0,0xb0,0xbb,0xbd,0xaf,
                          0xc4,0x9f,0x01,0x60,0x08};
#define TEST_DATA_MAX (100)
u64 *gd;

u64 find_fs(char *a, u64 start, u64 len);
int diff_identify(char *a, char *b,u64 start, u64 len, vec_cmpret_t *result);
void spt_thread_wait(int n, int thread);
int find_data(cluster_head_t *pclst, query_info_t *pqinfo);
int do_insert_data(cluster_head_t *pclst, char *pdata, spt_cb_get_key pf, spt_cb_end_key pf2);
int do_delete_data_no_free(cluster_head_t *pclst, char *pdata, spt_cb_get_key pf, spt_cb_end_key pf2);
int do_delete_data(cluster_head_t *pclst, char *pdata, spt_cb_get_key pf, spt_cb_end_key pf2);

void spt_set_errno(int val)
{
    g_thrd_errno = val;
}
int spt_get_errno(void)
{
    return g_thrd_errno;
}
#if 0
void spt_set_thrd_id(int val)
{
	g_thrd_id = val;
}
#endif
int spt_get_thrd_id(void)
{
	return g_thrd_id;
}

void spt_bit_clear(u8 *p, u64 start, u64 len)
{
    u8 bitstart, bitend;
    s64 lenbyte;
    u8 *acstart;

    bitstart = start%8;
    bitend = (bitstart + len)%8;
    acstart = p + start/8;
    lenbyte =  (bitstart + len)/8;

    if(bitstart != 0)
    {
        *acstart = *acstart >> (8-bitstart) << (8-bitstart);
        acstart++;
    }
    while(lenbyte > 0)
    {
        *acstart = 0;
        acstart++;
        lenbyte--;
    }
    if(bitend != 0)
    {
        *acstart = *acstart << bitend;
        *acstart = *acstart >> bitend;
    }
    return;
}

void spt_bit_cpy(u8 *to, const u8 *from, u64 start, u64 len)
{
    u8 bitstart, bitend;
    s64 lenbyte;
    u8 uca, ucb;
    u8 *acstart;
    u8 *bcstart;

    bitstart = start%8;
    bitend = (bitstart + len)%8;
    acstart = to + start/8;
    bcstart = (u8 *)from + start/8;
    lenbyte =  (bitstart + len)/8;

    if(bitstart != 0)
    {
        uca = *acstart >> (8-bitstart) << (8-bitstart);
        ucb = *bcstart << bitstart;
        ucb = ucb >> bitstart;
        *acstart = uca | ucb;
        acstart++;
        bcstart++;
        lenbyte--;
    }

    while(lenbyte > 0)
    {
        *acstart = *bcstart;
        acstart++;
        bcstart++;
        lenbyte--;
    }

    if(bitend != 0)
    {
        uca = *acstart << bitend;
        uca = uca >> bitend;
        ucb = *bcstart >> (8-bitend) << (8-bitend);
        *acstart = uca | ucb;
        acstart++;
        bcstart++;
    }
    return;
}

void spt_stack_init(spt_stack *p_stack, int size)
{
    p_stack->p_bottom = (void **)kmalloc(size * sizeof(void *),GFP_ATOMIC);
   
    if (p_stack->p_bottom == NULL)
    {
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        return;
    }
    p_stack->p_top = p_stack->p_bottom;
    p_stack->stack_size = size;

    return;
}

int spt_stack_full(spt_stack *p_stack)
{  
    return p_stack->p_top - p_stack->p_bottom >= p_stack->stack_size-1;
}

int spt_stack_empty(spt_stack *p_stack)
{
    return p_stack->p_top <= p_stack->p_bottom;
}

void spt_stack_push(spt_stack *p_stack, void *value)
{
    spt_stack *p_tmp = (spt_stack *)p_stack->p_bottom;
    
    if (spt_stack_full(p_stack))
    {      
        p_stack->p_bottom = 
        (void **)krealloc(p_stack->p_bottom, 2*p_stack->stack_size*sizeof(void *),GFP_ATOMIC);
        
        if (!p_stack->p_bottom)
        {
            kfree(p_tmp);
            printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
            return;
        }
        p_stack->stack_size = 2*p_stack->stack_size;
    }
    (p_stack->p_top)++;
    *(p_stack->p_top) = value;
    
    return;
 }

void* spt_stack_pop(spt_stack *p_stack)
{
    void *value = 0;
   
    if (spt_stack_empty(p_stack))
    {
        return (void *)-1;
    }
    value = *(p_stack->p_top--);
   
    return value;
}

void spt_stack_destroy(spt_stack *p_stack)
{
    kfree(p_stack->p_bottom);
//    kfree(p_stack);
    p_stack->stack_size = 0;
    p_stack->p_bottom = NULL;
    p_stack->p_top = NULL;
    p_stack = NULL;    
   
    return;
}


u64 ullfind_firt_zero(u64 dword)
{
    return 0;
}

u64 ullfind_firt_set(u64 dword)
{
    int i;
    for(i=63; i>=0; i--)
    {
        if(dword >> i != 0)
            return 63-i;
    }

    return 64;

}
u32 ulfind_firt_zero(u32 word)
{
    return 0;
}
u64 uifind_firt_set(u32 word)
{
    int i;
    for(i=31; i>=0; i--)
    {
        if(word >> i != 0)
            return 31-i;
    }

    return 32;

}
u16 usfind_firt_zero(u16 word)
{
    return 0;
}
u64 usfind_firt_set(u16 word)
{
    int i;
    for(i=15; i>=0; i--)
    {
        if(word >> i != 0)
            return 15-i;
    }

    return 16;

}
u8 ucfind_firt_zero(u8 byte)
{
    return 0;
}
u64 ucfind_firt_set(u8 byte)
{
    int i;
    for(i=7; i>=0; i--)
    {
        if(byte >> i != 0)
            return 7-i;
    }
    return 8;
}

char *spt_bottom_get_key(char *pdata)
{
    return pdata;
}

char *spt_upper_construct_data(char *pkey)
{
    spt_dh_ext *pext_head;
    char *pdata;

    pext_head = (spt_dh_ext *)kmalloc(sizeof(spt_dh_ext)+DATA_SIZE,GFP_ATOMIC);
    if(pext_head == NULL)
        return NULL;
    
    pdata = (char *)(pext_head + 1);
    pext_head->data = pdata;
    pext_head->plower_clst = NULL;
    memcpy(pdata, pkey, DATA_SIZE);
    return (char *)pext_head;
}

char *spt_bottom_construct_data(char *pkey)
{
    char *pdata;

    pdata = (char *)kmalloc(DATA_SIZE,GFP_ATOMIC);
    if(pdata == NULL)
        return NULL;
    memcpy(pdata, pkey, DATA_SIZE);
    return (char *)pdata;
}

char *get_real_data(cluster_head_t *pclst, char *pdata)
{
    spt_dh_ext *ext_head;

    if(pclst->is_bottom)
        return pdata;
    ext_head = (spt_dh_ext *)pdata;
    return ext_head->data;
}

int get_data_id(cluster_head_t *pclst, spt_vec* pvec)
{
    spt_vec *pcur, *pnext, *ppre;
    spt_vec tmp_vec, cur_vec, next_vec;
    //u8 direction;
    u32 vecid;
    int ret;

get_id_start:
    ppre = 0;
    cur_vec.val = pvec->val;
    pcur = pvec;
    if(cur_vec.status == SPT_VEC_RAW)
    {
        smp_mb();
        cur_vec.val = pvec->val;
        if(cur_vec.status == SPT_VEC_RAW)
        {
            return SPT_DO_AGAIN;
        }
    }
    if(cur_vec.status == SPT_VEC_INVALID)
    {
        return SPT_DO_AGAIN;
    }

    while(1)
    {
        if(cur_vec.type == SPT_VEC_RIGHT)
        {
            pnext = (spt_vec *)vec_id_2_ptr(pclst,cur_vec.rd);
            next_vec.val = pnext->val;
            if(next_vec.status == SPT_VEC_VALID
                && next_vec.type != SPT_VEC_SIGNPOST
                && next_vec.down != SPT_NULL)
            {
                ppre = pcur;
                pcur = pnext;
                cur_vec.val = next_vec.val;
                continue;
            }
            if(next_vec.status == SPT_VEC_RAW)
            {
                smp_mb();
                next_vec.val = pnext->val;
                if(next_vec.status == SPT_VEC_RAW)
                {
                    goto get_id_start;
                }
            }           
            if(next_vec.status == SPT_VEC_INVALID)
            {
                tmp_vec.val = cur_vec.val;
                vecid = cur_vec.rd;
                tmp_vec.rd = next_vec.rd;
                if(next_vec.type == SPT_VEC_SIGNPOST)
                {
                    tmp_vec.type = next_vec.ext_sys_flg;
                }
                else if(next_vec.type == SPT_VEC_DATA)
                {
                    if(next_vec.down == SPT_NULL)
                    {
                        spt_set_data_flag(tmp_vec);
                        if(next_vec.rd == SPT_NULL && pcur!=pclst->pstart)
                            tmp_vec.status = SPT_VEC_INVALID;
                    }
                    else
                    {
                        tmp_vec.rd = next_vec.down;
                    }                
                }
                else
                {
                    ;
                }
                //cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                //if(cur_vec.val == tmp_vec.val)//delete succ
                if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))
                {
                    ret = vec_free_to_buf(pclst, vecid, g_thrd_id);
                    if(ret != SPT_OK)
                        return ret;
                }
                cur_vec.val = pcur->val;
                if(cur_vec.status != SPT_VEC_VALID)
                {
                #if 0
                    if(ppre == NULL)
                        goto get_id_start;
                    else
                    {
                        cur_vec.val = ppre->val;
                        if(cur_vec.status == SPT_VEC_VALID)
                            goto get_id_start;
                        else
                        {
                            pcur = ppre;
                            ppre = NULL;
                            continue;
                        }
                    }

                    continue;
                #endif
                    goto get_id_start;
                }
                continue;
            }
            
            //对于一个向量的右结点:
                //如果是路标，不能是SPT_VEC_SYS_FLAG_DATA
                //如果是向量，必然有down
            if(next_vec.type == SPT_VEC_SIGNPOST)
            {
                if(next_vec.ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
                {
                    tmp_vec.val = next_vec.val;
                    tmp_vec.status = SPT_VEC_INVALID;
                    atomic64_cmpxchg((atomic64_t *)pnext, next_vec.val, tmp_vec.val);
                    //set invalid succ or not, refind from cur
                    cur_vec.val = pcur->val;
                    if((cur_vec.status == SPT_VEC_INVALID))
                    {
                        if(ppre == NULL)
                            goto get_id_start;
                        else
                        {
                            cur_vec.val = ppre->val;
                            if(cur_vec.status == SPT_VEC_INVALID)
                                goto get_id_start;
                            else
                            {
                                pcur = ppre;
                                ppre = NULL;
                                continue;
                            }
                        }
                    }
                    continue;
                }
            }
            else
            {
                if(next_vec.down == SPT_NULL)
                {
                    tmp_vec.val = next_vec.val;
                    tmp_vec.status = SPT_VEC_INVALID;
                    atomic64_cmpxchg((atomic64_t *)pnext, next_vec.val, tmp_vec.val);
                    //set invalid succ or not, refind from cur
                    cur_vec.val = pcur->val;
                    if((cur_vec.status != SPT_VEC_VALID))
                    {
                    #if 0
                        if(ppre == NULL)
                            goto get_id_start;
                        else
                        {
                            cur_vec.val = ppre->val;
                            if(cur_vec.status != SPT_VEC_VALID)
                                goto get_id_start;
                            else
                            {
                                pcur = ppre;
                                ppre = NULL;
                                continue;
                            }
                        }
                    #endif
                        goto get_id_start;
                    }
                    continue;
                }
            }
        }
        else if(cur_vec.type == SPT_VEC_DATA)
        {
            return cur_vec.rd;
        }
        //cur是路标，方向肯定是right,遍历过程中不会对方向是down的路标调用此接口
        else
        {
            if(cur_vec.ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
            {
                tmp_vec.val = cur_vec.val;
                tmp_vec.status = SPT_VEC_INVALID;
                cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                /*set invalid succ or not, refind from ppre*/;
                if(ppre == NULL)
                    goto get_id_start;
                else
                {
                    cur_vec.val = ppre->val;
                    if(cur_vec.status == SPT_VEC_INVALID)
                        goto get_id_start;
                    else
                    {
                        pcur = ppre;
                        ppre = NULL;
                        continue;
                    }
                }
            }
            pnext = (spt_vec *)vec_id_2_ptr(pclst,cur_vec.rd);
            next_vec.val = pnext->val;
            if(next_vec.status == SPT_VEC_RAW)
            {
                smp_mb();
                next_vec.val = pnext->val;
                if(next_vec.status == SPT_VEC_RAW)
                {
                    goto get_id_start;
                }
            }
            if(next_vec.status == SPT_VEC_INVALID)
            {
                tmp_vec.val = cur_vec.val;
                vecid = cur_vec.rd;
                tmp_vec.rd = next_vec.rd;
                if(next_vec.type == SPT_VEC_SIGNPOST)
                {
                    tmp_vec.ext_sys_flg = next_vec.ext_sys_flg;
                }
                else if(next_vec.type == SPT_VEC_DATA)
                {
                    if(next_vec.down == SPT_NULL)
                    {
                        tmp_vec.ext_sys_flg = SPT_VEC_SYS_FLAG_DATA;
                    }
                    else
                    {
                        tmp_vec.rd = next_vec.down;
                    }                
                }
                else
                {
                    ;
                }
                //cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                //if(cur_vec.val == tmp_vec.val)//delete succ
                if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))//delete succ
                {
                    ret = vec_free_to_buf(pclst, vecid, g_thrd_id);
                    if(ret != SPT_OK)
                        return ret;
                    continue;
                }
                else//delete fail
                {
                    cur_vec.val = pcur->val;
                    if(cur_vec.status == SPT_VEC_INVALID)
                    {
                        if(ppre == NULL)
                            goto get_id_start;
                        else
                        {
                            cur_vec.val = ppre->val;
                            if(cur_vec.status == SPT_VEC_INVALID)
                                goto get_id_start;
                            else
                            {
                                pcur = ppre;
                                ppre = NULL;
                                continue;
                            }
                        }
                    }
                    continue;
                }                    
            }
            //对于一个路标结点:
                //next为路标，是不合理结构。
            if(next_vec.type == SPT_VEC_SIGNPOST)
            {
                tmp_vec.val = cur_vec.val;
                tmp_vec.status = SPT_VEC_INVALID;
                cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                /*set invalid succ or not, refind from ppre*/
                if(ppre == NULL)
                    goto get_id_start;
                else
                {
                    cur_vec.val = ppre->val;
                    if(cur_vec.status == SPT_VEC_INVALID)
                        goto get_id_start;
                    else
                    {
                        pcur = ppre;
                        ppre = NULL;
                        continue;
                    }
                }
            }
            else
            {
                if(next_vec.down == SPT_NULL)
                {
                    tmp_vec.val = next_vec.val;
                    tmp_vec.status = SPT_VEC_INVALID;
                    atomic64_cmpxchg((atomic64_t *)pnext, next_vec.val, tmp_vec.val);
                    //set invalid succ or not, refind from cur
                    cur_vec.val = pcur->val;
                    if((cur_vec.status == SPT_VEC_INVALID))
                    {
                        if(ppre == NULL)
                            goto get_id_start;
                        else
                        {
                            cur_vec.val = ppre->val;
                            if(cur_vec.status == SPT_VEC_INVALID)
                                goto get_id_start;
                            else
                            {
                                pcur = ppre;
                                ppre = NULL;
                                continue;
                            }
                        }
                    }
                    continue;
                }
            }            
        }
        ppre = pcur;
        pcur = pnext;
        cur_vec.val = next_vec.val;
    }

}

int do_insert_dsignpost_right(cluster_head_t *pclst, insert_info_t *pinsert, char *new_data)
{
    spt_vec tmp_vec, *pvec_a;  
    u32 dataid, vecid_a;
    int ret;
    spt_dh *pdh;


    dataid = data_alloc_combo(pclst, g_thrd_id, &pdh);
    if(pdh == 0)
    {
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_db);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        return SPT_NOMEM;
    }
    pdh->ref = 1;
    pdh->pdata = new_data;

    tmp_vec.val = pinsert->key_val;
//    signpost = pinsert->signpost;

    vecid_a = vec_alloc_combo(pclst, g_thrd_id, &pvec_a);
    if(pvec_a == 0)
    {
        /*yzx释放资源， 申请新块，拆分*/
        spt_set_data_not_free(pdh);
        db_free_to_buf(pclst, dataid, g_thrd_id);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        return SPT_NOMEM;
    }
    pvec_a->val = 0;
    pvec_a->type = SPT_VEC_DATA;
    pvec_a->pos = 0;
    pvec_a->rd = dataid;
    pvec_a->down = tmp_vec.rd;

    tmp_vec.rd = vecid_a;
    smp_mb();
    if(pinsert->key_val == atomic64_cmpxchg((atomic64_t *)pinsert->pkey_vec, 
                        pinsert->key_val, tmp_vec.val))
    {
        return dataid;
    }
    else
    {
        spt_set_data_not_free(pdh);
        db_free_to_buf_simple(pclst, dataid, g_thrd_id);
        vec_free_to_buf_simple(pclst, vecid_a, g_thrd_id);
        ret = fill_in_rsv_list(pclst, 2, g_thrd_id);
        if(ret == SPT_OK)
            return SPT_DO_AGAIN;
        return ret;
    }

}

int do_insert_rsignpost_down(cluster_head_t *pclst, insert_info_t *pinsert, char *new_data)
{
    //spt_vec *ppre = NULL;
    spt_vec tmp_vec, *pvec_a, *pvec_b, *pvec_s;
    //char *pcur_data;//*ppre_data,
    u64 signpost;
    u32 dataid, vecid_a, vecid_b, vecid_s, cnt;
    spt_dh *pdh;
    int ret;

    pvec_s = 0;
    dataid = data_alloc_combo(pclst, g_thrd_id, &pdh);
    if(pdh == 0)
    {
        /*申请新块，拆分*/
        //BUG();
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        return SPT_NOMEM;
    }
    pdh->ref = 1;
    pdh->pdata = new_data;

    tmp_vec.val = pinsert->key_val;
    signpost = pinsert->signpost;

    vecid_a = vec_alloc_combo(pclst, g_thrd_id, &pvec_a);
    if(pvec_a == 0)
    {
        /*yzx释放资源， 申请新块，拆分*/
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_db);
        spt_set_data_not_free(pdh);
        db_free_to_buf(pclst, dataid, g_thrd_id);
        return SPT_NOMEM;
    }
    pvec_a->val = 0;
//    pvec_a->type = SPT_VEC_DATA;
    pvec_a->pos = 0;
    pvec_a->rd = tmp_vec.rd;
//    pvec_a->down = tmp_vec.rd;

    if(tmp_vec.ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
    {
        pvec_a->type = SPT_VEC_DATA;
    }
    else
    {
        pvec_a->type = SPT_VEC_RIGHT;
    }
    tmp_vec.rd = vecid_a;
    vecid_b = vec_alloc_combo(pclst, g_thrd_id, &pvec_b);
    if(pvec_b == 0)
    {
        /*yzx释放资源， 申请新块，拆分*/
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        spt_set_data_not_free(pdh);
        db_free_to_buf(pclst, dataid, g_thrd_id);
        vec_free_to_buf (pclst, vecid_a, g_thrd_id);
        return SPT_NOMEM;
    }
    pvec_b->val = 0;
    pvec_b->type = SPT_VEC_DATA;
    pvec_b->pos = (pinsert->fs-1)&SPT_VEC_SIGNPOST_MASK;
    pvec_b->rd = dataid;
    pvec_b->down = SPT_NULL;
    
    if((pinsert->fs-1)-signpost > SPT_VEC_SIGNPOST_MASK)
    {
        vecid_s = vec_alloc_combo(pclst, g_thrd_id, &pvec_s);
        if(pvec_s == 0)
        {
            /*yzx释放资源， 申请新块，拆分*/
            atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_db);
            printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
            spt_set_data_not_free(pdh);
            db_free_to_buf(pclst, dataid, g_thrd_id);
            vec_free_to_buf(pclst, vecid_a, g_thrd_id);
            vec_free_to_buf(pclst, vecid_b, g_thrd_id);
            return SPT_NOMEM;
        }
        pvec_s->val = 0;        
        pvec_s->idx = (pinsert->fs-1)>>SPT_VEC_SIGNPOST_BIT;
        pvec_s->rd = vecid_b;

        pvec_a->down = vecid_s;
    }
    else
    {
        pvec_a->down = vecid_b;
    }
    smp_mb();
    if(pinsert->key_val == atomic64_cmpxchg((atomic64_t *)pinsert->pkey_vec, 
                        pinsert->key_val, tmp_vec.val))
    {
        return dataid;
    }
    else
    {
        spt_set_data_not_free(pdh);
        db_free_to_buf_simple(pclst, dataid, g_thrd_id);
        vec_free_to_buf_simple(pclst, vecid_a, g_thrd_id);
        vec_free_to_buf_simple(pclst, vecid_b, g_thrd_id);
        cnt = 3;
        if(pvec_s != 0)
        {
            vec_free_to_buf_simple(pclst, vecid_s, g_thrd_id);
            cnt = 4;
        }
        ret = fill_in_rsv_list(pclst, cnt, g_thrd_id);
        if(ret == SPT_OK)
            return SPT_DO_AGAIN;
        return ret;
    }
}

int do_insert_first_set(cluster_head_t *pclst, insert_info_t *pinsert, char *new_data)
{
    u32 dataid;
    spt_vec tmp_vec, *pcur;
    spt_dh *pdh;
    spt_dh_ext *pdh_ext;
    int ret;

    dataid = data_alloc_combo(pclst, g_thrd_id, &pdh);
    if(pdh == 0)
    {
        /*申请新块，拆分*/
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_db);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        return SPT_NOMEM;
    }
    pdh->ref = 1;
    pdh->pdata = new_data;

    pcur = (spt_vec *)vec_id_2_ptr(pclst,pclst->vec_head);
//    cur_vec.val = pcur->val;
    
	if(pcur != pinsert->pkey_vec)
		BUG();

	tmp_vec.val = pinsert->key_val;
//    tmp_vec.val = cur_vec.val;
    tmp_vec.rd = dataid;
    smp_mb();
    if(pinsert->key_val == atomic64_cmpxchg((atomic64_t *)pinsert->pkey_vec, 
                        pinsert->key_val, tmp_vec.val))
    {
        if(!pclst->is_bottom)
        {
            pdh_ext = (spt_dh_ext *)pdh->pdata;
            pdh_ext->hang_vec = SPT_NULL;
        }
        return dataid;
    }
    else
    {
        spt_set_data_not_free(pdh);
        db_free_to_buf_simple(pclst, dataid, g_thrd_id);      
        ret = fill_in_rsv_list(pclst, 1, g_thrd_id);
        if(ret == SPT_OK)
            return SPT_DO_AGAIN;
        return ret;
    }

}
void *page_insert = NULL;
char  page_in_tree[4096] = {0};

int do_insert_up_via_r(cluster_head_t *pclst, insert_info_t *pinsert, char *new_data)
{
    spt_vec tmp_vec, *pvec_a, *pvec_b, *pvec_s, *pvec_s2;
    //char *pcur_data;//*ppre_data,
    u64 signpost;
    u32 dataid, vecid_a, vecid_b, vecid_s, vecid_s2, tmp_rd, cnt;
    spt_dh *pdh, *plast_dh;
    spt_dh_ext *pdh_ext, *plast_dh_ext;
    int ret;

    pvec_b = 0;
    pvec_s = 0;
    pvec_s2 = 0;
    dataid = data_alloc_combo(pclst, g_thrd_id, &pdh);
    if(pdh == 0)
    {
        /*申请新块，拆分*/
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_db);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        return SPT_NOMEM;
    }
    pdh->ref = 1;
    pdh->pdata = new_data;

    tmp_vec.val = pinsert->key_val;
    signpost = pinsert->signpost;
    tmp_rd = tmp_vec.rd;

    vecid_a = vec_alloc_combo(pclst, g_thrd_id, &pvec_a);
    if(pvec_a == 0)
    {
        /*yzx释放资源， 申请新块，拆分*/
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        spt_set_data_not_free(pdh);
        db_free_to_buf(pclst, dataid, g_thrd_id);
        return SPT_NOMEM;
    }
    pvec_a->val = 0;
    pvec_a->type = SPT_VEC_DATA;
    pvec_a->pos = (pinsert->cmp_pos-1)&SPT_VEC_SIGNPOST_MASK;
    pvec_a->rd = dataid;

    if((pinsert->cmp_pos-1)-signpost > SPT_VEC_SIGNPOST_MASK)
    {
        vecid_s = vec_alloc_combo(pclst, g_thrd_id, &pvec_s);
        if(pvec_s == 0)
        {
            /*yzx释放资源， 申请新块，拆分*/
            atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
            printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
            spt_set_data_not_free(pdh);
            db_free_to_buf(pclst, dataid, g_thrd_id);
            vec_free_to_buf(pclst, vecid_a, g_thrd_id);
            return SPT_NOMEM;
        }
        pvec_s->val = 0;
        pvec_s->idx = (pinsert->cmp_pos-1)>>SPT_VEC_SIGNPOST_BIT;
        pvec_s->rd = vecid_a;
    
        //pvec_a->down = vecid_b;
    
        tmp_vec.rd = vecid_s;
        signpost = pvec_s->idx << SPT_VEC_SIGNPOST_BIT;
    }
    else
    {
        tmp_vec.rd = vecid_a;
        if(tmp_vec.pos == pvec_a->pos && tmp_vec.pos != 0)
        {
            spt_debug("@@@@@ bug\r\n");
            dbg_switch = 1;
            page_insert = new_data;
			memcpy(page_in_tree,pinsert->pcur_data,4096);
			spt_debug("pvec_a->pos:%d\r\n",pvec_a->pos);
			spt_debug("cmp_pos:%d ,fs:%d, startbit :%d ,endbit :%d\r\n",pinsert->cmp_pos,pinsert->fs,pinsert->startbit,pinsert->endbit);

				
			debug_data_print(pinsert->pcur_data);
            while(1);
        }
    }

    if(tmp_vec.type == SPT_VEC_DATA || pinsert->endbit > pinsert->fs)
    {
        vecid_b = vec_alloc_combo(pclst, g_thrd_id, &pvec_b);
        if(pvec_b == 0)
        {
            /*yzx释放资源， 申请新块，拆分*/
            atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
            printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
            spt_set_data_not_free(pdh);
            db_free_to_buf(pclst, dataid, g_thrd_id);
            vec_free_to_buf(pclst, vecid_a, g_thrd_id);
            if(pvec_s != 0)
            {
                vec_free_to_buf(pclst, vecid_s, g_thrd_id);
            }
            return SPT_NOMEM;
        }
        pvec_b->val = 0;
        pvec_b->type = tmp_vec.type;
        pvec_b->pos = (pinsert->fs-1)&SPT_VEC_SIGNPOST_MASK;
        pvec_b->rd = tmp_rd;
        pvec_b->down = SPT_NULL;
        if(tmp_vec.type == SPT_VEC_SIGNPOST)
        {
            if(tmp_vec.ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
				BUG();
        }
        else
        {
            tmp_vec.type = SPT_VEC_RIGHT;
        }
        if((pinsert->fs-1)-signpost > SPT_VEC_SIGNPOST_MASK)
        {
            vecid_s2 = vec_alloc_combo(pclst, g_thrd_id, &pvec_s2);
            if(pvec_s2 == 0)
            {
                /*yzx释放资源， 申请新块，拆分*/
                atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
                spt_set_data_not_free(pdh);
                db_free_to_buf(pclst, dataid, g_thrd_id);
                vec_free_to_buf(pclst, vecid_a, g_thrd_id);
                vec_free_to_buf(pclst, vecid_b, g_thrd_id);
                if(pvec_s != 0)
                {
                    vec_free_to_buf(pclst, vecid_s, g_thrd_id);
                }
                printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
                return SPT_NOMEM;
            }
            pvec_s2->val = 0;        
            pvec_s2->idx = (pinsert->fs-1)>>SPT_VEC_SIGNPOST_BIT;
            pvec_s2->rd = vecid_b;
        
            pvec_a->down = vecid_s2;
        }
        else
        {
            pvec_a->down = vecid_b;
        }
    }
    else
    {
        if(tmp_vec.type == SPT_VEC_SIGNPOST)
        {
           if(tmp_vec.ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
			   BUG();
        }
        pvec_a->down = tmp_rd;
    }   
    smp_mb();
    if(pinsert->key_val == atomic64_cmpxchg((atomic64_t *)pinsert->pkey_vec, 
                        pinsert->key_val, tmp_vec.val))
    {
        if(!pclst->is_bottom)
        {
            pdh_ext = (spt_dh_ext *)pdh->pdata;
            plast_dh = (spt_dh *)db_id_2_ptr(pclst, pinsert->dataid);
            plast_dh_ext = (spt_dh_ext *)plast_dh->pdata;
            pdh_ext->hang_vec = plast_dh_ext->hang_vec;
            plast_dh_ext->hang_vec = vecid_a;
        }
        return dataid;
    }
    else
    {
        spt_set_data_not_free(pdh);
        db_free_to_buf_simple(pclst, dataid, g_thrd_id);
        vec_free_to_buf_simple(pclst, vecid_a, g_thrd_id);
        cnt = 2;
        if(pvec_b != 0)
        {
            vec_free_to_buf_simple(pclst, vecid_b, g_thrd_id);
            cnt++;
        }
        //vec_free_to_buf(*ppclst, vecid_b);
        if(pvec_s != 0)
        {
            vec_free_to_buf_simple(pclst, vecid_s, g_thrd_id);
            cnt++;
        }
        if(pvec_s2 != 0)
        {
            vec_free_to_buf_simple(pclst, vecid_s2, g_thrd_id);
            cnt++;
        }        
        ret = fill_in_rsv_list(pclst, cnt, g_thrd_id);
        if(ret == SPT_OK)
            return SPT_DO_AGAIN;
        return ret;
    }
}

/*比cur->right小时，插到下面*/
int do_insert_down_via_r(cluster_head_t *pclst, insert_info_t *pinsert, char *new_data)
{
    spt_vec tmp_vec, *pvec_a, *pvec_b, *pvec_s;
    //char *pcur_data;//*ppre_data,
    u64 signpost;
    u32 dataid, vecid_a, vecid_b, vecid_s,cnt;
    spt_dh *pdh;
    spt_dh_ext *pdh_ext;
    int ret;

    pvec_s = 0;
    dataid = data_alloc_combo(pclst, g_thrd_id, &pdh);
    if(pdh == 0)
    {
        /*申请新块，拆分*/
        //BUG();
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_db);
        return SPT_NOMEM;
    }
    pdh->ref = 1;
    pdh->pdata = new_data;

    tmp_vec.val = pinsert->key_val;
    signpost = pinsert->signpost;

    vecid_b = vec_alloc_combo(pclst, g_thrd_id, &pvec_b);
    if(pvec_b == 0)
    {
        /*yzx释放资源， 申请新块，拆分*/
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
        spt_set_data_not_free(pdh);
        db_free_to_buf(pclst, dataid, g_thrd_id);
        return SPT_NOMEM;
    }
    pvec_b->val = 0;
    pvec_b->type = SPT_VEC_DATA;
    pvec_b->pos = (pinsert->fs-1)&SPT_VEC_SIGNPOST_MASK;
    pvec_b->rd = dataid;
    pvec_b->down = SPT_NULL;

    vecid_a = vec_alloc_combo(pclst, g_thrd_id, &pvec_a);
    if(pvec_a == 0)
    {
        /*yzx释放资源， 申请新块，拆分*/
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        spt_set_data_not_free(pdh);
        db_free_to_buf(pclst, dataid, g_thrd_id);
        vec_free_to_buf(pclst, vecid_b, g_thrd_id);
        return SPT_NOMEM;
    }
    pvec_a->val = 0;
//    pvec_a->type = SPT_VEC_DATA;
    pvec_a->pos = (pinsert->cmp_pos-1)&SPT_VEC_SIGNPOST_MASK;
//    pvec_a->rd = dataid;

    if(tmp_vec.type == SPT_VEC_DATA)
    {
        pvec_a->type = SPT_VEC_DATA;
        pvec_a->rd = tmp_vec.rd;
        tmp_vec.type = SPT_VEC_RIGHT;
    }
    else if(tmp_vec.type == SPT_VEC_SIGNPOST)
    {
        pvec_a->rd = tmp_vec.rd;
        if(tmp_vec.ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
        {
            tmp_vec.ext_sys_flg = 0;
            pvec_a->type = SPT_VEC_DATA;
        }
        else
        {
            pvec_a->type = SPT_VEC_RIGHT;
        }
    }
    else
    {
        pvec_a->type = SPT_VEC_RIGHT;
        pvec_a->rd = tmp_vec.rd;
    }

    
    if((pinsert->cmp_pos-1)-signpost > SPT_VEC_SIGNPOST_MASK)
    {
        vecid_s = vec_alloc_combo(pclst, g_thrd_id, &pvec_s);
        if(pvec_s == 0)
        {
            /*yzx释放资源， 申请新块，拆分*/
            atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
            printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
            spt_set_data_not_free(pdh);
            db_free_to_buf(pclst, dataid, g_thrd_id);
            vec_free_to_buf(pclst, vecid_a, g_thrd_id);
            vec_free_to_buf(pclst, vecid_b, g_thrd_id);
            return SPT_NOMEM;
        }
        pvec_s->val = 0;
        pvec_s->idx = (pinsert->cmp_pos-1)>>SPT_VEC_SIGNPOST_BIT;
        pvec_s->rd = vecid_a;

        pvec_a->down = vecid_b;

        tmp_vec.rd = vecid_s;

    }
    else if((pinsert->fs-1)-signpost > SPT_VEC_SIGNPOST_MASK)
    {
        vecid_s = vec_alloc_combo(pclst, g_thrd_id, &pvec_s);
        if(pvec_s == 0)
        {
            /*yzx释放资源， 申请新块，拆分*/
            atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
            printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
            spt_set_data_not_free(pdh);
            db_free_to_buf(pclst, dataid, g_thrd_id);
            vec_free_to_buf(pclst, vecid_a, g_thrd_id);
            vec_free_to_buf(pclst, vecid_b, g_thrd_id);
            return SPT_NOMEM;
        }
        pvec_s->val = 0;        
        pvec_s->idx = (pinsert->fs-1)>>SPT_VEC_SIGNPOST_BIT;
        pvec_s->rd = vecid_b;

        pvec_a->down = vecid_s;

        tmp_vec.rd = vecid_a;

    }
    else
    {
        pvec_a->down = vecid_b;

        tmp_vec.rd = vecid_a;
        if(tmp_vec.pos == pvec_a->pos && tmp_vec.pos != 0)
        {
            spt_debug("@@@@@ bug\r\n");
            while(1);
        }
    }
    smp_mb();
    if(pinsert->key_val == atomic64_cmpxchg((atomic64_t *)pinsert->pkey_vec, 
                        pinsert->key_val, tmp_vec.val))
    {
        if(!pclst->is_bottom)
        {
            pdh_ext = (spt_dh_ext *)pdh->pdata;
            pdh_ext->hang_vec = vecid_a;
        }
        return dataid;
    }
    else
    {
        spt_set_data_not_free(pdh);
        db_free_to_buf_simple(pclst, dataid, g_thrd_id);
        vec_free_to_buf_simple(pclst, vecid_a, g_thrd_id);
        vec_free_to_buf_simple(pclst, vecid_b, g_thrd_id);
        cnt = 3;
        if(pvec_s != 0)
        {
            vec_free_to_buf_simple(pclst, vecid_s, g_thrd_id);
            cnt ++;
        }
        ret = fill_in_rsv_list(pclst, cnt, g_thrd_id);
        if(ret == SPT_OK)
            return SPT_DO_AGAIN;
        return ret;
    }
}
/*cur->down == null, 首位为0 ，直接插到cur->down上*/
//可能是首向量的down
int do_insert_last_down(cluster_head_t *pclst, insert_info_t *pinsert, char *new_data)
{
    spt_vec tmp_vec, *pvec_a, *pvec_s;
    //char *pcur_data;//*ppre_data,
    u64 signpost;
    u32 dataid, vecid_a, vecid_s, cnt;
    spt_dh *pdh;
    spt_dh_ext *pdh_ext;
    int ret;

    pvec_s = 0;
    dataid = data_alloc_combo(pclst, g_thrd_id, &pdh);
    if(pdh == 0)
    {
        /*申请新块，拆分*/
        //BUG();
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_db);
        return SPT_NOMEM;
    }
    pdh->ref = 1;
    pdh->pdata = new_data;

    tmp_vec.val = pinsert->key_val;
    signpost = pinsert->signpost;

    vecid_a = vec_alloc_combo(pclst, g_thrd_id, &pvec_a);
    if(pvec_a == 0)
    {
        /*yzx释放资源， 申请新块，拆分*/
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        spt_set_data_not_free(pdh);
        db_free_to_buf(pclst, dataid, g_thrd_id);
        return SPT_NOMEM;
    }
    pvec_a->val = 0;
    pvec_a->type = SPT_VEC_DATA;
    pvec_a->pos = (pinsert->fs-1)&SPT_VEC_SIGNPOST_MASK;
    pvec_a->rd = dataid;
    pvec_a->down = SPT_NULL;

    if((pinsert->fs-1)-signpost > SPT_VEC_SIGNPOST_MASK)
    {
        vecid_s = vec_alloc_combo(pclst, g_thrd_id, &pvec_s);
        if(pvec_s == 0)
        {
            /*yzx释放资源， 申请新块，拆分*/
            atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
            printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
            spt_set_data_not_free(pdh);
            db_free_to_buf(pclst, dataid, g_thrd_id);
            vec_free_to_buf(pclst, vecid_a, g_thrd_id);
            return SPT_NOMEM;
        }
        pvec_s->val = 0;        
        pvec_s->idx = (pinsert->fs-1)>>SPT_VEC_SIGNPOST_BIT;
        pvec_s->rd = vecid_a;

        tmp_vec.down = vecid_s;
    }
    else
    {
        tmp_vec.down = vecid_a;
    }        
    smp_mb();
    if(pinsert->key_val == atomic64_cmpxchg((atomic64_t *)pinsert->pkey_vec, 
                        pinsert->key_val, tmp_vec.val))
    {
        if(!pclst->is_bottom)
        {
            pdh_ext = (spt_dh_ext *)pdh->pdata;
            pdh_ext->hang_vec = pinsert->key_id;
        }
        return dataid;
    }
    else
    {
        spt_set_data_not_free(pdh);
        db_free_to_buf_simple(pclst, dataid, g_thrd_id);
        vec_free_to_buf_simple(pclst, vecid_a, g_thrd_id);
        cnt = 2;
        if(pvec_s != 0)
        {
            vec_free_to_buf_simple(pclst, vecid_s, g_thrd_id);
            cnt++;
        }
        ret = fill_in_rsv_list(pclst, cnt, g_thrd_id);
        if(ret == SPT_OK)
            return SPT_DO_AGAIN;
        return ret;
    }
}

int do_insert_up_via_d(cluster_head_t *pclst, insert_info_t *pinsert, char *new_data)
{
    spt_vec tmp_vec, *pvec_a, *pvec_s, *pvec_down;
    //char *pcur_data;//*ppre_data,
    u64 signpost;
    u32 dataid, down_dataid, vecid_a, vecid_s, cnt;
    spt_dh *pdh;
    spt_dh_ext *pdh_ext;
    int ret;

    pvec_s = 0;
    dataid = data_alloc_combo(pclst, g_thrd_id, &pdh);
    if(pdh == 0)
    {
        /*申请新块，拆分*/
        //BUG();
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_db);
        return SPT_NOMEM;
    }
    pdh->ref = 1;
    pdh->pdata = new_data;

    tmp_vec.val = pinsert->key_val;
    signpost = pinsert->signpost;

    vecid_a = vec_alloc_combo(pclst, g_thrd_id, &pvec_a);
    if(pvec_a == 0)
    {
        /*yzx释放资源， 申请新块，拆分*/
        atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        spt_set_data_not_free(pdh);
        db_free_to_buf(pclst, dataid, g_thrd_id);
        return SPT_NOMEM;
    }
    pvec_a->val = 0;
    pvec_a->type = SPT_VEC_DATA;
    pvec_a->pos = (pinsert->fs-1)&SPT_VEC_SIGNPOST_MASK;
    pvec_a->rd = dataid;

    if((pinsert->fs-1)-signpost > SPT_VEC_SIGNPOST_MASK)
    {
        vecid_s = vec_alloc_combo(pclst, g_thrd_id, &pvec_s);
        if(pvec_s == 0)
        {
            /*yzx释放资源， 申请新块，拆分*/
            atomic64_add(1,(atomic64_t *)&g_dbg_info.oom_no_vec);
            printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
            spt_set_data_not_free(pdh);
            db_free_to_buf(pclst, dataid, g_thrd_id);
            vec_free_to_buf(pclst, vecid_a, g_thrd_id);
            return SPT_NOMEM;
        }
        pvec_s->val = 0;        
        pvec_s->idx = (pinsert->fs-1)>>SPT_VEC_SIGNPOST_BIT;
        pvec_s->rd = vecid_a;

        if(tmp_vec.type == SPT_VEC_SIGNPOST)
        {
            pvec_a->down = tmp_vec.rd;
            tmp_vec.rd = vecid_s;
        }
        else
        {
            pvec_a->down = tmp_vec.down;
            tmp_vec.down = vecid_s;
        }        
    }
    else
    {
        if(tmp_vec.type == SPT_VEC_SIGNPOST)
        {
            pvec_a->down = tmp_vec.rd;
            tmp_vec.rd = vecid_a;
        }
        else
        {
            pvec_a->down = tmp_vec.down;
            tmp_vec.down = vecid_a;
        }
    }
    smp_mb();
    if(pinsert->key_val == atomic64_cmpxchg((atomic64_t *)pinsert->pkey_vec, 
                        pinsert->key_val, tmp_vec.val))
    {
        if(!pclst->is_bottom)
        {
            pdh_ext = (spt_dh_ext *)pdh->pdata;
            pdh_ext->hang_vec = pinsert->key_id;
            pvec_down = (spt_vec *)vec_id_2_ptr(pclst,pvec_a->down);
            down_dataid = get_data_id(pclst, pvec_down);//单线程操作必定成功。
            if(down_dataid < 0)
            {
                spt_debug("get_data_id error\r\n");
                BUG();
            }
            pdh = (spt_dh *)db_id_2_ptr(pclst, down_dataid);
            pdh_ext = (spt_dh_ext *)pdh->pdata;
            pdh_ext->hang_vec = vecid_a;
        }
        return dataid;
    }
    else
    {
        spt_set_data_not_free(pdh);
        db_free_to_buf_simple(pclst, dataid, g_thrd_id);
        vec_free_to_buf_simple(pclst, vecid_a, g_thrd_id);
        cnt = 2;
        if(pvec_s != 0)
        {
            vec_free_to_buf_simple(pclst, vecid_s, g_thrd_id);
            cnt ++;
        }
        ret = fill_in_rsv_list(pclst, cnt, g_thrd_id);
        if(ret == SPT_OK)
            return SPT_DO_AGAIN;
        return ret;
    }

}

void refresh_db_hang_vec(cluster_head_t *pclst, spt_vec *pdata_vec, spt_dh *pdel_dh)
{
    int down_data;
    spt_vec *pdown_vec, *pvec;
    spt_dh *pdown_dh;
    spt_dh_ext *pext_h, *pdown_ext_h;
    if(pdata_vec->down == SPT_NULL)
    {
        return;
    }
    pvec = (spt_vec *)vec_id_2_ptr(pclst, pdata_vec->down);
    while(pvec->type != SPT_VEC_DATA)
    {
        pvec = (spt_vec *)vec_id_2_ptr(pclst, pvec->rd);
    }
    //down_data = get_data_id(pclst,pdown_vec);//单线程，必定成功
    down_data = pvec->rd;
    pdown_dh = (spt_dh *)db_id_2_ptr(pclst, down_data);
    pdown_ext_h = (spt_dh_ext *)pdown_dh->pdata;
    pext_h = (spt_dh_ext *)pdel_dh->pdata;
    pdown_ext_h->hang_vec = pext_h->hang_vec;
    return;
}
/*返回一个db_id，有可能已经被删了也没有关系*/
int find_lowest_data(cluster_head_t *pclst, spt_vec *start_vec)
{
    spt_vec *pcur, cur_vec;

    pcur = start_vec;
    cur_vec.val = pcur->val;

    while(1)
    {
        if(cur_vec.type == SPT_VEC_DATA)
        {
            if(cur_vec.down == SPT_NULL)
                return cur_vec.rd;
            else
            {
                pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vec.down);
                cur_vec.val = pcur->val;
            }
        }
        else if(cur_vec.type == SPT_VEC_RIGHT)
        {
            if(cur_vec.down == SPT_NULL)
            {
                pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vec.rd);
            }
            else
            {
                pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vec.down);
            }
            cur_vec.val = pcur->val;
        }
        else
        {
            spt_debug("type error\r\n");
            while(1);
        }
    }
}
/*nth>=1*/
char *get_about_Nth_smallest_data(spt_sort_info *psort, int nth)
{
    int idx = psort->idx;

    if(psort->array[idx] == NULL)
    {
        if(nth >= idx)
            return psort->array[0];
        return psort->array[idx-nth];
    }
    else
    {
        if(nth >= psort->size)
            return psort->array[idx];
        return psort->array[(idx+psort->size-nth)%psort->size];
    }
}
void spt_order_array_free(spt_sort_info *psort)
{
    vfree(psort);
}
spt_sort_info *spt_order_array_init(cluster_head_t *pclst, int size)
{
    spt_sort_info *psort_ar;
    
    psort_ar = (spt_sort_info *)vmalloc(sizeof(spt_sort_info) + sizeof(char *)*size);
    if(psort_ar == NULL)
        return NULL;
    psort_ar->idx = 0;
    psort_ar->cnt = 0;
    psort_ar->size = size;
    return psort_ar;
}
spt_sort_info *spt_cluster_sort(cluster_head_t *pclst)
{
    spt_vec **stack;
    spt_sort_info *psort;
    int cur_data, cur_vecid, index;
    spt_vec *pcur, cur_vec;
//    spt_vec_f st_vec_f;
    spt_dh *pdh;

    stack = (spt_vec **)vmalloc(4096*8*8);
    if(stack == NULL)
    {
        return 0;
    }
    index = 0;
    psort = spt_order_array_init(pclst, pclst->data_total);
    if(psort == NULL)
    {
        vfree(stack);
        return 0;
    }
    cur_data = SPT_INVALID;

    cur_vecid = pclst->vec_head;
    pcur = (spt_vec *)vec_id_2_ptr(pclst, pclst->vec_head);

    cur_vec.val = pcur->val;
    if(cur_vec.down == SPT_NULL && cur_vec.rd == SPT_NULL)
    {
        printk("cluster is null\r\n");
        return NULL;
    }
    #if 0
    rank = pclst->used_dblk_cnt;
    cur_data = cur_vec.data;
    if(cur_data != SPT_INVALID)
    {
        pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
        pcur_data = get_real_data(pclst, pdh->pdata);
    }
    #endif
    stack[index] = pcur;
    index++;

    while (1)
    {
        if(cur_vec.type != SPT_VEC_DATA)
        {
            cur_vecid = cur_vec.rd;
            pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
            cur_vec.val = pcur->val;
            //debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
            //debug_vec_print(&st_vec_f, cur_vecid);
            if(cur_vec.type == SPT_VEC_SIGNPOST)
            {
                spt_debug("signpost vec found!!\r\n");
                while(1);
            }
            //debug_travl_stack_push(pstack,&st_vec_f, signpost);
            stack[index] = pcur;
            index++;
            #if 0
            pos = st_vec_f.pos;
            if(pos != 0)
                spt_bit_cpy(data, pcur_data, bit, pos);
            else
                spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            bit += st_vec_r.pos;
            #endif
        }
        else
        {            
            cur_data = cur_vec.rd;
            if(cur_data != SPT_NULL)
            {
                pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
                psort->array[psort->idx] = pdh->pdata;
                psort->idx = (psort->idx+1)%psort->size;
                psort->cnt++;
                //debug_pdh_data_print(pclst, pdh);            
            }

            if(index == 0)
            {
                break;
            }
            while(1)
            {
                index--;
                pcur = stack[index];
                cur_vec.val = pcur->val;
                if(cur_vec.down != SPT_NULL)
                {
                    cur_vecid =cur_vec.down;
                    pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                    cur_vec.val = pcur->val;
                    
                    if(cur_vec.type == SPT_VEC_SIGNPOST)
                    {
                        spt_debug("signpost vec found!!\r\n");
                        while(1);
                    }
                    stack[index] = pcur;
                    index++;
                    break;
                }              
                if(index == 0)
                {
                    //todo
                    goto sort_exit;
                }                
            }
        }
    }
sort_exit:    
    vfree(stack);
    return psort;
}

void spt_divided_info_free(spt_divided_info *pdvd_info)
{
    int loop;
    char **vb_array;
    cluster_head_t *pclst;
    if(pdvd_info->up_vb_arr)
    {
        vb_array = pdvd_info->up_vb_arr;
        pclst = pdvd_info->puclst;
        for(loop=0; loop<pdvd_info->divided_times; loop++)
        {
            if(vb_array[loop])
                pclst->freedata(vb_array[loop]);                
        }        
        kfree(pdvd_info->up_vb_arr);
    }
    if(pdvd_info->down_vb_arr)
    {
        vb_array = pdvd_info->down_vb_arr;
        pclst = pdvd_info->pdst_clst;
        for(loop=0; loop<pdvd_info->divided_times; loop++)
        {
            if(vb_array[loop])
                pclst->freedata(vb_array[loop]);                
        }
        kfree(pdvd_info->down_vb_arr);
    }
    kfree(pdvd_info);
    return;
}


spt_divided_info *spt_divided_info_init(spt_sort_info *psort, int dvd_times, 
                                        cluster_head_t *puclst, cluster_head_t *pdclst)
{
    spt_divided_info *pdvd_info;
    int loop, n;
    char **u_vb_array, **d_vb_array;
    char *pdata, *psrc, *pNth_data;
    
    pdvd_info = (spt_divided_info *)kmalloc(sizeof(spt_divided_info),GFP_ATOMIC);
    memset(pdvd_info, 0, sizeof(pdvd_info));
    pdvd_info->divided_times = dvd_times;
    pdvd_info->down_is_bottom = pdclst->is_bottom;
    pdvd_info->puclst = puclst;

    pdvd_info->up_vb_arr = (char **)kmalloc(sizeof(char *)*dvd_times,GFP_ATOMIC);
    if(pdvd_info->up_vb_arr == NULL)
    {
        spt_divided_info_free(pdvd_info);
        return NULL;
    }
    u_vb_array = pdvd_info->up_vb_arr;
    pdvd_info->down_vb_arr = (char **)kmalloc(sizeof(char *)*dvd_times,GFP_ATOMIC);
    if(pdvd_info->down_vb_arr == NULL)
    {
        spt_divided_info_free(pdvd_info);
        return NULL;
    }   
    d_vb_array = pdvd_info->down_vb_arr;
    n = 0;
    for(loop=0; loop<dvd_times; loop++)
    {
        n += SPT_DVD_CNT_PER_TIME;
        pNth_data = get_about_Nth_smallest_data(psort, n);
        psrc = pdclst->get_key_in_tree(pNth_data);

        pdata = puclst->construct_data(psrc);
        if(NULL == pdata)
        {
            spt_divided_info_free(pdvd_info);
            return NULL;
        }
        u_vb_array[loop] = pdata;

        pdata = pdclst->construct_data(psrc);
        if(NULL == pdata)
        {
            spt_divided_info_free(pdvd_info);
            return NULL;
        }
        d_vb_array[loop] = pdata;
        pdclst->get_key_in_tree_end(psrc);

    }
    pdvd_info->pdst_clst = cluster_init(pdvd_info->down_is_bottom, 
                                        pdclst->startbit, 
                                        pdclst->endbit, 
                                        pdclst->thrd_total,
                                        pdclst->get_key,
                                        pdclst->get_key_end,
                                        pdclst->freedata,
                                        pdclst->construct_data);
    if(pdvd_info->pdst_clst == NULL)
    {
        spt_divided_info_free(pdvd_info);
        return NULL;
    }
   
    return pdvd_info;
}

int divide_sub_cluster(cluster_head_t *pclst, spt_dh_ext *pup)
{
    int loop, dataid, ins_dvb_id, ret, total;
    int move_time = SPT_DVD_MOVE_TIMES;
//    int move_per_cnt = 100;
    spt_sort_info *psort;
    spt_divided_info *pdinfo;
    spt_dh_ext *pext_head;
    cluster_head_t *plower_clst, *pdst_clst;
    spt_dh *pdh;
    query_info_t qinfo = {0};

    pext_head = pup;
    plower_clst = pext_head->plower_clst;
   
    preempt_disable();
	spt_thread_start(g_thrd_id);
    
    psort = spt_cluster_sort(pext_head->plower_clst);
    if(psort == NULL)
        return SPT_ERR;
    pdinfo = spt_divided_info_init(psort, move_time, pclst, pext_head->plower_clst);
    spt_thread_exit(g_thrd_id);
    preempt_enable();

	pdst_clst = pdinfo->pdst_clst;

    for(loop=0; loop<move_time; loop++)
    {
        /**********
        thread_in
        1.插入上层虚拟挡板loop，此时挡板指向的cluster为空。工作线程如果落入此范围则thread_out
        thread_out
        2.等待两个tick(等待下层cluster中屏蔽区间的数据无人查询，避免查询结果产生错误)
        thread_in
        3.插入下层虚拟挡板loop
        4.开始搬移
          a.查询下级cluster中最小的数据。
          b.如果返回值不等于插入的虚拟挡板数据id，则删除此数据，然后往目标cluster中插入此数据,跳到a继续
          c.如果返回值等于虚拟挡板，则删除此数据，然后往目标cluster中插入此数据。跳到5
        5.
          a.查询下级cluster中最小的数据。
          b.如果返回值不等于插入的虚拟挡板数据id，则删除一次目标cluster中的虚拟挡板。跳到6
          c.如果返回值等于虚拟挡板，则删除此数据，然后往目标cluster中插入此数据。跳到5a
        6.设置上层虚拟挡板loop指向目的cluster。
        7.删除虚拟挡板loop-1.
        thread_out
        ***********/
        preempt_disable();
        spt_thread_start(g_thrd_id);
        do_insert_data(pclst, pdinfo->up_vb_arr[loop], 
                        pclst->get_key_in_tree, pclst->get_key_in_tree_end);
        spt_thread_exit(g_thrd_id);
        spt_thread_wait(2, g_thrd_id);
        spt_thread_start(g_thrd_id);
                
        qinfo.op = SPT_OP_INSERT;
        qinfo.signpost = 0;
        qinfo.pstart_vec = plower_clst->pstart;
        qinfo.startid = plower_clst->vec_head;
        qinfo.endbit = plower_clst->endbit;
        qinfo.data = pdinfo->down_vb_arr[loop];
        qinfo.get_key = plower_clst->get_key;
        if(find_data(plower_clst, &qinfo)!=SPT_OK)
        {
            BUG();
        }
        ins_dvb_id = qinfo.db_id;
        //if(qinfo.data == NULL)
        {
            pdinfo->down_vb_arr[loop] = 0;
        }
        total = 0;
        while(1)
        {
            dataid = find_lowest_data(plower_clst, plower_clst->pstart);
            pdh = (spt_dh *)db_id_2_ptr(plower_clst, dataid);
            while(1)
            {
                ret = do_delete_data_no_free(plower_clst, pdh->pdata, 
                                             plower_clst->get_key_in_tree,
                                             plower_clst->get_key_in_tree_end);
                if(ret == SPT_OK)
                    break;
                else if(ret == SPT_WAIT_AMT)
                {
                    spt_thread_exit(g_thrd_id);
                    spt_thread_wait(2, g_thrd_id);
					//preempt_enable();
					//schedule();
					//preempt_disable();
                    spt_thread_start(g_thrd_id);
                }
                else
                {
                    spt_debug("divide delete error\r\n");
                }
            }
            ret = do_insert_data(pdst_clst, pdh->pdata, 
                                 pdst_clst->get_key_in_tree,
                                 pdst_clst->get_key_in_tree_end);
            if(ret != SPT_OK)
            {
                spt_debug("divide insert error\r\n");
            }
            if(dataid == ins_dvb_id)
            {
                break;
            }
            total ++;
            if(total == 100)
            {
                total = 0;
                spt_thread_exit(g_thrd_id);
                preempt_enable();
                schedule();
                preempt_disable();
                spt_thread_start(g_thrd_id);
            }
        }
        while(1)
        {
            dataid = find_lowest_data(plower_clst, plower_clst->pstart);
            if(dataid == ins_dvb_id)
            {
                //pdh = (spt_dh *)db_id_2_ptr(plower_clst, dataid);
                while(1)
                {
                    ret = do_delete_data_no_free(plower_clst, pdh->pdata, 
                                                 plower_clst->get_key_in_tree,
                                                 plower_clst->get_key_in_tree_end);
                    if(ret == SPT_OK)
                        break;
                    else if(ret == SPT_WAIT_AMT)
                    {
                        spt_thread_exit(g_thrd_id);
                        spt_thread_wait(2, g_thrd_id);
				     	preempt_enable();
				    	schedule();
					    preempt_disable();
                        spt_thread_start(g_thrd_id);
                    }
                    else
                    {
                        spt_debug("divide delete error\r\n");
                    }
                }
                ret = do_insert_data(pdst_clst, pdh->pdata, 
                           pdst_clst->get_key_in_tree,
                           pdst_clst->get_key_in_tree_end);
                if(ret != SPT_OK)
                {
                    spt_debug("divide insert error\r\n");
                }
            }
            else
            {
                while(1)
                {
                    ret = do_delete_data(pdst_clst, pdh->pdata, 
                               pdst_clst->get_key_in_tree,
                               pdst_clst->get_key_in_tree_end);
                    if(ret == SPT_OK)
                        break;
                    else if(ret == SPT_WAIT_AMT)
                    {
                        spt_thread_exit(g_thrd_id);
                        spt_thread_wait(2, g_thrd_id);
						preempt_enable();
						schedule();
						preempt_disable();
                        spt_thread_start(g_thrd_id);
                    }
                    else
                    {
                        spt_debug("divide delete error\r\n");
                    }
                }
                break;
            }
        }
        pext_head = (spt_dh_ext *)pdinfo->up_vb_arr[loop];
        pext_head->plower_clst = pdst_clst;
        if(loop > 0)
        {
            while(1)
            {
                ret = do_delete_data(pclst, pdinfo->up_vb_arr[loop-1], 
                                   pclst->get_key_in_tree,
                                   pclst->get_key_in_tree_end);
                if(ret == SPT_OK)
                    break;
                else if(ret == SPT_WAIT_AMT)
                {
                    spt_thread_exit(g_thrd_id);
                    spt_thread_wait(2, g_thrd_id);
					preempt_enable();
					schedule();
					preempt_disable();
                    spt_thread_start(g_thrd_id);
                }
                else
                {
                    spt_debug("divide delete error\r\n");
                }
            }
            pdinfo->up_vb_arr[loop-1] = 0;
        }
        spt_thread_exit(g_thrd_id);
        preempt_enable();
    }
    pdinfo->up_vb_arr[loop-1] = 0;
    /*释放资源，虚拟挡板的释放由cluster的缓冲区中来做*/
    spt_divided_info_free(pdinfo);
    spt_order_array_free(psort);
    return SPT_OK;
}
int spt_divided_scan(cluster_head_t *pclst)
{
    spt_sort_info *psort;
    spt_dh_ext *pdh_ext;
    cluster_head_t *plower_clst;
    int i;

    psort = spt_cluster_sort(pclst);
    if(psort == NULL)
    {
        spt_debug("psort ERR\r\n");
        return SPT_ERR;
    }
    for(i=0;i<psort->size;i++)
    {
        pdh_ext = (spt_dh_ext *)psort->array[i];
        plower_clst = pdh_ext->plower_clst;
        if(plower_clst->data_total >= SPT_DVD_THRESHOLD_VA)
        {
            divide_sub_cluster(pclst,pdh_ext);
        }
    }
    spt_order_array_free(psort);
    return SPT_OK;
}
struct debug_map_cnt
{
    int idx;
    int line[65536];
};
struct debug_find_path
{
    char *page;
    int startbit;
    int len;
    int direct;
};

struct debug_find_path debug_path[48][1024] = {0};
int path_index[48] = {0};
unsigned char path_data[48][4096] = {0};

u64 map_cnt[48] = {0};
u64 unmap_cnt[48] = {0};

u64 debug_refind[48] = {0};
u64 debug_refind_max[48] = {0};
u64 map_cnt_max[48] = {0};

struct debug_map_cnt map_st[48] = {0};
struct debug_map_cnt unmap_st[48] = {0};

void print_debug_path(int id)
{
    unsigned char* pfind;
    int i;
    pfind = path_data[id];
    printk("=======path_data:=======\r\n");
    for(i = 0 ; i < 4096 ;i++)
    {
        if(0 == i%32)
            printk("\r\n");
        printk("%02x ",*((unsigned char*)pfind +i));
    
    }
    printk("=======find_path:=======\r\n");
    for(i=0;i<path_index[id]&& i<1024;i++)
    {
        printk("page:%p startbit:%d len:%d direct:%d\r\n", 
            debug_path[id][i].page,
            debug_path[id][i].startbit,
            debug_path[id][i].len,
            debug_path[id][i].direct);
    }
    return;
}

void printk_debug_map_cnt(void)
{
	int i,j;

	for(i = 0;i <48;i++)
	{
		if(map_cnt[i] == unmap_cnt[i])
		{
			continue;
		}
		printk("thread id %d,map cnt is %lld,umap_cnt %lld\r\n",i,map_cnt[i],unmap_cnt[i]);
		for(j =0 ;j < map_st[i].idx; j++ )
		{
			printk("map line num is %d\r\n",map_st[i].line[j]);
		}
		for(j =0 ;j < unmap_st[i].idx; j++ )
		{
			printk("umap line num is %d\r\n",unmap_st[i].line[j]);
		}

	}
}
void printk_debug_map_cnt_id(int thread_id)
{
	int i = thread_id;
	int j ;

		printk("thread id %d,map cnt is %lld,umap_cnt %lld\r\n",i,map_cnt[i],unmap_cnt[i]);
		for(j =0 ;j < map_st[i].idx; j++ )
		{
			printk("map line num is %d\r\n",map_st[i].line[j]);
		}
		for(j =0 ;j < unmap_st[i].idx; j++ )
		{
			printk("umap line num is %d\r\n",unmap_st[i].line[j]);
		}

}
//#define DEBUG_FIND_PATH
extern int thread_map_cnt[64];
/*ret:1查询不到；0删除成功；-1错误*/
int find_data(cluster_head_t *pclst, query_info_t *pqinfo)
{
    int cur_data, vecid, cmp, op, cur_vecid, pre_vecid, next_vecid, cnt;
    spt_vec *pcur, *pnext, *ppre;
    spt_vec tmp_vec, cur_vec, next_vec;
    char *pcur_data;//*ppre_data,
    u64 startbit, endbit, len, fs_pos, signpost;
    int va_old, va_new;
    u8 direction;
    int ret;
    int retb, dataid_confirm, cmp_invalid;
    vec_cmpret_t cmpres;
    insert_info_t st_insert_info;
    char *pdata, *prdata;
    spt_dh *pdh;
//    spt_cb_get_key get_key;
    spt_cb_end_key finish_key_cb;
    int i;
    char *pcur_data2;
    spt_dh *pdh2;
    int cur_data2;

    if(map_cnt[g_thrd_id] != unmap_cnt[g_thrd_id])
    {
        return SPT_ERR;
    }
    map_cnt[g_thrd_id] = unmap_cnt[g_thrd_id] = 0;
    map_st[g_thrd_id].idx = unmap_st[g_thrd_id].idx = 0;
    thread_map_cnt[g_thrd_id]=0;

//    query_info_t *pqinfo = pquery_info;
    if(pclst->status == SPT_WAIT_AMT)
    {
        cnt = rsv_list_fill_cnt(pclst, g_thrd_id);
        ret = fill_in_rsv_list(pclst, cnt, g_thrd_id);
        if(ret == SPT_OK)
        {
            pclst->status = SPT_OK;
        }
        else
        {
            return ret;
        }
    }
    ret = SPT_NOT_FOUND;
    op = pqinfo->op;

    atomic64_add(1,(atomic64_t *)&g_dbg_info.find_data);
    pdata = pqinfo->data;
    #if 1
    if(pqinfo->get_key == NULL)
        prdata = pclst->get_key(pqinfo->data);
    else
        prdata = pqinfo->get_key(pqinfo->data);
    map_st[g_thrd_id].line[map_st[g_thrd_id].idx] = __LINE__;
    map_st[g_thrd_id].idx = (map_st[g_thrd_id].idx+1)&0xffff;
    map_cnt[g_thrd_id]++;
    if(map_cnt[g_thrd_id] > map_cnt_max[g_thrd_id])
        map_cnt_max[g_thrd_id] = map_cnt[g_thrd_id];
    if(pqinfo->get_key_end == NULL)
    {
        finish_key_cb = pclst->get_key_end;
    }
    else
    {
        finish_key_cb = pqinfo->get_key_end;
    }
    #else
    if(pqinfo->data_type == SPT_SYS_DATA)
    {
        prdata = pclst->get_key(pqinfo->data);
    }
    #endif
    cur_data = SPT_INVALID;
    debug_refind[g_thrd_id] = 0;

refind_start:
    atomic64_add(1,(atomic64_t *)&g_dbg_info.refind_start);
    signpost = pqinfo->signpost;
    pcur = pqinfo->pstart_vec;
    cur_vecid = pre_vecid = pqinfo->startid;
#ifdef DEBUG_FIND_PATH
    if(op!=SPT_OP_FIND)
        path_index[g_thrd_id] = 0;
#endif    
refind_forward:
    //debug
    debug_refind[g_thrd_id]++;
    if(debug_refind[g_thrd_id] > debug_refind_max[g_thrd_id])
    {       
        debug_refind_max[g_thrd_id] = debug_refind[g_thrd_id];
    }
    if(atomic_read((atomic_t *) &dbg_switch) == 1)
    {
        while(1);
    }
        
    if(pcur == NULL)
        goto refind_start;
    atomic64_add(1,(atomic64_t *)&g_dbg_info.refind_forward);
    ppre = NULL;
    cur_vecid = pre_vecid;
    pre_vecid = SPT_INVALID;
    cur_vec.val = pcur->val;
    if(pcur == pclst->pstart)
    {
        startbit = pclst->startbit;
        #ifdef DEBUG_FIND_PATH
        if(op!=SPT_OP_FIND)
            path_index[g_thrd_id] = 0;
        #endif        
    }
    else
    {
        startbit = signpost + cur_vec.pos + 1;
        #ifdef DEBUG_FIND_PATH
        if(op!=SPT_OP_FIND)
            path_index[g_thrd_id]--;
        #endif
    }
    endbit = pqinfo->endbit;
    if(cur_vec.status == SPT_VEC_RAW)
    {
        smp_mb();
        cur_vec.val = pcur->val;
        if(cur_vec.status == SPT_VEC_RAW)
        {
            if(pcur == pqinfo->pstart_vec)
            {
                finish_key_cb(prdata);
                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                unmap_cnt[g_thrd_id]++;
                spt_debug("@@@@@@@\r\n");
                return SPT_DO_AGAIN;
            }
            else
            {
                goto refind_start;
            }
        }
    }
    if(cur_vec.status == SPT_VEC_INVALID 
        || cur_vec.type == SPT_VEC_SIGNPOST)
    {
        if(pcur == pqinfo->pstart_vec)
        {
            finish_key_cb(prdata);
            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
            unmap_cnt[g_thrd_id]++;
            spt_debug("@@@@@@@\r\n");
            return SPT_DO_AGAIN;
        }
        else
        {
            goto refind_start;
        }
    }
    direction = SPT_DIR_START;
    if(cur_data != SPT_INVALID)
    {
        cur_data = SPT_INVALID;
        pclst->get_key_in_tree_end(pcur_data);
        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
        unmap_cnt[g_thrd_id]++;
    }

    fs_pos = find_fs(prdata, startbit, endbit);
    
    while(startbit<endbit)
    {
        /*首位是1，与pcur_vec->right比*/
        if(fs_pos == startbit)
        {
            if(cur_vec.type == SPT_VEC_DATA)
            {
                len = endbit - startbit;
                if(cur_data != SPT_INVALID)
                {
                    pclst->get_key_in_tree_end(pcur_data);
                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                    unmap_cnt[g_thrd_id]++;
                    if(cur_vec.rd != cur_data)
                    {
                        cur_data = SPT_INVALID;
                        goto refind_start;
                    }
                }                
                cur_data = cur_vec.rd;
                if(cur_data >= 0 && cur_data < SPT_INVALID)
                {
                    pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
                    smp_mb();
                    pcur_data = pclst->get_key_in_tree( pdh->pdata);
                    
                    map_st[g_thrd_id].line[map_st[g_thrd_id].idx] = __LINE__;
                    map_st[g_thrd_id].idx = (map_st[g_thrd_id].idx+1)&0xffff;
                    map_cnt[g_thrd_id]++;
                    if(map_cnt[g_thrd_id] > map_cnt_max[g_thrd_id])
                        map_cnt_max[g_thrd_id] = map_cnt[g_thrd_id];                    
                }
                else if(cur_data == SPT_NULL)
                {
                    switch(op){
                    case SPT_OP_FIND:
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        return ret;
                    case SPT_OP_INSERT:
                        st_insert_info.pkey_vec= pcur;
                        st_insert_info.key_val= cur_vec.val;
                        ret = do_insert_first_set(pclst, &st_insert_info, pdata);
                        if(ret == SPT_DO_AGAIN)
                        {
                            cur_data = SPT_INVALID;
                            goto refind_start;
                        }
                        else if(ret >= 0)
                        {
                            pqinfo->db_id = ret;
                            pqinfo->data = 0;
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return SPT_OK;
                        }
                        else
                        {
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return ret;
                        }
                        break;
                    case SPT_OP_DELETE:
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        return ret;
                    default:
                        break;
                    }

                } 
                else
                {
                    spt_debug("@@@@@ bug\r\n");
                    while(1);
                }
                //ppre = pcur;
                //pre_vec.val = cur_vec.val;
                //pcur = NULL;
            }
            else if(cur_vec.type == SPT_VEC_RIGHT)
            {
                pnext = (spt_vec *)vec_id_2_ptr(pclst,cur_vec.rd);
                next_vec.val = pnext->val;
                next_vecid = cur_vec.rd;
                if(next_vec.status == SPT_VEC_RAW)
                {
                    smp_mb();
                    next_vec.val = pnext->val;
                    if(next_vec.status == SPT_VEC_RAW)
                    {
                        goto refind_start;
                    }
                }
                if(next_vec.status == SPT_VEC_INVALID)
                {
                    tmp_vec.val = cur_vec.val;
                    vecid = cur_vec.rd;
                    tmp_vec.rd = next_vec.rd;
                    if(next_vec.type == SPT_VEC_SIGNPOST)
                    {
                        tmp_vec.type = next_vec.ext_sys_flg;
                    }
                    else if(next_vec.type == SPT_VEC_DATA)
                    {
                        if(next_vec.down == SPT_NULL)
                        {
                            spt_set_data_flag(tmp_vec);
                            if(next_vec.rd == SPT_NULL && pcur!=pclst->pstart)
                                tmp_vec.status = SPT_VEC_INVALID;
                        }
                        else
                        {
                            tmp_vec.rd = next_vec.down;
                            //简化逻辑，最后再触发重查
                            #if 0 
                            if(cur_data != SPT_INVALID)
                            {
                                //之前的比较无效，删除结点后重查
                                if(cur_data != next_vec.rd)
                                {
                                    cmp_invalid = 1;
                                }
                                cur_data = SPT_INVALID;
                                pclst->get_key_in_tree_end(pcur_data);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                            }
                            #endif
                        }                
                    }
                    else
                    {
                        ;
                    }
                    //cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                    if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))//delete succ
                    {
                        retb = vec_free_to_buf(pclst, vecid, g_thrd_id);
                        if(retb != SPT_OK)
                        {
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            if(cur_data != SPT_INVALID)
                            {
                                pclst->get_key_in_tree_end(pcur_data);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                            }
                            if(ret == SPT_OK)
                                return ret;
                            return retb;
                        }
                        ///TODO:取新值后再continue
                    }
                    #if 0
                    if(cmp_invalid)
                    {
                        cmp_invalid = 0;
                        goto refind_start;
                    }
                    #endif
                    cur_vec.val = pcur->val;
                    if(cur_vec.status != SPT_VEC_VALID)
                    {
                        pcur = ppre;
                        goto refind_forward;
                    }
                    continue;
                }
                
                //对于一个向量的右结点:
                    //如果是路标，不能是SPT_VEC_SYS_FLAG_DATA
                    //如果是向量，必然有down
                if(next_vec.type == SPT_VEC_SIGNPOST)
                {
                    if(next_vec.ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
                    {
                        tmp_vec.val = next_vec.val;
                        tmp_vec.status = SPT_VEC_INVALID;
                        atomic64_cmpxchg((atomic64_t *)pnext, next_vec.val, tmp_vec.val);
                        //set invalid succ or not, refind from cur
                        cur_vec.val = pcur->val;
                        if((cur_vec.status == SPT_VEC_INVALID))
                        {
                            pcur = ppre;
                            goto refind_forward;
                        }
                        continue;
                    }
                    len = (next_vec.idx << SPT_VEC_SIGNPOST_BIT ) - startbit + 1;
                }
                else
                {
                    if(next_vec.down == SPT_NULL)
                    {
                        tmp_vec.val = next_vec.val;
                        tmp_vec.status = SPT_VEC_INVALID;
                        atomic64_cmpxchg((atomic64_t *)pnext, next_vec.val, tmp_vec.val);
                        //set invalid succ or not, refind from cur
                        cur_vec.val = pcur->val;
                        if((cur_vec.status != SPT_VEC_VALID))
                        {
                            pcur = ppre;
                            goto refind_forward;
                        }
                        continue;
                    }
                    len = next_vec.pos + signpost - startbit + 1;
                }
#if 1
                if(cur_data == SPT_INVALID)//yzx
                {
                    cur_data = get_data_id(pclst, pnext);
                    if(cur_data >= 0 && cur_data < SPT_INVALID)
                    {
                        pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
                        smp_mb();
                        pcur_data = pclst->get_key_in_tree(pdh->pdata);
                        map_st[g_thrd_id].line[map_st[g_thrd_id].idx] = __LINE__;
                        map_st[g_thrd_id].idx = (map_st[g_thrd_id].idx+1)&0xffff;
                        map_cnt[g_thrd_id]++;
                        if(map_cnt[g_thrd_id] > map_cnt_max[g_thrd_id])
                            map_cnt_max[g_thrd_id] = map_cnt[g_thrd_id];                        
                    }
                    else if(cur_data == SPT_DO_AGAIN)
                    {
                        cur_data = SPT_INVALID;
                        goto refind_start;
                    }
                    else if(cur_data == SPT_NULL)
                    {
                        switch(op){
                        case SPT_OP_FIND:
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return ret;
                        case SPT_OP_INSERT:
                            st_insert_info.pkey_vec= pcur;
                            st_insert_info.key_val= cur_vec.val;
                            ret = do_insert_first_set(pclst, &st_insert_info, pdata);
                            if(ret == SPT_DO_AGAIN)
                            {
                                cur_data = SPT_INVALID;
                                goto refind_start;
                            }
                            else if(ret >= 0)
                            {
                                pqinfo->db_id = ret;
                                pqinfo->data = 0;
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                return SPT_OK;
                            }
                            else
                            {
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                return ret;
                            }
                            break;
                        case SPT_OP_DELETE:
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return ret;
                        default:
                            break;
                        }
    
                    }
                    else
                    {
                        //SPT_NOMEM or SPT_WAIT_AMT;
                        
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        if(ret == SPT_OK)
                            return ret;
                        return cur_data;
                    }
                    
                }
#endif            
            }
            //cur是路标 当前不存在路标
            else
            {
                //pnext = (spt_vec *)vec_id_2_ptr(*ppclst,cur_vec.rd);
                //next_vec.val = pnext->val;
                //起始结点不允许是路标，因此方向只有RIGHT和DOWN
                if(direction == SPT_RIGHT)
                {
                    if(cur_vec.ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
                    {
                        tmp_vec.val = cur_vec.val;
                        tmp_vec.status = SPT_VEC_INVALID;
                        //cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                        atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                        /*set invalid succ or not, refind from ppre*/
                        pcur = ppre;
                        goto refind_forward;
                    }
                    pnext = (spt_vec *)vec_id_2_ptr(pclst,cur_vec.rd);
                    next_vec.val = pnext->val;
                    next_vecid = cur_vec.rd;
                    if(next_vec.status == SPT_VEC_RAW)
                    {
                        smp_mb();
                        next_vec.val = pnext->val;
                        if(next_vec.status == SPT_VEC_RAW)
                        {
                            goto refind_start;
                        }
                    }
                    if(next_vec.status == SPT_VEC_INVALID)
                    {
                        tmp_vec.val = cur_vec.val;
                        vecid = cur_vec.rd;
                        tmp_vec.rd = next_vec.rd;
                        if(next_vec.type == SPT_VEC_SIGNPOST)
                        {
                            tmp_vec.ext_sys_flg = next_vec.ext_sys_flg;
                        }
                        else if(next_vec.type == SPT_VEC_DATA)
                        {
                            if(next_vec.down == SPT_NULL)
                            {
                                tmp_vec.ext_sys_flg = SPT_VEC_SYS_FLAG_DATA;
                            }
                            else
                            {
                                tmp_vec.rd = next_vec.down;
                                if(cur_data != SPT_INVALID)
                                {
                                    cur_data = SPT_INVALID;
                                    pclst->get_key_in_tree_end(pcur_data);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
                                }
                            }                
                        }
                        else
                        {
                            ;
                        }
                        //cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                        if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))//delete succ
                        {
                            retb = vec_free_to_buf(pclst, vecid, g_thrd_id);
                            if(retb != SPT_OK)
                            {
                                if(cur_data != SPT_INVALID)
                                {
                                    pclst->get_key_in_tree_end(pcur_data);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
                                }                            
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                if(ret == SPT_OK)
                                    return ret;
                                return retb;
                            }
                        }
                            cur_vec.val = pcur->val;
                            if(cur_vec.status == SPT_VEC_INVALID)
                            {
                                pcur = ppre;
                                goto refind_forward;
                            }
                            continue;
                    }
                    //对于一个路标结点:
                        //next为路标，是不合理结构。
                    if(next_vec.type == SPT_VEC_SIGNPOST)
                    {
                        tmp_vec.val = cur_vec.val;
                        tmp_vec.status = SPT_VEC_INVALID;
                        cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                        /*set invalid succ or not, refind from ppre*/
                        pcur = ppre;
                        goto refind_forward;
                    }
                    else
                    {
                        if(next_vec.down == SPT_NULL)
                        {
                            tmp_vec.val = next_vec.val;
                            tmp_vec.status = SPT_VEC_INVALID;
                            atomic64_cmpxchg((atomic64_t *)pnext, next_vec.val, tmp_vec.val);
                            //set invalid succ or not, refind from cur
                            cur_vec.val = pcur->val;
                            if((cur_vec.status == SPT_VEC_INVALID))
                            {
                                pcur = ppre;
                                goto refind_forward;
                            }
                            continue;
                        }
                        //signpost赋值以后，不能再从pre重查了。
                        signpost = cur_vec.idx << SPT_VEC_SIGNPOST_BIT;                        
                        len = next_vec.pos + signpost - startbit + 1;
                    }
                }
                else //direction = down
                {
                    if(cur_vec.rd == SPT_NULL)
                    {
                        tmp_vec.val = cur_vec.val;
                        tmp_vec.status = SPT_VEC_INVALID;
                        cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                        /*set invalid succ or not, refind from ppre*/
                        pcur = ppre;
                        goto refind_forward;
                    }
                    pnext = (spt_vec *)vec_id_2_ptr(pclst,cur_vec.rd);
                    next_vec.val = pnext->val;
                    next_vecid = cur_vec.rd;
                    if(next_vec.status == SPT_VEC_RAW)
                    {
                        smp_mb();
                        next_vec.val = pnext->val;
                        if(next_vec.status == SPT_VEC_RAW)
                        {
                            goto refind_start;
                        }
                    }
                    if(next_vec.status == SPT_VEC_INVALID)
                    {
                        tmp_vec.val = cur_vec.val;
                        vecid = cur_vec.rd;
                        if(next_vec.type == SPT_VEC_SIGNPOST)
                        {
                            tmp_vec.rd = next_vec.rd;
                        }
                        else
                        {
                            tmp_vec.rd = next_vec.down;
                        }
                        //cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                        if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))//delete succ
                        {
                            retb = vec_free_to_buf(pclst, vecid, g_thrd_id);
                            if(retb != SPT_OK)
                            {
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                if(cur_data != SPT_INVALID)
                                {
                                    pclst->get_key_in_tree_end(pcur_data);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
                                }
                                if(ret == SPT_OK)
                                    return ret;
                                return retb;
                            }
                        }
                        cur_vec.val = pcur->val;
                        if(cur_vec.status == SPT_VEC_INVALID)
                        {
                            pcur = ppre;
                            goto refind_forward;
                        }
                        continue;
                    }
                    //只处理应该遍历的路径
                    if(next_vec.type != SPT_VEC_SIGNPOST && next_vec.pos == 0)
                    {
                        ppre = pcur;
                        //pre_vec.val = cur_vec.val;
                        pcur = pnext;
                        pre_vecid = cur_vecid;
                        cur_vecid = next_vecid;
                        cur_vec.val = next_vec.val;
                        direction = SPT_DOWN;
                        //signpost赋值以后，不能再从pre重查了。
                        signpost = cur_vec.idx << SPT_VEC_SIGNPOST_BIT;                            
                        continue;
                    }
                    else
                    {
                        switch(op){
                        case SPT_OP_FIND:
                            if(cur_data != SPT_INVALID)
                            {
                                pclst->get_key_in_tree_end(pcur_data);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                            }
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return ret;
                        case SPT_OP_INSERT:
                            st_insert_info.pkey_vec = pcur;
                            st_insert_info.key_val= cur_vec.val;
                            //st_insert_info.signpost = signpost;
                            ret = do_insert_dsignpost_right(pclst, &st_insert_info, pdata);
                            if(ret == SPT_DO_AGAIN)
                            {
                                goto refind_start;
                            }
                            else if(ret >= 0)
                            {
                                pqinfo->db_id = ret;
                                pqinfo->data = 0;
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                if(cur_data != SPT_INVALID)
                                {
                                    pclst->get_key_in_tree_end(pcur_data);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
                                }
                                return SPT_OK;
                            }
                            else
                            {
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                if(cur_data != SPT_INVALID)
                                {
                                    pclst->get_key_in_tree_end(pcur_data);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
                                }
                                return ret;
                            }
                            break;
                        case SPT_OP_DELETE:
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            if(cur_data != SPT_INVALID)
                            {
                                pclst->get_key_in_tree_end(pcur_data);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                            }
                            return ret;
                        default:
                            break;
                        }
                    }
                }
            }
#if 0
            if(cur_data == SPT_INVALID)//yzx
            {
                cur_data = get_data_id(pclst, pnext);
                if(cur_data >= 0 && cur_data < SPT_INVALID)
                {
                    pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
                    smp_mb();
                    pcur_data = pclst->get_key_in_tree(pdh->pdata);
                    map_st[g_thrd_id].line[map_st[g_thrd_id].idx] = __LINE__;
                    map_st[g_thrd_id].idx = (map_st[g_thrd_id].idx+1)&0xffff;
                    map_cnt[g_thrd_id]++;
                }
                else if(cur_data == SPT_DO_AGAIN)
                {
                    cur_data = SPT_INVALID;
                    goto refind_start;
                }
                else if(cur_data == SPT_NULL)
                {
                    switch(op){
                    case SPT_OP_FIND:
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        return ret;
                    case SPT_OP_INSERT:
                        st_insert_info.pkey_vec= pcur;
                        st_insert_info.key_val= cur_vec.val;
                        ret = do_insert_first_set(pclst, &st_insert_info, pdata);
                        if(ret == SPT_DO_AGAIN)
                        {
                            cur_data = SPT_INVALID;
                            goto refind_start;
                        }
                        else if(ret >= 0)
                        {
                            pqinfo->db_id = ret;
                            pqinfo->data = 0;
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return SPT_OK;
                        }
                        else
                        {
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return ret;
                        }
                        break;
                    case SPT_OP_DELETE:
                        
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        return ret;
                    default:
                        break;
                    }

                }
                else
                {
                    //SPT_NOMEM or SPT_WAIT_AMT;
                    
                    finish_key_cb(prdata);
                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                    unmap_cnt[g_thrd_id]++;
					if(ret == SPT_OK)
						return ret;
                    return cur_data;
                }
                
            }
#endif            
            
            cmp = diff_identify(prdata, pcur_data, startbit, len, &cmpres);

            #ifdef DEBUG_FIND_PATH
            if(op!=SPT_OP_FIND)
            {
                debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].page = pdh->pdata;
                debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].startbit = startbit;
                debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].len = len;
                debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].direct = 1;
                spt_bit_cpy(path_data[g_thrd_id],pcur_data,startbit,len);
                path_index[g_thrd_id]++;
            }
            #endif       
            if(cmp == 0)
            {
                startbit += len;              
                /*find the same record*/
                if(startbit >= endbit)
                {
                    break;
                }
                ppre = pcur;
                pcur = pnext;
                pre_vecid = cur_vecid;
                cur_vecid = next_vecid;
                cur_vec.val = next_vec.val;
                direction = SPT_RIGHT;
                ///TODO:startbit already >= DATA_BIT_MAX
                fs_pos = find_fs(prdata, startbit, endbit - startbit);
                continue;
            }
            /*insert up*/
            else 
            {
                if(cur_vec.type != SPT_VEC_DATA)
                    if(cur_data != get_data_id(pclst, pnext))
                        goto refind_start;
                if(cmp > 0)
                {
                    switch(op){
                    case SPT_OP_FIND:
                        if(pqinfo->op == SPT_OP_FIND)
                        {
                            pqinfo->db_id = cur_data;
                            pqinfo->vec_id = cur_vecid;
                            pqinfo->cmp_result = 1;
                        }
                        
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        if(cur_data != SPT_INVALID)
                        {
                            pclst->get_key_in_tree_end(pcur_data);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                        }
                        return ret;
                    case SPT_OP_INSERT:
                        st_insert_info.pkey_vec= pcur;
                        st_insert_info.key_val= cur_vec.val;
                        st_insert_info.cmp_pos = cmpres.pos;
                        st_insert_info.fs = cmpres.smallfs;
                        st_insert_info.signpost = signpost;
                        st_insert_info.endbit = startbit+len;
                        st_insert_info.dataid = cur_data;
                        //for debug
                        st_insert_info.pcur_data = pcur_data;
                        st_insert_info.startbit = startbit;
                        st_insert_info.cmpres = cmpres;
                        if(pclst->is_bottom)
						{
							//slice_data_cmp(pdh->pdata,__LINE__);
							//slice_data_cmp(pdata,__LINE__);
						}
						ret = do_insert_up_via_r(pclst, &st_insert_info, pdata);
                        if(ret == SPT_DO_AGAIN)
                        {
                            goto refind_start;
                        }
                        else if(ret >= 0)
                        {
                            pqinfo->db_id = ret;
                            pqinfo->data = 0;
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            if(cur_data != SPT_INVALID)
                            {
                                pclst->get_key_in_tree_end(pcur_data);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                            }
                            return SPT_OK;
                        }
                        else
                        {
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            if(cur_data != SPT_INVALID)
                            {
                                pclst->get_key_in_tree_end(pcur_data);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                            }
                            return ret;
                        }
                        break;
                    case SPT_OP_DELETE:
#if 1
spt_debug("=====================================================\r\n");
spt_debug("cmp info,[startbit:%d] [cmp_pos:%d] [small_fs:%d] [cur_vec:%lld]\r\n", startbit, 
            cmpres.pos, cmpres.smallfs, cur_vec.val);
printk("data_in_tree:%p\r\n", pdh->pdata);
for(i = 0 ; i < 32 ;i++)
{
    printk("%02x ",*((unsigned char*)pcur_data +i));

}
printk("data_insert:%p\r\n", pqinfo->data);
for(i = 0 ; i < 32 ;i++)
{
    printk("%02x ",*((unsigned char*)prdata +i));

}
spt_debug("=====================================================\r\n");
#endif
                        
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        if(cur_data != SPT_INVALID)
                        {
                            pclst->get_key_in_tree_end(pcur_data);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                        }
                        return ret;
                    default:
                        break;
                    }
                }
                /*insert down*/
                else
                {
                    switch(op){
                    case SPT_OP_FIND:
                        if(pqinfo->op == SPT_OP_FIND)
                        {
                            pqinfo->db_id = cur_data;
                            pqinfo->vec_id = cur_vecid;
                            pqinfo->cmp_result = -1;
                        }
                        
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        if(cur_data != SPT_INVALID)
                        {
                            pclst->get_key_in_tree_end(pcur_data);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                        }
                        return ret;
                    case SPT_OP_INSERT:
                        startbit +=len;
                        if(cmpres.smallfs == startbit && startbit<endbit)
                        {
                            st_insert_info.fs = find_fs(prdata, startbit, endbit-startbit);
                        }
                        else
                        {
                            st_insert_info.fs = cmpres.smallfs;
                        }
                        st_insert_info.pkey_vec= pcur;
                        st_insert_info.key_val= cur_vec.val;
                        st_insert_info.cmp_pos = cmpres.pos;
                        st_insert_info.signpost = signpost;
                        ret = do_insert_down_via_r(pclst, &st_insert_info, pdata);
                        if(ret == SPT_DO_AGAIN)
                        {
                            goto refind_start;
                        }
                        else if(ret >= 0)
                        {
                            pqinfo->db_id = ret;
                            pqinfo->data = 0;
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            if(cur_data != SPT_INVALID)
                            {
                                pclst->get_key_in_tree_end(pcur_data);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                            }
                            return SPT_OK;
                        }
                        else
                        {
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            if(cur_data != SPT_INVALID)
                            {
                                pclst->get_key_in_tree_end(pcur_data);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                            }
                            return ret;
                        }
                        break;
                    case SPT_OP_DELETE:
                        #if 1
                        spt_debug("=====================================================\r\n");
                        spt_debug("cmp info,[startbit:%d] [cmp_pos:%d] [small_fs:%d] [cur_vec:%lld]\r\n", startbit, 
                                    cmpres.pos, cmpres.smallfs, cur_vec.val);
                        printk("data_in_tree:%p\r\n", pdh->pdata);
                        for(i = 0 ; i < 4096 ;i++)
                        {
                            if(0 == i%32)
								printk("\r\n");
							printk("%02x ",*((unsigned char*)pcur_data +i));
                        
                        }
                        printk("data_insert:%p\r\n", pqinfo->data);
                        for(i = 0 ; i < 4096 ;i++)
                        {
                            if(0 == i%32)
								printk("\r\n");
                            printk("%02x ",*((unsigned char*)prdata +i));
                        
                        }
                        spt_debug("=====================================================\r\n");
                        #endif
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        if(cur_data != SPT_INVALID)
                        {
                            pclst->get_key_in_tree_end(pcur_data);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                        }
                        return ret;
                    default:
                        break;
                    }
                }
            }
        }
        else
        {
            if(cur_data != SPT_INVALID)
            {
                if(cur_data != get_data_id(pclst, pcur))
                    goto refind_start;
                cur_data = SPT_INVALID;
                pclst->get_key_in_tree_end(pcur_data);
                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                unmap_cnt[g_thrd_id]++;
            }
            while(fs_pos > startbit)
            {
                //先删除invalid，但cur是路标还是向量，取next的方法不一致，故需要先区分
                if(cur_vec.type != SPT_VEC_SIGNPOST)
                {
                    if(cur_vec.down == SPT_NULL)
                    {
                        if(direction == SPT_RIGHT)
                        {
                            tmp_vec.val = cur_vec.val;
                            tmp_vec.status = SPT_VEC_INVALID;
                            cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                            /*set invalid succ or not, refind from ppre*/
                            pcur = ppre;
                            goto refind_forward;
                        }
                        switch(op){
                        case SPT_OP_FIND:
                            if(cur_data == SPT_INVALID)
                            {
                                cur_data = get_data_id(pclst, pcur);
                                if(cur_data >= 0 && cur_data < SPT_INVALID)
                                {
                                    //pqinfo->db_id = cur_data;
                                }
                                else if(cur_data == SPT_DO_AGAIN)
                                {
                                    cur_data = SPT_INVALID;
                                    goto refind_start;
                                }
                                else
                                {
                                    //SPT_NOMEM or SPT_WAIT_AMT;
                                    
                                    finish_key_cb(prdata);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
									if(ret == SPT_OK)
										return ret;
                                    
                                    return cur_data;
                                }
                            }
                            if(pqinfo->op == SPT_OP_FIND)
                            {
                                pqinfo->db_id = cur_data;
                                pqinfo->vec_id = cur_vecid;
                                pqinfo->cmp_result = -1;
                            }
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return ret;
                        case SPT_OP_INSERT:
                            st_insert_info.pkey_vec= pcur;
                            st_insert_info.key_val= cur_vec.val;
                            st_insert_info.fs = fs_pos;
                            st_insert_info.signpost = signpost;
                            st_insert_info.key_id = cur_vecid;
                            ret = do_insert_last_down(pclst, &st_insert_info, pdata);
                            if(ret == SPT_DO_AGAIN)
                            {
                                goto refind_start;
                            }
                            else if(ret >= 0)
                            {
                                pqinfo->db_id = ret;
                                pqinfo->data = 0;
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                return SPT_OK;
                            }
                            else
                            {
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                return ret;
                            }
                            break;
                        case SPT_OP_DELETE:
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return ret;
                        default:
                            break;
                        }
                    }
                    pnext = (spt_vec *)vec_id_2_ptr(pclst,cur_vec.down);
                    next_vec.val = pnext->val;
                    next_vecid = cur_vec.down;
                    if(next_vec.status == SPT_VEC_RAW)
                    {
                        smp_mb();
                        next_vec.val = pnext->val;
                        if(next_vec.status == SPT_VEC_RAW)
                        {
                            goto refind_start;
                        }
                    }
                    if(next_vec.status == SPT_VEC_INVALID)
                    {
                        tmp_vec.val = cur_vec.val;
                        vecid = cur_vec.down;
                        if(next_vec.type == SPT_VEC_SIGNPOST)
                        {
                            tmp_vec.down = next_vec.rd;
                        }
                        else
                        {
                            tmp_vec.down = next_vec.down;
                        }
                        //cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                        if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))//delete succ
                        {
                            retb = vec_free_to_buf(pclst, vecid, g_thrd_id);
                            if(retb != SPT_OK)
                            {
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                if(ret == SPT_OK)
                                    return ret;
                                return retb;
                            }
                        }

                        cur_vec.val = pcur->val;
                        if(cur_vec.status != SPT_VEC_VALID)
                        {
                            pcur = ppre;
                            goto refind_forward;
                        }
                        continue;

                    }
                    //对于一个向量，下结点为尾路标是不合法的。
                    if(next_vec.type == SPT_VEC_SIGNPOST)
                    {
                        if(next_vec.rd == SPT_NULL)
                        {
                            tmp_vec.val = next_vec.val;
                            tmp_vec.status = SPT_VEC_INVALID;
                            atomic64_cmpxchg((atomic64_t *)pnext, next_vec.val, tmp_vec.val);
                            //set invalid succ or not, refind from cur
                            cur_vec.val = pcur->val;
                            if((cur_vec.status == SPT_VEC_INVALID))
                            {
                                pcur = ppre;
                                goto refind_forward;
                            }
                            continue;
                        }
                        len = (next_vec.idx << SPT_VEC_SIGNPOST_BIT ) - startbit + 1;
                    }
                    else
                    {
                        len = next_vec.pos + signpost - startbit + 1;
                    }
                    direction = SPT_DOWN;
                }
                else//SIGN_POST
                {               
                    if(direction == SPT_RIGHT)
                    {
                        if(cur_vec.ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
                        {
                            tmp_vec.val = cur_vec.val;
                            tmp_vec.status = SPT_VEC_INVALID;
                            cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                            /*set invalid succ or not, refind from ppre*/
                            pcur = ppre;
                            goto refind_forward;
                        }
                        pnext = (spt_vec *)vec_id_2_ptr(pclst,cur_vec.rd);
                        next_vec.val = pnext->val;
                        next_vecid = cur_vec.rd;
                        if(next_vec.status == SPT_VEC_RAW)
                        {
                            smp_mb();
                            next_vec.val = pnext->val;
                            if(next_vec.status == SPT_VEC_RAW)
                            {
                                goto refind_start;
                            }
                        }
                        if(next_vec.status == SPT_VEC_INVALID)
                        {
                            tmp_vec.val = cur_vec.val;
                            vecid = cur_vec.rd;
                            tmp_vec.rd = next_vec.rd;
                            if(next_vec.type == SPT_VEC_SIGNPOST)
                            {
                                tmp_vec.ext_sys_flg = next_vec.ext_sys_flg;
                            }
                            else if(next_vec.type == SPT_VEC_DATA)
                            {
                                if(next_vec.down == SPT_NULL)
                                {
                                    tmp_vec.ext_sys_flg = SPT_VEC_SYS_FLAG_DATA;
                                }
                                else
                                {
                                    tmp_vec.rd = next_vec.down;
                                    if(cur_data != SPT_INVALID)
                                    {
                                        cur_data = SPT_INVALID;
                                        pclst->get_key_in_tree_end(pcur_data);
                                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                        unmap_cnt[g_thrd_id]++;
                                    }
                                }                
                            }
                            else
                            {
                                ;
                            }
                            //cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                            if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))//delete succ
                            {
                                retb = vec_free_to_buf(pclst, vecid, g_thrd_id);
                                if(retb != SPT_OK)
                                {
                                    
                                    finish_key_cb(prdata);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
                                    
                                    if(ret == SPT_OK)
                                        return ret;
                                    return retb;
                                }
                            }
                                cur_vec.val = pcur->val;
                                if(cur_vec.status == SPT_VEC_INVALID)
                                {
                                    pcur = ppre;
                                    goto refind_forward;
                                }
                                continue;
                        }

                        //只处理pos为0的右向量
                        if(next_vec.type != SPT_VEC_SIGNPOST 
                            && next_vec.pos == 0)
                        {
                            ppre = pcur;
                            //pre_vec.val = cur_vec.val;
                            pcur = pnext;
                            pre_vecid = cur_vecid;
                            cur_vecid = next_vecid;
                            cur_vec.val = next_vec.val;
                            //direction = SPT_DOWN;
                            //signpost赋值以后，不能再从pre重查了。
                            signpost = cur_vec.idx << SPT_VEC_SIGNPOST_BIT;                            
                            continue;
                        }
                        else
                        {
                            switch(op){
                            case SPT_OP_FIND:
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                return ret;
                            case SPT_OP_INSERT:
                                signpost = cur_vec.idx << SPT_VEC_SIGNPOST_BIT;
                                st_insert_info.pkey_vec = pcur;
                                st_insert_info.key_val= cur_vec.val;
                                st_insert_info.fs = fs_pos;
                                st_insert_info.signpost = signpost;
                                ret = do_insert_rsignpost_down(pclst, &st_insert_info, pdata);
                                if(ret == SPT_DO_AGAIN)
                                {
                                    goto refind_start;
                                }
                                else if(ret >= 0)
                                {
                                    pqinfo->db_id = ret;
                                    pqinfo->data = 0;
                                    
                                    finish_key_cb(prdata);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
                                    
                                    return SPT_OK;
                                }
                                else
                                {
                                    
                                    finish_key_cb(prdata);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
                                    
                                    return ret;
                                }
                                break;
                            case SPT_OP_DELETE:
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
                                
                                return ret;
                            default:
                                break;
                            }
                        }
                    }
                    else//DOWN
                    {
                        if(cur_vec.rd == SPT_NULL)
                        {
                            tmp_vec.val = cur_vec.val;
                            tmp_vec.status = SPT_VEC_INVALID;
                            cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                            /*set invalid succ or not, refind from ppre*/
                            pcur = ppre;
                            goto refind_forward;
                        }
                        pnext = (spt_vec *)vec_id_2_ptr(pclst,cur_vec.rd);
                        next_vec.val = pnext->val;
                        next_vecid = cur_vec.rd;
                        if(next_vec.status == SPT_VEC_RAW)
                        {
                            smp_mb();
                            next_vec.val = pnext->val;
                            if(next_vec.status == SPT_VEC_RAW)
                            {
                                goto refind_start;
                            }
                        }
                        if(next_vec.status == SPT_VEC_INVALID)
                        {
                            tmp_vec.val = cur_vec.val;
                            vecid = cur_vec.rd;
                            if(next_vec.type == SPT_VEC_SIGNPOST)
                            {
                                tmp_vec.rd = next_vec.rd;
                            }
                            else
                            {
                                tmp_vec.rd = next_vec.down;
                            }
                            //cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                            if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))//delete succ
                            {
                                retb = vec_free_to_buf(pclst, vecid, g_thrd_id);
                                if(retb != SPT_OK)
                                {
                                    
                                    finish_key_cb(prdata);
                                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                    unmap_cnt[g_thrd_id]++;
                                    
                                    if(ret == SPT_OK)
                                        return ret;
                                    return retb;
                                }
                            }
                                cur_vec.val = pcur->val;
                                if(cur_vec.status == SPT_VEC_INVALID)
                                {
                                    pcur = ppre;
                                    goto refind_forward;
                                }
                                continue;
                        }
                        //对于一个路标结点:
                            //next为路标，是不合理结构。
                        if(next_vec.type == SPT_VEC_SIGNPOST)
                        {
                            tmp_vec.val = cur_vec.val;
                            tmp_vec.status = SPT_VEC_INVALID;
                            cur_vec.val = atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val);
                            /*set invalid succ or not, refind from ppre*/
                            pcur = ppre;
                            goto refind_forward;
                        }
                        else
                        {
                            //signpost赋值以后，不能再从pre重查了。
                            signpost = cur_vec.idx << SPT_VEC_SIGNPOST_BIT;
                            len = next_vec.pos + signpost - startbit + 1;
                        }
                    }
                }
                #ifdef DEBUG_FIND_PATH
                if(op!=SPT_OP_FIND)
                {
                    cur_data2 = get_data_id(pclst, pnext);
                    if(cur_data2 >= 0 && cur_data2 < SPT_INVALID)
                    {
                        pdh2 = (spt_dh *)db_id_2_ptr(pclst, cur_data2);
                        smp_mb();
                        pcur_data2 = pclst->get_key_in_tree(pdh2->pdata);
                        map_cnt[g_thrd_id]++;
                        if(map_cnt[g_thrd_id] > map_cnt_max[g_thrd_id])
                            map_cnt_max[g_thrd_id] = map_cnt[g_thrd_id];                    
                        
                        debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].page = pdh2->pdata;
                        debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].startbit = startbit;
                        debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].len = len;
                        debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].direct = 0;
                        spt_bit_cpy(path_data[g_thrd_id],pcur_data2,startbit,len);
                        pclst->get_key_in_tree_end(pcur_data2);                        
                        unmap_cnt[g_thrd_id]++;
                        path_index[g_thrd_id]++;    
                    }
                    else
                    {
                        printk("\r\n get dataid fail\r\n");
                        debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].page = 0;
                        debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].startbit = startbit;
                        debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].len = len;
                        debug_path[g_thrd_id][path_index[g_thrd_id]&0x3ff].direct = 0;
                        spt_bit_clear(path_data[g_thrd_id],startbit,len);
                        path_index[g_thrd_id]++;
                    }
                }
                #endif
                
                /*insert*/
                if(fs_pos < startbit + len)
                {
                    switch(op){
                    case SPT_OP_FIND:
                        if(cur_data == SPT_INVALID)
                        {
                            cur_data = get_data_id(pclst, pcur);
                            if(cur_data >= 0 && cur_data < SPT_INVALID)
                            {
                                //pqinfo->db_id = cur_data;
                            }
                            else if(cur_data == SPT_DO_AGAIN)
                            {
                                cur_data = SPT_INVALID;
                                goto refind_start;
                            }
                            else
                            {
                                //SPT_NOMEM or SPT_WAIT_AMT;
                                
                                finish_key_cb(prdata);
                                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                                unmap_cnt[g_thrd_id]++;
								if(ret == SPT_OK)
									return ret;
                                
                                return cur_data;
                            }
                        }
                        if(pqinfo->op == SPT_OP_FIND)
                        {
                            pqinfo->db_id = cur_data;
                            pqinfo->vec_id = cur_vecid;
                            pqinfo->cmp_result = -1;
                        }
                        
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        return ret;
                    case SPT_OP_INSERT:
                        st_insert_info.pkey_vec= pcur;
                        st_insert_info.key_val= cur_vec.val;
                        st_insert_info.fs = fs_pos;
                        st_insert_info.signpost = signpost;
                        st_insert_info.key_id = cur_vecid;
                        ret = do_insert_up_via_d(pclst, &st_insert_info, pdata);
                        if(ret == SPT_DO_AGAIN)
                        {
                            goto refind_start;
                        }
                        else if(ret >= 0)
                        {
                            pqinfo->db_id = ret;
                            pqinfo->data = 0;
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return SPT_OK;
                        }
                        else
                        {
                            
                            finish_key_cb(prdata);
                            unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                            unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                            unmap_cnt[g_thrd_id]++;
                            
                            return ret;
                        }
                        break;
                    case SPT_OP_DELETE:
                        
                        finish_key_cb(prdata);
                        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                        unmap_cnt[g_thrd_id]++;
                        
                        return ret;
                    default:
                        break;
                    }
                }
                startbit += len;
                #if 0
                /*find the same record*/
                if(startbit >= endbit)
                {
                    break;
                }
                #endif
                ppre = pcur;
                pcur = pnext;
                pre_vecid = cur_vecid;
                cur_vecid = next_vecid;
                cur_vec.val = next_vec.val;
            }
            if(fs_pos != startbit) BUG();
        }
    }

    ret = SPT_OK;
    
    if(cur_data == SPT_INVALID)//yzx
    {
        if((cur_vec.type != SPT_VEC_DATA))
        {
            while(1);
        }
        cur_data = cur_vec.rd;
       
        pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
        smp_mb();
        //pcur_data = pclst->get_key(pdh->pdata);
    }
    else
    {
        pclst->get_key_in_tree_end(pcur_data);
        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
        unmap_cnt[g_thrd_id]++;
    }
    //pdh = (spt_dh *)(pcur_data - sizeof(spt_dh));

    switch(op){
    case SPT_OP_FIND:
        if(pqinfo->op == SPT_OP_FIND)
        {
            pqinfo->db_id = cur_data;
            pqinfo->vec_id = cur_vecid;
            pqinfo->cmp_result = 0;
        }
        
        finish_key_cb(prdata);
        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
        unmap_cnt[g_thrd_id]++;
        
        return ret;
    case SPT_OP_INSERT:
        while(1)
        {
            va_old = pdh->ref;
            if(va_old == 0)
            {
                cur_data = SPT_INVALID;
                ret = SPT_NOT_FOUND;
                goto refind_start;
            }
            else if(va_old > 0)
            {
                va_new = va_old+1;
                if(va_old == atomic_cmpxchg((atomic_t *)&pdh->ref, va_old,va_new))
                    break;
            }
            else
            {
                BUG();
            }
        }
        pqinfo->db_id = cur_data;
        pqinfo->ref_cnt = va_new;
        
        finish_key_cb(prdata);
        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
        unmap_cnt[g_thrd_id]++;
        
        return ret;
    case SPT_OP_DELETE:
        pqinfo->db_id = cur_data;
        while(1)
        {
            va_old = pdh->ref;
            if(va_old == 0)
            {
                
                finish_key_cb(prdata);
                unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                unmap_cnt[g_thrd_id]++;
                
                return SPT_NOT_FOUND;
            }
            else if(va_old > 0)
            {
                va_new = va_old-1;
                if(va_old == atomic_cmpxchg((atomic_t *)&pdh->ref, va_old,va_new))
                    break;
            }
            else
            {
                BUG();
            }
        }
        //pqinfo->ref_cnt = va_new;
        if(va_old == 1)
        {
            tmp_vec.val = cur_vec.val;
            tmp_vec.rd = SPT_NULL;
            if(pcur == (spt_vec *)vec_id_2_ptr(pclst,pclst->vec_head))
            {
                if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))//invalidate succ
                {
                    spt_set_data_free_flag(pdh, pqinfo->free_flag);
                    db_free_to_buf(pclst,cur_data, g_thrd_id);
                    
                    finish_key_cb(prdata);
                    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
                    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
                    unmap_cnt[g_thrd_id]++;
                    
                    pqinfo->ref_cnt = 0;
                    return SPT_OK;
                }
                else
                {
                    cur_data = SPT_INVALID;
                    //atomic_add_return(1,(atomic_t *)&pdh->ref);
                    while(1)
                    {
                        va_old = pdh->ref;
                        va_new = va_old+1;
                        if(va_old == atomic_cmpxchg((atomic_t *)&pdh->ref, va_old,va_new))
                            break;
                    }
                    
                    ret = SPT_NOT_FOUND;
                    goto refind_start;
                }
            }
            tmp_vec.status = SPT_VEC_INVALID;
            if(cur_vec.val == atomic64_cmpxchg((atomic64_t *)pcur, cur_vec.val, tmp_vec.val))//invalidate succ
            {
                spt_set_data_free_flag(pdh, pqinfo->free_flag);
                db_free_to_buf(pclst,cur_data, g_thrd_id);
                if(!pclst->is_bottom)
                    refresh_db_hang_vec(pclst, pcur, pdh);
                op = SPT_OP_FIND;
                //pcur = ppre;
                cur_data = SPT_INVALID;
                pqinfo->ref_cnt = 0;
                //goto refind_forward;
                goto refind_start;
            }
            else
            {
                //atomic_add_return(1,(atomic_t *)&pdh->ref);
                while(1)
                {
                    va_old = pdh->ref;
                    va_new = va_old+1;
                    if(va_old == atomic_cmpxchg((atomic_t *)&pdh->ref, va_old,va_new))
                        break;
                }                
                cur_data = SPT_INVALID;
                ret = SPT_NOT_FOUND;
                goto refind_start;
            }
        }
        
        finish_key_cb(prdata);
        unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
        unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
        unmap_cnt[g_thrd_id]++;
        
        pqinfo->ref_cnt = va_new;
        atomic64_add(1,(atomic64_t *)&g_dbg_info.delete_ok);
        return SPT_OK;
    default:
        break;
    }
    
    finish_key_cb(prdata);
    unmap_st[g_thrd_id].line[unmap_st[g_thrd_id].idx] = __LINE__;
    unmap_st[g_thrd_id].idx = (unmap_st[g_thrd_id].idx+1)&0xffff;
    unmap_cnt[g_thrd_id]++;
    
    return SPT_ERR;
}

int do_insert_data(cluster_head_t *pclst, 
                   char *pdata, 
                   spt_cb_get_key pf,
                   spt_cb_end_key pf2)
{
    query_info_t qinfo = {0};
    spt_vec *pvec_start;

    pvec_start = (spt_vec *)vec_id_2_ptr(pclst, pclst->vec_head);
    
    qinfo.op = SPT_OP_INSERT;
    qinfo.signpost = 0;
    qinfo.pstart_vec = pvec_start;
    qinfo.startid = pclst->vec_head;
    qinfo.endbit = pclst->endbit;
    qinfo.data = pdata;
    qinfo.get_key = pf;
    qinfo.get_key_end = pf2;

    return find_data(pclst,&qinfo);
}

int do_delete_data(cluster_head_t *pclst, 
                   char *pdata, 
                   spt_cb_get_key pf,
                   spt_cb_end_key pf2)
{
    query_info_t qinfo = {0};
    spt_vec *pvec_start;

    pvec_start = (spt_vec *)vec_id_2_ptr(pclst, pclst->vec_head);
    
    qinfo.op = SPT_OP_DELETE;
    qinfo.signpost = 0;
    qinfo.pstart_vec = pvec_start;
    qinfo.startid = pclst->vec_head;
    qinfo.endbit = pclst->endbit;
    qinfo.data = pdata;
    qinfo.free_flag = 1;
    qinfo.get_key = pf;
    qinfo.get_key_end = pf2;

    return find_data(pclst,&qinfo);
}

int do_delete_data_no_free(cluster_head_t *pclst, 
                           char *pdata, 
                           spt_cb_get_key pf,
                           spt_cb_end_key pf2)
{
    query_info_t qinfo = {0};
    spt_vec *pvec_start;

    pvec_start = (spt_vec *)vec_id_2_ptr(pclst, pclst->vec_head);
    
    qinfo.op = SPT_OP_DELETE;
    qinfo.signpost = 0;
    qinfo.pstart_vec = pvec_start;
    qinfo.startid = pclst->vec_head;
    qinfo.endbit = DATA_BIT_MAX;
    qinfo.data = pdata;
    qinfo.free_flag = 0;
    qinfo.get_key = pf;
    qinfo.get_key_end = pf2;

    return find_data(pclst,&qinfo);
}

cluster_head_t *find_next_cluster(cluster_head_t *pclst, char *pdata)
{
    query_info_t qinfo;
    int ret;
    spt_dh *pdh;
    spt_dh_ext *pdext_h;
    spt_vec *phang_vec, *pvec;

refind_next:
    memset(&qinfo,0,sizeof(query_info_t));
    qinfo.op = SPT_OP_FIND;
    qinfo.signpost = 0;
    qinfo.pstart_vec = pclst->pstart;
    qinfo.startid = pclst->vec_head;
    qinfo.endbit = pclst->endbit;
    qinfo.data = pdata;
//    qinfo.get_key = pf;

    ret = find_data(pclst, &qinfo);
    if(ret >= 0)
    {
        pdh = (spt_dh *)db_id_2_ptr(pclst, qinfo.db_id);
        pdext_h = (spt_dh_ext *)pdh->pdata;    
        if(qinfo.cmp_result == 0)
        {
            return pdext_h->plower_clst;
        }
        else if(qinfo.cmp_result < 0)
        {
            pvec = (spt_vec *)vec_id_2_ptr(pclst, qinfo.vec_id);
            if(pvec->type == SPT_VEC_DATA)
            {
                return pdext_h->plower_clst;
            }
            else
            {
                pvec = (spt_vec *)vec_id_2_ptr(pclst, pvec->rd);
                ret = find_lowest_data(pclst, pvec);
                if(ret == SPT_NULL)
                    goto refind_next;
                pdh = (spt_dh *)db_id_2_ptr(pclst, ret);
                pdext_h = (spt_dh_ext *)pdh->pdata;
                return pdext_h->plower_clst;
            }
        }
        else
        {
            phang_vec = (spt_vec *)vec_id_2_ptr(pclst, pdext_h->hang_vec);
            if(phang_vec->type == SPT_VEC_DATA)
            {
                if(phang_vec->rd == SPT_NULL)
                    goto refind_next;            
                pdh = (spt_dh *)db_id_2_ptr(pclst, phang_vec->rd);
                pdext_h = (spt_dh_ext *)pdh->pdata;
                return pdext_h->plower_clst;
            }
            else
            {
                pvec = (spt_vec *)vec_id_2_ptr(pclst, phang_vec->rd);
                ret = find_lowest_data(pclst, pvec);
                if(ret == SPT_NULL)
                    goto refind_next;            
                pdh = (spt_dh *)db_id_2_ptr(pclst, ret);
                pdext_h = (spt_dh_ext *)pdh->pdata;
                return pdext_h->plower_clst;
            }
        }
    }
    else
    {
        spt_debug("find_data err!\r\n");
        BUG();
        return NULL;
    }
}
/*interface*/
char *insert_data(cluster_head_t *pclst, char *pdata)
{
    cluster_head_t *pnext_clst;
    query_info_t qinfo = {0};
    spt_dh *pdh;
    int ret = 0;

    pnext_clst = find_next_cluster(pclst, pdata);
    if(pnext_clst == NULL)
    {
        spt_set_errno(SPT_MASKED);
        return 0;
    }

    qinfo.op = SPT_OP_INSERT;
    qinfo.signpost = 0;
    qinfo.pstart_vec = pnext_clst->pstart;
    qinfo.startid = pnext_clst->vec_head;
    qinfo.endbit = pnext_clst->endbit;
    qinfo.data = pdata;

    ret = find_data(pnext_clst,&qinfo);
    if(ret >= 0)
    {
        pdh = (spt_dh *)db_id_2_ptr(pnext_clst, qinfo.db_id);
        return pdh->pdata;
    }
    else
    {
        spt_debug("find_data err!\r\n");
        spt_set_errno(ret);
        return 0;
    }
}

int delete_data(cluster_head_t *pclst, char *pdata)
{
    cluster_head_t *pnext_clst;
    query_info_t qinfo = {0};
    int ret = 0;

    pnext_clst = find_next_cluster(pclst, pdata);
    if(pnext_clst == NULL)
    {
        spt_set_errno(SPT_MASKED);
        return -1;
    }

    qinfo.op = SPT_OP_DELETE;
    qinfo.signpost = 0;
    qinfo.pstart_vec = pnext_clst->pstart;
    qinfo.startid = pnext_clst->vec_head;
    qinfo.endbit = pnext_clst->endbit;
    qinfo.data = pdata;
	qinfo.ref_cnt = -10000;
    qinfo.free_flag = 1;
    ret = find_data(pnext_clst,&qinfo);
    if(ret >= 0)
    {
        return qinfo.ref_cnt;
    }
    else
    {
        //spt_debug("find_data err!\r\n");
        spt_set_errno(ret);
        return -2;
    }
} 
#if 0
cluster_head_t *spt_first_level_clst_init(u64 start, u64 end, int threadnum)
{
    spt_dh_ext *pdh_ext;
    
    ptopclst = cluster_init(0, start, end, threadnum, spt_upper_get_key);
    if(ptopclst == NULL)
        return NULL;
    pdh_ext = malloc(sizeof(spt_dh_ext)+DATA_SIZE);
    if(pdh_ext == NULL)
    {
        cluster_destroy(ptopclst);
        return NULL;
    }
    pdh_ext->data = (char *)(pdh_ext+1);
    pdh_ext->plower_clst = pgclst;
    memset(pdh_ext->data, 0xff, 2);

    do_insert_data(ptopclst, (char *)pdh_ext, spt_upper_get_key);
    return ptopclst;
}
//#endif
cluster_head_t *spt_cluster_init(u64 startbit, 
                              u64 endbit, 
                              int thread_num,
                              spt_cb_get_key pf,
                              spt_cb_end_key pf2,
                              spt_cb_free pf_free,
                              spt_cb_construct pf_con)
{
    cluster_head_t *pclst, *plower_clst;
    spt_dh_ext *pdh_ext;
    pclst = cluster_init(0, startbit, 1000, thread_num, pf, pf2, free_data, 
                            spt_upper_construct_data);
    if(pclst == NULL)
        return NULL;
    plower_clst = cluster_init(1, startbit, endbit, thread_num, pf, pf2, 
                                pf_free, pf_con);
    if(plower_clst == NULL)
    {
        cluster_destroy(pclst);
        return NULL;
    }
    
    pdh_ext = malloc(sizeof(spt_dh_ext)+DATA_SIZE);
    if(pdh_ext == NULL)
    {
        cluster_destroy(pclst);
        cluster_destroy(plower_clst);
        return NULL;
    }
    pdh_ext->data = (char *)(pdh_ext+1);
    pdh_ext->plower_clst = plower_clst;
    memset(pdh_ext->data, 0xff, DATA_SIZE);

    do_insert_data(pclst, (char *)pdh_ext, pclst->get_key_in_tree, pclst->get_key_in_tree_end);
    return pclst;
}
#endif
cluster_head_t *spt_cluster_init(u64 startbit, 
                              u64 endbit, 
                              int thread_num,
                              spt_cb_get_key pf,
                              spt_cb_end_key pf2,
                              spt_cb_free pf_free,
                              spt_cb_construct pf_con)
{
    cluster_head_t *pclst, *plower_clst;
    spt_dh_ext *pdh_ext;
    char *pdata;
    int i;
    pclst = cluster_init(0, startbit, DATA_BIT_MAX, thread_num, pf, pf2, free_data, 
                            spt_upper_construct_data);
    if(pclst == NULL)
        return NULL;

    plower_clst = cluster_init(1, startbit, endbit, thread_num, pf, pf2, 
                                pf_free, pf_con);
    if(plower_clst == NULL)
    {
        cluster_destroy(pclst);
        return NULL;
    }
    
	plow_clst = plower_clst;
    pdh_ext = kmalloc(sizeof(spt_dh_ext)+DATA_SIZE,GFP_ATOMIC);
    if(pdh_ext == NULL)
    {
        cluster_destroy(pclst);
        cluster_destroy(plower_clst);
        return NULL;
    }
    pdh_ext->data = (char *)(pdh_ext+1);
    pdh_ext->plower_clst = plower_clst;
    memset(pdh_ext->data, 0xff, DATA_SIZE);

    do_insert_data(pclst, (char *)pdh_ext, pclst->get_key_in_tree, pclst->get_key_in_tree_end);


    plower_clst = cluster_init(1, startbit, endbit, thread_num, pf, pf2, 
                                pf_free, pf_con);
    if(plower_clst == NULL)
    {
        cluster_destroy(pclst);
        return NULL;
    }
    
    pdh_ext = kmalloc(sizeof(spt_dh_ext)+DATA_SIZE,GFP_ATOMIC);
    if(pdh_ext == NULL)
    {
        cluster_destroy(pclst);
        cluster_destroy(plower_clst);
        return NULL;
    }
    pdh_ext->data = (char *)(pdh_ext+1);
    pdh_ext->plower_clst = plower_clst;
    pdata = pdh_ext->data + 4095;
    *pdata = 8192;

    do_insert_data(pclst, (char *)pdh_ext, pclst->get_key_in_tree, pclst->get_key_in_tree_end);


    
    for(i=0;i< 0;i++)
    {
        plower_clst = cluster_init(1, startbit, endbit, thread_num, pf, pf2, 
                                    pf_free, pf_con);
        if(plower_clst == NULL)
        {
            cluster_destroy(pclst);
            return NULL;
        }
        
        pdh_ext = kmalloc(sizeof(spt_dh_ext),GFP_ATOMIC);
        if(pdh_ext == NULL)
        {
            cluster_destroy(pclst);
            cluster_destroy(plower_clst);
            return NULL;
        }
        pdh_ext->data = kmalloc(DATA_SIZE,GFP_ATOMIC);
        if(pdh_ext->data == NULL)
        {
            cluster_destroy(pclst);
            return NULL;
        }
		get_random_bytes(pdh_ext->data,DATA_SIZE);
        pdh_ext->plower_clst = plower_clst;
        do_insert_data(pclst, (char *)pdh_ext, pclst->get_key_in_tree, pclst->get_key_in_tree_end);
    }
    //debug_cluster_travl(pclst);
    return pclst;
}

#ifdef _BIG_ENDIAN
/*    must be byte aligned
    len:bit len, */
void find_smallfs(u8 *a, s64 len, int align, vec_cmpret_t *result)
{
    u8 uca;
    u64 bitend, ulla, fs;
    s64 lenbyte;
    u32 uia;
    u16 usa;
    
    bitend = len%8; 
    // //要比较的位不包含此位,也可理解为最后一个字节还需要比较多少位
    lenbyte = len/8;

    switch(align)
    {
        case 8:
            while(lenbyte>=8)
            {
                ulla = *(u64 *)a;a += 8;
                if(ulla != 0)
                {
                    fs = ullfind_firt_set(ulla);
                    result->smallfs += fs;
                    result->finish = 1;
                    return ;
                }
                result->smallfs += 64;
                lenbyte -= 8;
            }
        case 4:
            while(lenbyte>=4)
            {
                uia = *(u32 *)a;a += 4;
                if(uia != 0)
                {
                    fs = uifind_firt_set(uia);
                    result->smallfs += fs;
                    result->finish = 1;
                    return ;
                }
                result->smallfs += 32;
                lenbyte -= 4;
            }
        case 2:
            while(lenbyte>=2)
            {
                usa = *(u16 *)a;a += 2;
                if(usa != 0)
                {
                    fs = usfind_firt_set(usa);
                    result->smallfs += fs;
                    result->finish = 1;
                    return ;
                }
                result->smallfs += 16;
                lenbyte -= 2;
            }            
        case 1:
            while(lenbyte>=1)
            {
                uca = *a;a ++;
                if(uca != 0)
                {
                    fs = ucfind_firt_set(uca);
                    result->smallfs += fs;
                    result->finish = 1;
                    return ;
                }
                result->smallfs += 8;
                lenbyte --;
            }            
            break;
        default:
            printk("\n%s\t%d", __FUNCTION__, __LINE__);
            BUG();
            
    }

    if(bitend)
    {
        uca = *a;
        fs = ucfind_firt_set(uca);
        if(fs >= bitend)
        {
            result->smallfs += bitend;
        }
        else
        {
            result->smallfs += fs;
        }
    }
//    result->finish = 1;
    return;
}

int align_compare(u8 *a, u8 *b, s64 len, int align, vec_cmpret_t *result)
{
    u8 uca, ucb,ucres, bitend;
    u64 ulla, ullb, ullres, fs;
    s64 lenbyte;
    u32 uia, uib, uires;
    u16 usa, usb, usres;
    int ret=0;
    u8 *c;
    
    bitend = len%8;
    lenbyte = len/8;

    switch(align)
    {
        case 8:
            while(lenbyte>=8)
            {
                ulla = *(u64 *)a;a += 8;
                ullb = *(u64 *)b;b += 8;
                
                if(ulla == ullb)
                {
                    result->pos += 64;
                    lenbyte -= 8;
                    continue;
                }
                else
                {
                    ullres = ulla^ullb;
                    fs = ullfind_firt_set(ullres);
                    result->smallfs = result->pos;
                    result->pos += fs;
                //    result->flag = 0;
                    if(ulla > ullb)
                    {
                        ret = 1;
                        c = b;
                        ulla = ullb;
                    }
                    else
                    {
                        ret = -1;
                        c = a;
                    }
                    ulla = ulla << fs;
                    if(ulla != 0)
                    {
                        fs = ullfind_firt_set(ulla);
                        result->smallfs = result->pos + fs;
                        result->finish = 1;
                    }
                    else
                    {
                        result->smallfs += 64;
                        find_smallfs(c, (lenbyte<<3)+bitend, 8, result);
                        //result->finish = 1;
                    }                
                    return ret;
                }
            }
        case 4:
            while(lenbyte>=4)
            {
                uia = *(u32 *)a;a += 4;
                uib = *(u32 *)b;b += 4;
                
                if(uia == uib)
                {
                    result->pos += 32;
                    lenbyte -= 4;
                    continue;
                }
                else
                {
                    uires = uia^uib;
                    fs = uifind_firt_set(uires);
                    result->smallfs = result->pos;
                    result->pos += fs;

                    if(uia > uib)
                    {
                        ret = 1;
                        c = b;
                        uia = uib;
                    }
                    else
                    {
                        ret = -1;
                        c = a;
                    }
                    uia = uia << fs;
                    if(uia != 0)
                    {
                        fs = uifind_firt_set(uia);
                        result->smallfs = result->pos + fs;
                        result->finish = 1;
                    }
                    else
                    {
                        result->smallfs += 32;
                        find_smallfs(c, (lenbyte<<3)+bitend, 4, result);
                        //result->finish = 1;
                    }                
                    return ret;
                }
            }

        case 2:
            while(lenbyte>=2)
            {
                usa = *(u16 *)a;a += 2;
                usb = *(u16 *)b;b += 2;
                
                if(usa == usb)
                {
                    result->pos += 16;
                    lenbyte -= 2;
                    continue;
                }
                else
                {
                    usres = usa^usb;
                    fs = usfind_firt_set(usres);
                    result->smallfs = result->pos;
                    result->pos += fs;

                    if(usa > usb)
                    {
                        ret = 1;
                        c = b;
                        usa = usb;
                    }
                    else
                    {
                        ret = -1;
                        c = a;
                    }
                    usa = usa << fs;
                    if(usa != 0)
                    {
                        fs = usfind_firt_set(usa);
                        result->smallfs = result->pos + fs;
                        result->finish = 1;
                    }
                    else
                    {
                        result->smallfs += 16;
                        find_smallfs(c, (lenbyte<<3)+bitend, 2, result);
                        //result->finish = 1;
                    }                
                    return ret;
                }
            }            
        case 1:
            while(lenbyte>=1)
            {
                uca = *(u64 *)a;a ++;
                ucb = *(u64 *)b;b ++;
                
                if(uca == ucb)
                {
                    result->pos += 8;
                    lenbyte --;
                    continue;
                }
                else
                {
                    ucres = uca^ucb;
                    fs = ucfind_firt_set(ucres);
                    result->smallfs = result->pos;
                    result->pos += fs;
            
                    if(uca > ucb)
                    {
                        ret = 1;
                        c = b;
                        uca = ucb;
                    }
                    else
                    {
                        ret = -1;
                        c = a;
                    }
                    uca = uca << fs;
                    if(uca != 0)
                    {
                        fs = ucfind_firt_set(uca);
                        result->smallfs = result->pos + fs;
                        result->finish = 1;
                    }
                    else
                    {
                        result->smallfs += 8;
                        find_smallfs(c, (lenbyte<<3)+bitend, 1, result);
                        //result->finish = 1;
                    }                
                    return ret;
                }
            }
            break;
        default:
            printk("\n%s\t%d", __FUNCTION__, __LINE__);
            BUG();        
    }

    if(bitend)
    {
        uca = *a;
        ucb = *b;
        /*最后一个字节中多余的低位bit清零*/
        uca = uca >> (8-bitend) << (8-bitend);
        ucb = ucb >> (8-bitend) << (8-bitend);
        if(uca == ucb)
        {
            result->pos += bitend;
            result->smallfs = result->pos;
            ret = 0;
        }
        else
        {
            ucres = uca^ucb;
            /*无关位已被抹掉，因为uca!=ucb，所以必然能在有效位中找到set bit*/
            fs = ucfind_firt_set(ucres);
            result->smallfs = result->pos;
            result->pos += fs;
            
            if(uca > ucb)
            {
                ret = 1;
                c = b;
                uca = ucb;
            }
            else
            {
                ret = -1;
                c = a;
            }
            uca = uca << fs;
            if(uca != 0)
            {
                fs = ucfind_firt_set(uca);
                result->smallfs = result->pos + fs;
                result->finish = 1;
            }
            else
            {
                result->smallfs += bitend;
            //    result->finish = 1;
            }                
            
        }
    }

    return ret;

}

u64 find_fs(char *a, u64 start, u64 len)
{
    s64 align;
    u64 fs, ret, ulla;
    s64 lenbyte;
    u32 uia;
    u16 usa;
    u8 uca, bitstart, bitend;
    u8 *acstart;
    int ret = 0;

    fs = 0;
    bitstart = start%8;
    bitend = (bitstart + len)%8;
    acstart = (u8 *)a + start/8;
    lenbyte =  (bitstart + len)/8;
    ret = start;

    if(lenbyte == 0)
    {
        uca = *acstart;
        uca &= (1<<(8-bitstart))-1;
        fs = ucfind_firt_set(uca); 
        if(fs >= bitend)
        {
            /*指向范围外第一位*/
            return start + len;
        }
        else
        {
            return start + fs - bitstart;
        }
    }
    
    if(bitstart != 0)
    {
        lenbyte--;
        uca = *acstart;acstart++;
        /*第一个字节中多余的bit清零*/
        uca &= (1<<(8-bitstart))-1;
        if(uca == 0)
        {
            ret += 8 - bitstart;
        }
        else
        {
            fs = ucfind_firt_set(uca);
            ret += fs-bitstart;
            return ret;
        }
    }

    if((align=(unsigned long)acstart%8) != 0)
    {
        align = 8-align;
        if(lenbyte < align)
        {
            while(lenbyte>=1)
            {
                uca = *acstart;acstart ++;
                if(uca != 0)
                {
                    fs = ucfind_firt_set(uca);
                    ret += fs;
                    return ret;
                }
                ret += 8;
                lenbyte --;
            }
            return ret;
        }
        else
        {
            while(align>=1)
            {
                uca = *acstart;acstart ++;
                if(uca != 0)
                {
                    fs = ucfind_firt_set(uca);
                    ret += fs;
                    return ret;
                }
                ret += 8;
                align --;
            }
            lenbyte -= align;

        }
    }

    while(lenbyte>=8)
    {
        ulla = *(u64 *)acstart;acstart += 8;
        if(ulla != 0)
        {
            fs = ullfind_firt_set(ulla);
            ret += fs;
            return ret;
        }
        ret += 64;
        lenbyte -= 8;
    }
    while(lenbyte>=4)
    {
        uia = *(u32 *)acstart;acstart += 4;
        if(uia != 0)
        {
            fs = uifind_firt_set(uia);
            ret += fs;
            return ret;
        }
        ret += 32;
        lenbyte -= 4;
    }
    while(lenbyte>=2)
    {
        usa = *(u16 *)acstart;acstart += 2;
        if(usa != 0)
        {
            fs = usfind_firt_set(usa);
            ret += fs;
            return ret;
        }
        ret += 16;
        lenbyte -= 2;
    }            
    while(lenbyte>=1)
    {
        uca = *acstart;acstart ++;
        if(uca != 0)
        {
            fs = ucfind_firt_set(uca);
            ret += fs;
            return ret;
        }
        ret += 8;
        lenbyte --;
    }            
    if(bitend)
    {
        uca = *acstart;
        fs = ucfind_firt_set(uca);
        if(fs >= bitend)
        {
            ret += bitend;
        }
        else
        {
            ret += fs;
        }
    }
    return ret; 
}

#else
void find_smallfs(u8 *a, s64 len, int align, vec_cmpret_t *result)
{
     u8 uca;
     u64 bitend, ulla, fs;
     s64 lenbyte;
     u32 uia;
     u16 usa;
     int i;
     
     bitend = len%8; 
     // //要比较的位不包含此位,也可理解为最后一个字节还需要比较多少位
     lenbyte = len/8;
 
     switch(align)
     {
         case 8:
             while(lenbyte>=8)
             {
                 ulla = *(u64 *)a;
                 if(ulla != 0)
                 {
                     for(i=0;i<8;i++)
                     {
                         uca = *a;
                         if(uca != 0)
                         {
                             fs = ucfind_firt_set(uca);
                             result->smallfs += fs;
                             result->finish = 1;
                             return ;
                         }
                         result->smallfs += 8;
                         a++;
                     }
                 }
                 result->smallfs += 64;
                 lenbyte -= 8;
                 a += 8;
             }
         case 4:
             while(lenbyte>=4)
             {
                 uia = *(u32 *)a;
                 if(uia != 0)
                 {
                     for(i=0;i<4;i++)
                     {
                         uca = *a;
                         if(uca != 0)
                         {
                             fs = ucfind_firt_set(uca);
                             result->smallfs += fs;
                             result->finish = 1;
                             return ;
                         }
                         result->smallfs += 8;
                         a++;
                     }
 
                 }
                 result->smallfs += 32;
                 lenbyte -= 4;
                 a += 4;
             }
         case 2:
             while(lenbyte>=2)
             {
                 usa = *(u16 *)a;
                 if(usa != 0)
                 {
                     for(i=0;i<2;i++)
                     {
                         uca = *a;
                         if(uca != 0)
                         {
                             fs = ucfind_firt_set(uca);
                             result->smallfs += fs;
                             result->finish = 1;
                             return ;
                         }
                         result->smallfs += 8;
                         a++;
                     }
                 }
                 result->smallfs += 16;
                 lenbyte -= 2;
                 a += 2;
             }             
         case 1:
             while(lenbyte>=1)
             {
                 uca = *a;a ++;
                 if(uca != 0)
                 {
                     fs = ucfind_firt_set(uca);
                     result->smallfs += fs;
                     result->finish = 1;
                     return ;
                 }
                 result->smallfs += 8;
                 lenbyte --;
             }             
             break;
         default:
             printk("\n%s\t%d", __FUNCTION__, __LINE__);
             BUG();
             
     }
 
     if(bitend)
     {
         uca = *a;
         fs = ucfind_firt_set(uca);
         if(fs >= bitend)
         {
             result->smallfs += bitend;
         }
         else
         {
             result->smallfs += fs;
         }
     }
 //  result->finish = 1;
     return;
 
}

int align_compare(u8 *a, u8 *b, s64 len, int align, vec_cmpret_t *result)
{
    u8 uca, ucb,ucres, bitend;
    u64 ulla, ullb, fs;
    s64 lenbyte;
    u32 uia, uib;
    u16 usa, usb;
    int ret = 0;
    u8 *c;
    
    bitend = len%8;
    lenbyte = len/8;

    switch(align)
    {
        case 8:
            while(lenbyte>=8)
            {
                ulla = *(u64 *)a;
                ullb = *(u64 *)b;
                
                if(ulla == ullb)
                {
                    result->pos += 64;
                    lenbyte -= 8;
                }
                else
                {
                    goto perbyte;
                }
                a += 8;
                b += 8;
            }
        case 4:
            while(lenbyte>=4)
            {
                uia = *(u32 *)a;
                uib = *(u32 *)b;
                
                if(uia == uib)
                {
                    result->pos += 32;
                    lenbyte -= 4;
                }
                else
                {
                    goto perbyte;
                }
                a += 4;
                b += 4;
            }

        case 2:
            while(lenbyte>=2)
            {
                usa = *(u16 *)a;
                usb = *(u16 *)b;
                
                if(usa == usb)
                {
                    result->pos += 16;
                    lenbyte -= 2;
                }
                else
                {
                    goto perbyte;
                }
                a += 2;
                b += 2;
            }            
perbyte:case 1:
            while(lenbyte>=1)
            {
                uca = *(u64 *)a;
                ucb = *(u64 *)b;
                
                if(uca == ucb)
                {
                    result->pos += 8;
                    lenbyte --;
                }
                else
                {
                    ucres = uca^ucb;
                    fs = ucfind_firt_set(ucres);
                    result->smallfs = result->pos;
                    result->pos += fs;
            
                    if(uca > ucb)
                    {
                        ret = 1;
                        c = b;
                        uca = ucb;
                    }
                    else
                    {
                        ret = -1;
                        c = a;
                    }
                    uca = uca << fs;
                    if(uca != 0)
                    {
                        fs = ucfind_firt_set(uca);
                        result->smallfs = result->pos + fs;
                        result->finish = 1;
                    }
                    else
                    {
                        result->smallfs += 8;
                        c++;
                        lenbyte--;
                        find_smallfs(c, (lenbyte<<3)+bitend, 1, result);
                        //result->finish = 1;
                    }                
                    return ret;
                }
                a++;
                b++;
            }
            break;
        default:
            printk("\n%s\t%d", __FUNCTION__, __LINE__);
            BUG();        
    }

    if(bitend)
    {
        uca = *a;
        ucb = *b;
        /*最后一个字节中多余的低位bit清零*/
        uca = uca >> (8-bitend) << (8-bitend);
        ucb = ucb >> (8-bitend) << (8-bitend);
        if(uca == ucb)
        {
            result->pos += bitend;
            result->smallfs = result->pos;
            ret = 0;
        }
        else
        {
            ucres = uca^ucb;
            /*无关位已被抹掉，因为uca!=ucb，所以必然能在有效位中找到set bit*/
            fs = ucfind_firt_set(ucres);
            result->smallfs = result->pos;
            result->pos += fs;
            
            if(uca > ucb)
            {
                ret = 1;
                c = b;
                uca = ucb;
            }
            else
            {
                ret = -1;
                c = a;
            }
            uca = uca << fs;
            if(uca != 0)
            {
                fs = ucfind_firt_set(uca);
                result->smallfs = result->pos + fs;
                result->finish = 1;
            }
            else
            {
                result->smallfs += bitend;
            //    result->finish = 1;
            }                
            
        }
    }

    return ret;

}

u64 find_fs(char *a, u64 start, u64 len)
{
    s64 align;
    u64 fs, ret, ulla;
    s64 lenbyte;
    u32 uia;
    u16 usa;
    u8 uca, bitstart, bitend;
    u8 *acstart;
    int i;

//    perbyteCnt = perllCnt = perintCnt = pershortCnt = 0;
    fs = 0;
    bitstart = start%8;
    bitend = (bitstart + len)%8;
    acstart = (u8 *)a + start/8;
    lenbyte =  (bitstart + len)/8;
    ret = start;

    if(lenbyte == 0)
    {
        uca = *acstart;
        uca &= (1<<(8-bitstart))-1;
        fs = ucfind_firt_set(uca); 
        if(fs >= bitend)
        {
            /*指向范围外第一位*/
            return start + len;
        }
        else
        {
            return start + fs - bitstart;
        }
    }
    
    if(bitstart != 0)
    {
        lenbyte--;
        uca = *acstart;acstart++;
        /*第一个字节中多余的bit清零*/
        uca &= (1<<(8-bitstart))-1;
        if(uca == 0)
        {
            ret += 8 - bitstart;
        }
        else
        {
            fs = ucfind_firt_set(uca);
            ret += fs-bitstart;
            return ret;
        }
    }

    if((align=(unsigned long)acstart%8) != 0)
    {
        align = 8-align;
        if(lenbyte < align)
        {
            while(lenbyte>=1)
            {
                uca = *acstart;acstart ++;
                if(uca != 0)
                {
                    fs = ucfind_firt_set(uca);
                    ret += fs;
                    return ret;
                }
                ret += 8;
                lenbyte --;
            }
            return ret;
        }
        else
        {
            lenbyte -= align;
            while(align>=1)
            {
                uca = *acstart;acstart ++;
                if(uca != 0)
                {
                    fs = ucfind_firt_set(uca);
                    ret += fs;
                    return ret;
                }
                ret += 8;
                align --;
            }
        }
    }

    while(lenbyte>=8)
    {
        ulla = *(u64 *)acstart;
        if(ulla != 0)
        {
            for(i=0;i<8;i++)
            {
                uca = *acstart;
                if(uca != 0)
                {
                    fs = ucfind_firt_set(uca);
                    ret += fs;
                    return ret;
                }
                ret += 8;
                acstart++;
            }
        }
        ret += 64;
        lenbyte -= 8;
        acstart += 8;
    }
    while(lenbyte>=4)
    {
        uia = *(u32 *)acstart;
        if(uia != 0)
        {
            for(i=0;i<4;i++)
            {
                uca = *acstart;
                if(uca != 0)
                {
                    fs = ucfind_firt_set(uca);
                    ret += fs;
                    return ret;
                }
                ret += 8;
                acstart++;
            }
        }
        ret += 32;
        lenbyte -= 4;
        acstart += 4;
    }
    while(lenbyte>=2)
    {
        usa = *(u16 *)acstart;
        if(usa != 0)
        {
            for(i=0;i<2;i++)
            {
                uca = *acstart;
                if(uca != 0)
                {
                    fs = ucfind_firt_set(uca);
                    ret += fs;
                    return ret;
                }
                ret += 8;
                acstart++;
            }
        }
        ret += 16;
        lenbyte -= 2;
        acstart += 2;
    }            
    while(lenbyte>=1)
    {
        uca = *acstart;
        if(uca != 0)
        {
            fs = ucfind_firt_set(uca);
            ret += fs;
            return ret;
        }
        ret += 8;
        lenbyte --;
        acstart ++;
    }            
    if(bitend)
    {
        uca = *acstart;
        fs = ucfind_firt_set(uca);
        if(fs >= bitend)
        {
            ret += bitend;
        }
        else
        {
            ret += fs;
        }
    }
    return ret; 
}

#endif


#if 1
/*bitend: 比较不包含此位*/
int bit_inbyte_cmp(u8 *a, u8 *b,u8 bitstart, u8 bitend, vec_cmpret_t *result)
{
    u8 uca, ucb, cres ,fs;
    int ret;

    uca = *a;
    ucb = *b;

    uca = (u8)(uca << bitstart) >> bitstart;
    ucb = (u8)(ucb << bitstart) >> bitstart;

    uca = (u8)(uca >> (8-bitend)) << (8-bitend);        
    ucb = (u8)(ucb >> (8-bitend)) << (8-bitend);
    
    if(uca == ucb)
    {
        result->pos += bitend - bitstart;
        ret = 0;
    }
    else
    {
        cres = uca^ucb;
        fs = ucfind_firt_set(cres);//fs不可能大于bitend;
        /*如果相等，返回的是下一个要比较的位；不等，返回的是哪一位出现的不等*/
        result->pos += fs-bitstart;
        if(uca > ucb)
        {
            uca = ucb;
            ret = 1;
        }
        else
        {
            ret = -1;
        }
        /*计算较小数据相同位后的first set*/
        result->smallfs = result->pos;
        uca = uca << fs;
        bitend = bitend - fs;
        fs = ucfind_firt_set(uca);
        /*bitend后的位已经抹零*/
        if(fs == 8)
        {
            result->smallfs += bitend;
        }
        else
        {
            result->smallfs += fs;
        }
    }

    result->finish = 1;
    return ret;
}

int diff_identify(char *a, char *b,u64 start, u64 len, vec_cmpret_t *result)
{
    s64 align;
    u64 fz, fs;
    s64 lenbyte;
    u8 uca, ucb, cres,bitstart, bitend;
    u8 *acstart;
    u8 *bcstart;
    int ret = 0;

//    perbyteCnt = perllCnt = perintCnt = pershortCnt = 0;
    fs = fz = 0;
    bitstart = start%8;
    bitend = (bitstart + len)%8;
    acstart = (u8 *)a + start/8;
    bcstart = (u8 *)b + start/8;
    lenbyte =  (bitstart + len)/8;
    result->pos = start;
    result->finish = 0;

    if(lenbyte == 0)
        return bit_inbyte_cmp(acstart, bcstart, bitstart, bitend, result);

    if(bitstart != 0)
    {
        lenbyte--;
        uca = *acstart;acstart++;
        ucb = *bcstart;bcstart++;
        /*第一个字节中多余的bit清零*/
        uca = uca << bitstart;
        uca = uca >> bitstart;
        ucb = ucb << bitstart;
        ucb = ucb >> bitstart;
        
        if(uca == ucb)
        {
            result->pos += 8-bitstart;
        }
        else
        {
            cres = uca^ucb;
            fs = ucfind_firt_set(cres);
            /*这样的话，如果相等，返回的是下一个要比较的位；不等，返回的是哪一位出现的不等*/
            result->pos += fs-bitstart;
            if(uca > ucb)
            {
                ret =  1;
                acstart = bcstart;
                uca = ucb;
            }
            else
            {
                ret = -1;
            }
            result->smallfs = result->pos;
                        
            uca = uca << fs;
            if(uca == 0)
            {
                result->smallfs += 8 - fs;
            }
            else
            {
                fs = ucfind_firt_set(uca);
                result->smallfs += fs;
                result->finish = 1;
                return ret;
            }
            if((align=(unsigned long)acstart%8) != 0)
            {
                align = 8-align;
                if(lenbyte < align)
                {
                    find_smallfs(acstart, (lenbyte<<3)+bitend, 1, result);
                }
                else
                {
                    find_smallfs(acstart, align<<3, 1, result);
                    /*后一个条件等价于判断 bitend == 0 && lenbyte == align*/
                    if(result->finish != 1 && result->smallfs < start + len )
                    {
                        acstart += align;
                        lenbyte -= align;
                        find_smallfs(acstart, (lenbyte<<3)+bitend, 8, result);
                    }
                }
            }
            else
            {
                find_smallfs(acstart, (lenbyte<<3)+bitend, 8, result);
            }
            result->finish = 1;
            return ret;
        }
        
    }

    if((unsigned long)acstart%8 == (unsigned long)bcstart%8)
    {
        if((align=(unsigned long)acstart%8) != 0)
        {
            align = 8-align;
            if(lenbyte < align)
            {
                ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 1, result);
            }
            else
            {
                ret = align_compare(acstart, bcstart, align<<3, 1, result);
                if(result->finish == 1)
                    return ret;
                else
                {
                    acstart += align;
                    bcstart += align;
                    lenbyte -= align;
                    if(ret == 0)
                    {
                        ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 8, result);
                    }
                    else if(ret == 1)
                    {
                        find_smallfs(bcstart, (lenbyte<<3) + bitend, 8, result);
                    }
                    else
                    {
                        find_smallfs(acstart, (lenbyte<<3) + bitend, 8, result);
                    }
                }
            }
        }
        else
        {
            ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 8, result);
        }
    }
    else if((unsigned long)acstart%4 == (unsigned long)bcstart%4)
    {
        if((align=(unsigned long)acstart%4) != 0)
        {
            align = 4-align;
            if(lenbyte < align)
            {
                ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 1, result);
            }
            else
            {
                ret = align_compare(acstart, bcstart, align<<3, 1, result);
                if(result->finish == 1)
                    return ret;
                else
                {
                    acstart += align;
                    bcstart += align;
                    lenbyte -= align;
                    if(ret == 0)
                    {
                        ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 8, result);
                    }
                    else if(ret == 1)
                    {
                        find_smallfs(bcstart, (lenbyte<<3) + bitend, 4, result);
                    }
                    else
                    {
                        find_smallfs(acstart, (lenbyte<<3) + bitend, 4, result);
                    }
                }
            }
        }
        else
        {
            ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 4, result);
        }
    }

    else if((unsigned long)acstart%2 == (unsigned long)bcstart%2)
    {
        if((align=(unsigned long)acstart%2) != 0)
        {
            align = 1;
            if(lenbyte < align)
            {
                ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 1, result);
            }
            else
            {
                ret = align_compare(acstart, bcstart, align<<3, 1, result);
                if(result->finish == 1)
                    return ret;
                else
                {
                    acstart += align;
                    bcstart += align;
                    lenbyte -= align;
                    if(ret == 0)
                    {
                        ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 2, result);
                    }
                    else if(ret == 1)
                    {
                        find_smallfs(bcstart, (lenbyte<<3) + bitend, 2, result);
                    }
                    else
                    {
                        find_smallfs(acstart, (lenbyte<<3) + bitend, 2, result);
                    }
                }
            }
        }
        else
        {
            ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 2, result);
        }
    }
    else
    {
        ret = align_compare(acstart, bcstart, (lenbyte<<3) + bitend, 1, result);    
    }
    result->finish = 1;
    return ret; 
}
#endif

spt_thrd_t *spt_thread_init(int thread_num)
{
    g_thrd_h = kmalloc(sizeof(spt_thrd_t),GFP_ATOMIC);
    if(g_thrd_h == NULL)
    {
        spt_debug("OOM\r\n");
        return NULL;
    }
    g_thrd_h->thrd_total= thread_num;
    g_thrd_h->black_white_map = SPT_BWMAP_ALL_ONLINE;
    g_thrd_h->online_map = 0;
    g_thrd_h->tick = 0;
   
    return g_thrd_h;
}

void spt_atomic64_set_bit(int nr, atomic64_t *v)
{
    u64 tmp,old_val, new_val;

    tmp = 1ull<<nr;
    do
    {
        old_val = atomic64_read(v);
        new_val = tmp|old_val;

    }while(old_val != atomic64_cmpxchg(v, old_val, new_val));

    return;
}
/*返回清除后的值*/
u64 spt_atomic64_clear_bit_return(int nr, atomic64_t *v)
{
    u64 tmp,old_val, new_val;

    tmp = ~(1ull<<nr);
    do
    {
        old_val = atomic64_read(v);
        new_val = tmp&old_val;

    }while(old_val != atomic64_cmpxchg(v, old_val, new_val));

    return new_val;
}


int spt_thread_start(int thread)
{
#if 0    
    int cnt,ret;
    cnt = rsv_list_fill_cnt(pgclst, g_thrd_id);
    if(cnt > 0)
    {
        ret = fill_in_rsv_list(pgclst, cnt, g_thrd_id);
        if(ret != SPT_OK)
            return ret;
   }
#endif 
    spt_atomic64_set_bit(thread, (atomic64_t *)&g_thrd_h->online_map);
    smp_mb();
    return SPT_OK;
}

void spt_thread_exit(int thread)
{
    u64 olmap, bwmap, new_val;

    smp_mb();
    olmap = spt_atomic64_clear_bit_return(thread,(atomic64_t *)&g_thrd_h->online_map);
    do
    {
        bwmap = atomic64_read((atomic64_t *)&g_thrd_h->black_white_map);
        if(bwmap == 0)
            return;
        new_val = bwmap & olmap;
    }while(bwmap != atomic64_cmpxchg((atomic64_t *)&g_thrd_h->black_white_map, bwmap, new_val));
    if(new_val == 0)
    {
        atomic_add(1, (atomic_t *)&g_thrd_h->tick);
        if(0 != atomic64_cmpxchg((atomic64_t *)&g_thrd_h->black_white_map, 
                                    0, SPT_BWMAP_ALL_ONLINE))
        {
            spt_debug("@@@@@@@@@@@@@@@@@@Err\r\n");
            BUG();
        }
    }
    return;
}

void spt_thread_wait(int n, int thread)
{
    u32 tick;
    tick = atomic_read((atomic_t *)&g_thrd_h->tick);
    while(atomic_read((atomic_t *)&g_thrd_h->tick) - tick < n)
    {
        spt_thread_start(thread);
        spt_thread_exit(thread);
    }
    return;
}

void debug_print_2(u8 *p, u32 size)
{
    u8 data, k;
    int i;
    for(k = 0; k < size; k++)
    {
        data = *(p+k);
        for (i = 7; i >= 0; i--)
        {
            if(data & (1 << i))
                printk("1");
            else
                printk("0");
        }
    }
    printk("\r\n");
    return;
}

void debug_get_final_vec(cluster_head_t *pclst, spt_vec* pvec, spt_vec_f *pvec_f, 
                            u64 sp, u32 data, int direction)
{
    //下路标没有dataid，所以对于路标来说，dataid不重要
    if(pvec->type == SPT_VEC_SIGNPOST)
    {
        if(pvec->ext_sys_flg == SPT_VEC_SYS_FLAG_DATA)
        {
            spt_debug("found SPT_VEC_SYS_FLAG_DATA vec\r\n");
            while(1);
        }
        pvec_f->data = data;
        pvec_f->pos = pvec->idx << SPT_VEC_SIGNPOST_BIT;
        if(direction == SPT_DOWN)
        {
            pvec_f->down = pvec->rd;
            pvec_f->right = SPT_INVALID;
        }
        else
        {
            pvec_f->right = pvec->rd;
            pvec_f->down = SPT_INVALID;
        }
        return;
    }

    pvec_f->down = pvec->down;;
    pvec_f->pos = pvec->pos + sp;
    if(pvec->type == SPT_VEC_DATA)
    {
        pvec_f->data = pvec->rd;
        pvec_f->right = SPT_NULL;
        return;
    }
    pvec_f->right = pvec->rd;
    if(data != SPT_INVALID)
    {
        pvec_f->data = data;
    }
    else
    {
        pvec_f->data = get_data_id(pclst, pvec);
        if(pvec_f->data == SPT_DO_AGAIN)
        {
            spt_debug("invalid found\r\n");
            while(1);
        }
    }
    return;
}

void debug_vec_print(spt_vec_f *pvec_f, int vec_id)
{
    printk("{down:%d, right:%d, pos:%lld, data:%d}[%d]\r\n",pvec_f->down, pvec_f->right, pvec_f->pos, pvec_f->data, vec_id);
    return;
}

void debug_id_vec_print(cluster_head_t *pclst, int id)
{
    return;
}
void debug_dh_ext_print(spt_dh_ext *p)
{
    printk("hang_vec_id:%d\tlower_cluster:%p", p->hang_vec, p->plower_clst);
}
void debug_data_print(char *pdata)
{
    int i;
    u8 *p = (u8 *)pdata;
#if 0
	for(i = 0; i < DATA_SIZE; i++,p++)
    {
        if(i%8 == 0)
        {
            printk("\r\n");
        }
        printk("%02x ", *p);
    }
    printk("\r\n");
#endif    
	printk("print data addr is %p\r\n",pdata);
return;
}
void debug_pdh_data_print(cluster_head_t *pclst, spt_dh *pdh)
{
    spt_dh_ext *pdh_ext;
    
    if(pclst->is_bottom)
        debug_data_print(pdh->pdata);
    else
    {
        pdh_ext = (spt_dh_ext *)pdh->pdata;
        debug_data_print(pdh_ext->data);
    }
}
travl_info *debug_travl_stack_pop(spt_stack *p_stack)
{
    return (travl_info *)spt_stack_pop(p_stack);
}


void debug_travl_stack_destroy(spt_stack *p_stack)
{
    travl_info *node;
    while(!spt_stack_empty(p_stack))
    {
        node = debug_travl_stack_pop(p_stack);
        kfree(node);
    }
    spt_stack_destroy(p_stack);
    return;
}


void debug_travl_stack_push(spt_stack *p_stack, spt_vec_f *pvec_f, long long signpost)
{
    travl_info *node;
    node = (travl_info *)kmalloc(sizeof(travl_info),GFP_ATOMIC);
    if(node == NULL)
    {
        printk("\r\nOUT OF MEM        %d\t%s", __LINE__, __FUNCTION__);
        debug_travl_stack_destroy(p_stack);
        return;
    }
    node->signpost = signpost;
    node->vec_f = *pvec_f;
    spt_stack_push(p_stack,node);
    return;
}


void debug_cluster_travl(cluster_head_t *pclst)
{
    spt_stack stack = {0};
    spt_stack *pstack = &stack;
    //u8 data[DATA_SIZE] = {0};
    int cur_data, cur_vecid;
    spt_vec *pcur;
    spt_vec_f st_vec_f;
    spt_dh *pdh;
    char *pcur_data = NULL;
    u64 signpost;
    travl_info *pnode;
    u32 ref_total;
    char *data;

    ref_total = 0;
    signpost = 0;
    spt_stack_init(pstack, 1000);
    cur_data = SPT_INVALID;

    cur_vecid = pclst->vec_head;
    pcur = (spt_vec *)vec_id_2_ptr(pclst, pclst->vec_head);

    debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
    if(pcur->down == SPT_NULL && pcur->rd == SPT_NULL)
    {
        printk("cluster is null\r\n");
        debug_travl_stack_destroy(pstack);
        return;
    }

    cur_data = st_vec_f.data;
    if(cur_data != SPT_NULL)
    {
        pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
        pcur_data = pdh->pdata;
        ref_total += pdh->ref;
    }
    debug_vec_print(&st_vec_f, cur_vecid);
#if 0
    if(st_vec_r.pos == 0)
    {
        printk("only one vec in this cluster\r\n");
        debug_vec_print(&st_vec_r, cur_vec);
        debug_data_print(pcur_data);
        return;        
    }
#endif
    debug_travl_stack_push(pstack,&st_vec_f, signpost);

    while (1)
    {
        if(pcur->type != SPT_VEC_DATA)
        {
            cur_vecid = st_vec_f.right;
            pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
            debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
            debug_vec_print(&st_vec_f, cur_vecid);
            if(pcur->type == SPT_VEC_SIGNPOST)
            {
                signpost = st_vec_f.pos;
                
                cur_vecid = st_vec_f.right;
                pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
                debug_vec_print(&st_vec_f, cur_vecid);                
                if(pcur->type == SPT_VEC_SIGNPOST)
                {
                    spt_debug("double signpost vec found!!\r\n");
                    while(1);
                }
            }
            debug_travl_stack_push(pstack,&st_vec_f, signpost);
            #if 0
            pos = st_vec_f.pos;
            if(pos != 0)
                spt_bit_cpy(data, pcur_data, bit, pos);
            else
                spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            bit += st_vec_r.pos;
            #endif
        }
        else
        {            
            //debug_vec_print(&st_vec_r, cur_vec);
            //单个数据遍历完
            //spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            //此时data应该等于插入的某个数
            //debug_vec_print(&st_vec_f, cur_vecid);
            if(pcur_data != NULL)//head->right为空时，pcur_data等于空
            {
                #if 0
                if(memcmp(data, pcur_data, DATA_SIZE)!=0)
                {
                    printk("\r\nErr    %d\t%s", __LINE__, __FUNCTION__);
                }
                #endif
                //pdh = (spt_dh *)(pcur_data-sizeof(spt_dh));
//                pdh->rank = rank;
//                rank--;
                data = get_real_data(pclst, pcur_data);
                if(!pclst->is_bottom)
                {
                    debug_dh_ext_print((spt_dh_ext *)pcur_data);
                }
                debug_data_print(data);
				if(pclst->is_bottom)
				{
					unsigned char *data_mem = NULL;
					int i = 0;
					data_mem = kmap(data);
					for(i = 0 ; i < 4096 ;i++)
					{
						if(0 == (i %32 ))
						{
							printk("\r\n");
						}
						printk("%02x ",*(data_mem+i));

					}
					kunmap(data);
				}
            }
            
            if(spt_stack_empty(pstack))
            {
                break;
            }
            while(1)
            {
                pnode = debug_travl_stack_pop(pstack);
                if(pnode == (travl_info *)-1)
                {
                    printk("\r\n");
                    debug_travl_stack_destroy(pstack);
                    printk("\r\n@@@@@@@@ data_total:%d\tref_total:%d@@@@@@@@\r\n", 
                    pclst->data_total, ref_total);                    
                    return;
                }
                signpost = pnode->signpost;
                if(pnode->vec_f.down != SPT_NULL)
                {
                    cur_vecid = pnode->vec_f.down;
                    pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                    debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, SPT_INVALID, SPT_DOWN);
                    //debug_vec_print(&st_vec_f, cur_vecid);
                    
                    if(pcur->type == SPT_VEC_SIGNPOST)
                    {
                        signpost = st_vec_f.pos;
                        cur_vecid = pnode->vec_f.down;
                        pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                        debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, SPT_INVALID, SPT_DOWN);
                        if(pcur->type == SPT_VEC_SIGNPOST)
                        {
                            spt_debug("double signpost vec found!!\r\n");
                            while(1);
                        }
                    }
                    cur_data = st_vec_f.data;
                    pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
                    pcur_data = pdh->pdata;
                    ref_total += pdh->ref;
                    printk("\r\n@@data[%p],bit:%lld\r\n", pcur_data, st_vec_f.pos);
                    debug_vec_print(&st_vec_f, cur_vecid);
                    debug_travl_stack_push(pstack,&st_vec_f, signpost);
                    kfree(pnode);
                    break;
                }
                kfree(pnode);
            }
        }
    }
    debug_travl_stack_destroy(pstack);
    printk("\r\n@@@@@@@@ data_total:%d\tref_total:%d@@@@@@@@\r\n", 
    pclst->data_total, ref_total);
    return;
}

u32 debug_thrd_vec_statistic(cluster_head_t *pclst)
{
    int i;
    u32 total=0;
    for(i=0;i<pclst->thrd_total;i++)
    {
        total+=pclst->thrd_data[i].vec_cnt;
    }
    return total;
}

u32 debug_thrd_data_statistic(cluster_head_t *pclst)
{
    int i;
    u32 total=0;
    for(i=0;i<pclst->thrd_total;i++)
    {
        total+=pclst->thrd_data[i].data_cnt;
    }
    return total;
}
spt_sort_info *debug_statistic2(cluster_head_t *pclst)
{
    spt_vec **stack;
    spt_sort_info *psort;
    int cur_data, cur_vecid, index;
    spt_vec *pcur, cur_vec;
//    spt_vec_f st_vec_f;
    spt_dh *pdh;
    u32 ref_total, buf_vec_total, buf_data_total,data_total;
    u32 lower_ref;
    cluster_head_t *plower_clst;
    char *pcur_data = NULL;

    stack = (spt_vec **)kmalloc(4096*8*8,GFP_ATOMIC);
    if(stack == NULL)
    {
        return 0;
    }
    index = 0;

    cur_data = SPT_INVALID;

    cur_vecid = pclst->vec_head;
    pcur = (spt_vec *)vec_id_2_ptr(pclst, pclst->vec_head);

    cur_vec.val = pcur->val;
    if(cur_vec.down == SPT_NULL && cur_vec.rd == SPT_NULL)
    {
        printk("cluster is null\r\n");
        return NULL;
    }
    #if 0
    rank = pclst->used_dblk_cnt;
    cur_data = cur_vec.data;
    if(cur_data != SPT_INVALID)
    {
        pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
        pcur_data = get_real_data(pclst, pdh->pdata);
    }
    #endif
    stack[index] = pcur;
    index++;

    while (1)
    {
        if(cur_vec.type != SPT_VEC_DATA)
        {
            cur_vecid = cur_vec.rd;
            pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
            cur_vec.val = pcur->val;
            //debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
            //debug_vec_print(&st_vec_f, cur_vecid);
            if(cur_vec.type == SPT_VEC_SIGNPOST)
            {
                spt_debug("signpost vec found!!\r\n");
                while(1);
            }
            //debug_travl_stack_push(pstack,&st_vec_f, signpost);
            stack[index] = pcur;
            index++;
            #if 0
            pos = st_vec_f.pos;
            if(pos != 0)
                spt_bit_cpy(data, pcur_data, bit, pos);
            else
                spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            bit += st_vec_r.pos;
            #endif
        }
        else
        {            
            cur_data = cur_vec.rd;
            if(cur_data != SPT_NULL)
            {
                pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
                pcur_data = pdh->pdata;
                ref_total += pdh->ref;
                if(!pclst->is_bottom)
                {
                    plower_clst = ((spt_dh_ext *)pcur_data)->plower_clst;
                    buf_data_total += debug_thrd_data_statistic(plower_clst);
                    buf_vec_total += debug_thrd_vec_statistic(plower_clst);
                    lower_ref += debug_statistic2(plower_clst);
					data_total += plower_clst->data_total;
				}
				else
				{
					slice_data_cmp(pcur_data,__LINE__);
				}

                //debug_pdh_data_print(pclst, pdh);            
            }

            if(index == 0)
            {
                break;
            }
            while(1)
            {
                index--;
                pcur = stack[index];
                cur_vec.val = pcur->val;
                if(cur_vec.down != SPT_NULL)
                {
                    cur_vecid =cur_vec.down;
                    pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                    cur_vec.val = pcur->val;
                    
                    if(cur_vec.type == SPT_VEC_SIGNPOST)
                    {
                        spt_debug("signpost vec found!!\r\n");
                        while(1);
                    }
                    stack[index] = pcur;
                    index++;
                    break;
                }              
                if(index == 0)
                {
                    //todo
                    goto sort_exit;
                }                
            }
        }
    }
sort_exit:    
    kfree(stack);
    if(!pclst->is_bottom)
    {
        spt_debug("\r\n lower_total_ref:%d\r\n", lower_ref);
        spt_debug("\r\n data_total:%d\r\n", data_total);
        spt_debug("\r\n buf_data_total:%d\r\n", buf_data_total);
        spt_debug("\r\n buf_vec_total:%d\r\n", buf_vec_total);
    }    
    return ref_total;
}


int debug_statistic(cluster_head_t *pclst)
{
    spt_stack stack = {0};
    spt_stack *pstack = &stack;
    //u8 data[DATA_SIZE] = {0};
    int cur_data, cur_vecid;
    spt_vec *pcur;
    spt_vec_f st_vec_f;
    spt_dh *pdh;
    char *pcur_data = NULL;
    u64 signpost;
    travl_info *pnode;
    u32 ref_total, buf_vec_total, buf_data_total,data_total;
    u32 lower_ref;
    char *data;
    cluster_head_t *plower_clst;

    buf_vec_total = 0;
    buf_data_total = 0;
	data_total = 0;
    ref_total = 0;
    lower_ref = 0;
    signpost = 0;
    spt_stack_init(pstack, 1000);
    cur_data = SPT_INVALID;

    cur_vecid = pclst->vec_head;
    pcur = (spt_vec *)vec_id_2_ptr(pclst, pclst->vec_head);

    debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
    if(pcur->down == SPT_NULL && pcur->rd == SPT_NULL)
    {
        debug_travl_stack_destroy(pstack);
        return ref_total;
    }

    cur_data = st_vec_f.data;
    if(cur_data != SPT_NULL)
    {
        pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
        pcur_data = pdh->pdata;
        ref_total += pdh->ref;
    }
#if 0
    if(st_vec_r.pos == 0)
    {
        printf("only one vec in this cluster\r\n");
        debug_vec_print(&st_vec_r, cur_vec);
        debug_data_print(pcur_data);
        return;        
    }
#endif
    debug_travl_stack_push(pstack,&st_vec_f, signpost);

    while (1)
    {
        if(pcur->type != SPT_VEC_DATA)
        {
            cur_vecid = st_vec_f.right;
            pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
            debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
            if(pcur->type == SPT_VEC_SIGNPOST)
            {
                signpost = st_vec_f.pos;
                
                cur_vecid = st_vec_f.right;
                pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
                if(pcur->type == SPT_VEC_SIGNPOST)
                {
                    spt_debug("double signpost vec found!!\r\n");
                    while(1);
                }
            }
            debug_travl_stack_push(pstack,&st_vec_f, signpost);
            #if 0
            pos = st_vec_f.pos;
            if(pos != 0)
                spt_bit_cpy(data, pcur_data, bit, pos);
            else
                spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            bit += st_vec_r.pos;
            #endif
        }
        else
        {            
            //debug_vec_print(&st_vec_r, cur_vec);
            //单个数据遍历完
            //spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            //此时data应该等于插入的某个数
            //debug_vec_print(&st_vec_f, cur_vecid);
            spt_thread_start(g_thrd_id);
            spt_thread_exit(g_thrd_id);
            if(pcur_data != NULL)//head->right为空时，pcur_data等于空
            {
				//slice_data_cmp(pcur_data);    
			
#if 0
                if(memcmp(data, pcur_data, DATA_SIZE)!=0)
                {
                    printf("\r\nErr    %d\t%s", __LINE__, __FUNCTION__);
                }
                #endif
                //pdh = (spt_dh *)(pcur_data-sizeof(spt_dh));
//                pdh->rank = rank;
//                rank--;
                data = get_real_data(pclst, pcur_data);
                if(!pclst->is_bottom)
                {
                    plower_clst = ((spt_dh_ext *)pcur_data)->plower_clst;
                    buf_data_total += debug_thrd_data_statistic(plower_clst);
                    buf_vec_total += debug_thrd_vec_statistic(plower_clst);
                    lower_ref += debug_statistic(plower_clst);
					data_total += plower_clst->data_total;
				}
				else
				{
					slice_data_cmp(pcur_data,__LINE__);
				}
            }
            
            if(spt_stack_empty(pstack))
            {
                break;
            }
            while(1)
            {
                pnode = debug_travl_stack_pop(pstack);
                if(pnode == (travl_info *)-1)
                {
                    debug_travl_stack_destroy(pstack);
                    if(!pclst->is_bottom)
                    {
                        spt_debug("\r\n lower_total_ref:%d\r\n", lower_ref);
                        spt_debug("\r\n data_total:%d\r\n", data_total);
                        spt_debug("\r\n buf_data_total:%d\r\n", buf_data_total);
                        spt_debug("\r\n buf_vec_total:%d\r\n", buf_vec_total);
                    }
                    return ref_total;
                }
                signpost = pnode->signpost;
                if(pnode->vec_f.down != SPT_NULL)
                {
                    cur_vecid = pnode->vec_f.down;
                    pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                    debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, SPT_INVALID, SPT_DOWN);
                    //debug_vec_print(&st_vec_f, cur_vecid);
                    
                    if(pcur->type == SPT_VEC_SIGNPOST)
                    {
                        signpost = st_vec_f.pos;
                        cur_vecid = pnode->vec_f.down;
                        pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                        debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, SPT_INVALID, SPT_DOWN);
                        if(pcur->type == SPT_VEC_SIGNPOST)
                        {
                            spt_debug("double signpost vec found!!\r\n");
                            while(1);
                        }
                    }
                    cur_data = st_vec_f.data;
                    pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
                    pcur_data = pdh->pdata;
                    ref_total += pdh->ref;
                    debug_travl_stack_push(pstack,&st_vec_f, signpost);
                    kfree(pnode);
                    break;
                }
                kfree(pnode);
            }
        }
    }
    debug_travl_stack_destroy(pstack);
    if(!pclst->is_bottom)
    {
        spt_debug("\r\n lower_total_ref:%d\r\n", lower_ref);
		spt_debug("\r\n data_total:%d\r\n", data_total);
        spt_debug("\r\n buf_data_total:%d\r\n", buf_data_total);
        spt_debug("\r\n buf_vec_total:%d\r\n", buf_vec_total);
    }    
    return ref_total;
}

void debug_buf_free(cluster_head_t *pclst)
{
    spt_stack stack = {0};
    spt_stack *pstack = &stack;
    //u8 data[DATA_SIZE] = {0};
    int cur_data, cur_vecid, i;
    spt_vec *pcur;
    spt_vec_f st_vec_f;
    spt_dh *pdh;
    char *pcur_data = NULL;
    u64 signpost;
    travl_info *pnode;
    u32 ref_total, buf_vec_total, buf_data_total,data_total;
    u32 lower_ref;
    char *data;
    cluster_head_t *plower_clst;

    signpost = 0;
    spt_stack_init(pstack, 1000);
    cur_data = SPT_INVALID;

    cur_vecid = pclst->vec_head;
    pcur = (spt_vec *)vec_id_2_ptr(pclst, pclst->vec_head);

    debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
    if(pcur->down == SPT_NULL && pcur->rd == SPT_NULL)
    {
        debug_travl_stack_destroy(pstack);
        return ;
    }

    cur_data = st_vec_f.data;
    if(cur_data != SPT_NULL)
    {
        pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
        pcur_data = pdh->pdata;
    }
#if 0
    if(st_vec_r.pos == 0)
    {
        printf("only one vec in this cluster\r\n");
        debug_vec_print(&st_vec_r, cur_vec);
        debug_data_print(pcur_data);
        return;        
    }
#endif
    debug_travl_stack_push(pstack,&st_vec_f, signpost);

    while (1)
    {
        if(pcur->type != SPT_VEC_DATA)
        {
            cur_vecid = st_vec_f.right;
            pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
            debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
            if(pcur->type == SPT_VEC_SIGNPOST)
            {
                signpost = st_vec_f.pos;
                
                cur_vecid = st_vec_f.right;
                pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, cur_data, SPT_RIGHT);
                if(pcur->type == SPT_VEC_SIGNPOST)
                {
                    spt_debug("double signpost vec found!!\r\n");
                    while(1);
                }
            }
            debug_travl_stack_push(pstack,&st_vec_f, signpost);
            #if 0
            pos = st_vec_f.pos;
            if(pos != 0)
                spt_bit_cpy(data, pcur_data, bit, pos);
            else
                spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            bit += st_vec_r.pos;
            #endif
        }
        else
        {            
            //debug_vec_print(&st_vec_r, cur_vec);
            //单个数据遍历完
            //spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            //此时data应该等于插入的某个数
            //debug_vec_print(&st_vec_f, cur_vecid);
            if(pcur_data != NULL)//head->right为空时，pcur_data等于空
            {
                #if 0
                if(memcmp(data, pcur_data, DATA_SIZE)!=0)
                {
                    printf("\r\nErr    %d\t%s", __LINE__, __FUNCTION__);
                }
                #endif
                //pdh = (spt_dh *)(pcur_data-sizeof(spt_dh));
//                pdh->rank = rank;
//                rank--;
                data = get_real_data(pclst, pcur_data);
                if(!pclst->is_bottom)
                {
                    plower_clst = ((spt_dh_ext *)pcur_data)->plower_clst;
                    for(i=0; i<plower_clst->thrd_total; i++)
                    {
                        vec_buf_free(plower_clst, i);
                        db_buf_free(plower_clst, i);
                    }
                    plower_clst->status = SPT_OK;
                }
            }
            
            if(spt_stack_empty(pstack))
            {
                break;
            }
            while(1)
            {
                pnode = debug_travl_stack_pop(pstack);
                if(pnode == (travl_info *)-1)
                {
                    debug_travl_stack_destroy(pstack);
                    return ;
                }
                signpost = pnode->signpost;
                if(pnode->vec_f.down != SPT_NULL)
                {
                    cur_vecid = pnode->vec_f.down;
                    pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                    debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, SPT_INVALID, SPT_DOWN);
                    //debug_vec_print(&st_vec_f, cur_vecid);
                    
                    if(pcur->type == SPT_VEC_SIGNPOST)
                    {
                        signpost = st_vec_f.pos;
                        cur_vecid = pnode->vec_f.down;
                        pcur = (spt_vec *)vec_id_2_ptr(pclst, cur_vecid);
                        debug_get_final_vec(pclst, pcur, &st_vec_f, signpost, SPT_INVALID, SPT_DOWN);
                        if(pcur->type == SPT_VEC_SIGNPOST)
                        {
                            spt_debug("double signpost vec found!!\r\n");
                            while(1);
                        }
                    }
                    cur_data = st_vec_f.data;
                    pdh = (spt_dh *)db_id_2_ptr(pclst, cur_data);
                    pcur_data = pdh->pdata;
                    ref_total += pdh->ref;
                    debug_travl_stack_push(pstack,&st_vec_f, signpost);
                    kfree(pnode);
                    break;
                }
                kfree(pnode);
            }
        }
    }
    debug_travl_stack_destroy(pstack);
    return ;
}

int debug_upper_delete(cluster_head_t *pclst, char *pdata)
{
    return do_delete_data(pclst, pdata, 
                           pclst->get_key_in_tree,
                           pclst->get_key_in_tree_end);
}

int debug_upper_insert(cluster_head_t *pclst, char *pdata)
{
    spt_dh_ext *pdh_ext;

    pdh_ext = (spt_dh_ext *)kmalloc(sizeof(spt_dh_ext)+DATA_SIZE,GFP_ATOMIC);

    pdh_ext->data = (char *)(pdh_ext + 1);
//    pdh_ext->plower_clst = (char *)*pdata;
    memcpy(pdh_ext->data, pdata, DATA_SIZE);

    return do_insert_data(pclst,(char *)pdh_ext, pclst->get_key_in_tree, 
                            pclst->get_key_in_tree_end);
}

char *debug_insert(cluster_head_t *pclst, char *pdata)
{
    char *p, *pret;
    p = (char *)kmalloc(DATA_SIZE,GFP_ATOMIC);

    memcpy(p, pdata, DATA_SIZE);

    pret = insert_data(pclst, p);
    if(pret == 0 || pret != p)
    {
        kfree(p);
    }
    return pret;
}

int debug_gd_init(void)
{
    int i;
    gd = (u64 *)kmalloc(sizeof(u64)*TEST_DATA_MAX,GFP_ATOMIC);
    if(gd == NULL)
    {
        spt_debug("gd malloc return NULL\r\n");
        return SPT_ERR;
    }
    gd[0] = gd[1] = 1;
    for(i=2;i<TEST_DATA_MAX;i++)
        gd[i] = gd[i-1] + gd[i-2];

    return SPT_OK;
}

void debug_insert_all(void)
{
    int i;
    for(i=0;i<DBG_DATA_NUM;i++)
    {
        debug_insert(ptopclst, (char*)&g_data[i]);
    }
    debug_cluster_travl(ptopclst);
}
spt_sort_info *pdbg_sort;

void debug_sort(cluster_head_t *pclst)
{
    spt_dh_ext *pdh_ext;
    int i;
    pdbg_sort = spt_cluster_sort(pclst);

    for(i=0;i<pdbg_sort->size;i++)
    {
        pdh_ext = (spt_dh_ext *)(pdbg_sort->array[i]);
        debug_data_print(pdh_ext->data);
    }
    spt_order_array_free(pdbg_sort);
}
#if 0
void debug_vec_print(spt_vec_r *pvec_r, int vec_id)
{
    printk("{down:%d, right:%d, pos:%lld, data:%d}[%d]\r\n",pvec_r->down, pvec_r->right, pvec_r->pos, pvec_r->data, vec_id);
    return;
}

void debug_id_vec_print(cluster_head_t *pclst, int id)
{
    return;
}

void debug_data_print(u8 *pdata)
{
    int i;
    u8 *p = pdata;
    for(i = 0; i < DATA_SIZE; i++,p++)
    {
        if(i%8 == 0)
        {
            printk("\r\n");
        }
        printk("%02x ", *p);
    }
    printk("\r\n");
    return;
}
void debug_id_data_print(cluster_head_t *pclst, int id)
{
    u8 *pdata;

    pdata = (u8 *)db_id_2_ptr(pclst, id);
    debug_data_print(pdata);
    return;
}

travl_info *debug_travl_stack_pop(spt_stack *p_stack)
{
    return (travl_info *)spt_stack_pop(p_stack);
}

void debug_travl_stack_destroy(spt_stack *p_stack)
{
    travl_info *node;
    while(!spt_stack_empty(p_stack))
    {
        node = debug_travl_stack_pop(p_stack);
        free(node);
    }
    spt_stack_destroy(p_stack);
    return;
}

void debug_travl_stack_push(spt_stack *p_stack, spt_vec *pvec_r, int 
direct)
{
    travl_info *node;
    node = (travl_info *)malloc(sizeof(travl_info));
    if(node == NULL)
    {
        printk("\r\nOUT OF MEM        %d\t%s", __LINE__, __FUNCTION__);
        debug_travl_stack_destroy(p_stack);
        return;
    }
    node->direction = direct;
    node->vec_r = *pvec_r;
    spt_stack_push(p_stack,(int)node);
    return;
}


void debug_cluster_travl(cluster_head_t *pclst)
{
    spt_stack stack = {0};
    spt_stack *pstack = &stack;
    u8 data[DATA_SIZE] = {0};
    int cur_vec, cur_data;
    spt_vec *pcur_vec;
    u8 *pcur_data = NULL;
    u64 bit=0, pos;
    spt_vec st_vec_r;
    travl_info *pnode;
    u16 rank, *prsv;
 //   int count=0;

    spt_stack_init(pstack, 100);

    cur_vec = pclst->vec_head;
    pcur_vec = (spt_vec *)vec_id_2_ptr(pclst, cur_vec);
    get_final_vec(pclst, pcur_vec, &st_vec_r, -1);    
    
    if(st_vec_r.down == SPT_NULL && st_vec_r.right == SPT_NULL)
    {
        printk("cluster is null\r\n");
        return;
    }
    rank = pclst->used_dblk_cnt;
    cur_data = st_vec_r.data;//没有首位为1的data时，cur_data等于-1，bug
    /*此时right肯定为空*/
    if(cur_data != -1)
    {
        pcur_data = (u8 *)db_id_2_ptr(pclst, cur_data);    
    }
#if 0
    if(st_vec_r.pos == 0)
    {
        printk("only one vec in this cluster\r\n");
        debug_vec_print(&st_vec_r, cur_vec);
        debug_data_print(pcur_data);
        return;        
    }
#endif
    debug_travl_stack_push(pstack,&st_vec_r, SPT_RIGHT);

    while (1)
    {
        if(st_vec_r.right != SPT_NULL)
        {
            cur_vec = st_vec_r.right;
            pcur_vec = (spt_vec *)vec_id_2_ptr(pclst, cur_vec);
            get_final_vec(pclst, pcur_vec, &st_vec_r, cur_data);
            debug_vec_print(&st_vec_r, cur_vec);
            debug_travl_stack_push(pstack,&st_vec_r, SPT_RIGHT);
            pos = st_vec_r.pos;
            if(pos != 0)
                spt_bit_cpy(data, pcur_data, bit, pos);
            else
                spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            bit += st_vec_r.pos;
        }
        else
        {            
            //debug_vec_print(&st_vec_r, cur_vec);
            //单个数据遍历完
            //spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
            //此时data应该等于插入的某个数
            if(pcur_data != NULL)//head->right为空时，pcur_data等于空
            {
                if(memcmp(data, pcur_data, DATA_SIZE)!=0)
                {
                    printk("\r\nErr    %d\t%s", __LINE__, __FUNCTION__);
                }
                prsv = (u16 *)(pcur_data + DATA_SIZE);
                *prsv = rank;
                rank--;
                debug_data_print(data);

                if(st_vec_r.down != SPT_NULL)
                {
                    printk("\r\nErr    %d\t%s", __LINE__, __FUNCTION__);
                }
            }
            
            if(spt_stack_empty(pstack))
            {
                break;
            }
            while(1)
            {
                pnode = debug_travl_stack_pop(pstack);
                if(pnode == (travl_info *)-1)
                {
                    printk("\r\n");
                    return;
                }
                if(pnode->direction == SPT_RIGHT && pnode->vec_r.down != SPT_NULL)
                {
                    pnode->direction = SPT_DOWN;
                    //不用申请内存，直接入栈
                    spt_stack_push(pstack,(int)pnode);
                    //spt_bit_clear(data, bit - pnode->vec_r.pos, pnode->vec_r.pos);
                    cur_vec = pnode->vec_r.down;
                    pcur_vec = (spt_vec *)vec_id_2_ptr(pclst, cur_vec);
                    get_final_vec(pclst, pcur_vec, &st_vec_r, -1);
                    cur_data = st_vec_r.data;
                    pcur_data = (u8 *)db_id_2_ptr(pclst, cur_data);
                    printk("\r\n@@data[%p],bit:%lld\r\n", pcur_data, bit);
                    debug_vec_print(&st_vec_r, cur_vec);
                    debug_travl_stack_push(pstack,&st_vec_r, SPT_RIGHT);                    
                    pos = st_vec_r.pos;
                    if(pos != 0)
                        spt_bit_cpy(data, pcur_data, bit, pos);
                    else
                        spt_bit_cpy(data, pcur_data, bit, DATA_BIT_MAX - bit);
                    bit += st_vec_r.pos;
                    
                    break;
                }
                else
                {
                    bit -= pnode->vec_r.pos;
                    kfree(pnode);
                }
            }
        }
    }
    
}

void debug_outer_q_in(travl_q *q, travl_outer_info *pnode)
{
    if(q->tail == NULL)
    {
        q->head = q->tail = pnode;
        return;
    }
    q->tail->next = pnode;
    q->tail = pnode;
    return;
}

travl_outer_info *debug_outer_q_de(travl_q *q)
{
    travl_outer_info *p;
    if(q->head == NULL)
    {
        return NULL;
    }
    p = q->head;
    if(q->head == q->tail)
    {
        q->head = q->tail = NULL;
    }
    else
    {
        q->head = p->next;
    }
    return p;
}
void debug_outer_q_destroy(travl_q *q)
{
    return;
}

void debug_cluster_outer_prior_travl(cluster_head_t * pclst)
{
    int cur_vec, cur_data;
    spt_vec *pcur_vec;
    u8 *pcur_data;
    u64 bit=0;
    spt_vec st_vec_r;
    travl_outer_info *pnode;
    u16 rank;
    travl_q tq = {0};
    FILE *fp;
    char buf[1024]={0};
    int j = 0;

    cur_vec = pclst->vec_head;
    pcur_vec = (spt_vec *)vec_id_2_ptr(pclst, cur_vec);
    get_final_vec(pclst, pcur_vec, &st_vec_r, -1);    

    if(st_vec_r.down == SPT_NULL && st_vec_r.right == SPT_NULL)
    {
        printk("cluster is null\r\n");
        return;
    }
    
    fp = fopen("file.txt","w+");
    cur_data = st_vec_r.data;
    /*没有首位是1的data*/
    if(cur_data == -1)
    {
        cur_vec = st_vec_r.down;
    }

    while(cur_vec != SPT_NULL)
    {
        pcur_vec = (spt_vec *)vec_id_2_ptr(pclst, cur_vec);
        get_final_vec(pclst, pcur_vec, &st_vec_r, -1);
        cur_data = st_vec_r.data;
        pcur_data = (u8 *)db_id_2_ptr(pclst, cur_data);
        rank = *(u16 *)(pcur_data + DATA_SIZE);
        
        if(st_vec_r.right != SPT_NULL)
        {
            bit += st_vec_r.pos;
            pnode = (travl_outer_info *)kmalloc(sizeof(travl_outer_info),GFP_ATOMIC);
            if(pnode == NULL)
            {
                printk("\r\nOUT OF MEM        %d\t%s", __LINE__, __FUNCTION__);
                debug_outer_q_destroy(&tq);
                fclose(fp);
                return;
            }
            pnode->next = NULL;
            pnode->pre = cur_vec;
            pnode->cur = st_vec_r.right;
            pnode->data = cur_data;
            pnode->xy_pre.x = rank;
            pnode->xy_pre.y = bit;
            debug_outer_q_in(&tq, pnode);
        }
        else
        {
            bit = DATA_BIT_MAX;
        }       
        j += sprintf(buf+j, "%lld\t%d\r\n", bit, rank);
        
        cur_vec = st_vec_r.down;
    }
    j += sprintf(buf+j, "\r\n");
    fwrite(buf, j, 1, fp);

    while(NULL != (pnode = debug_outer_q_de(&tq)))
    {
        j = 0;
        rank = pnode->xy_pre.x;
        bit = pnode->xy_pre.y;
        j += sprintf(buf+j, "%lld\t%d\r\n", bit, rank);
        cur_vec = pnode->cur;
        cur_data = pnode->data;
        kfree(pnode);
        while(cur_vec != SPT_NULL)
        {
            pcur_vec = (spt_vec *)vec_id_2_ptr(pclst, cur_vec);
            get_final_vec(pclst, pcur_vec, &st_vec_r, -1);
            cur_data = st_vec_r.data;
            pcur_data = (u8 *)db_id_2_ptr(pclst, cur_data);
            rank = *(u16 *)(pcur_data + DATA_SIZE);
            
            if(st_vec_r.right != SPT_NULL)
            {
                bit += st_vec_r.pos;
                pnode = (travl_outer_info *)kmalloc(sizeof(travl_outer_info),GFP_ATOMIC);
                if(pnode == NULL)
                {
                    printk("\r\nOUT OF MEM        %d\t%s", __LINE__, __FUNCTION__);
                    debug_outer_q_destroy(&tq);
                    fclose(fp);
                    return;
                }
                pnode->next = NULL;
                pnode->pre = cur_vec;
                pnode->cur = st_vec_r.right;
                pnode->data = cur_data;
                pnode->xy_pre.x = rank;
                pnode->xy_pre.y = bit;
                debug_outer_q_in(&tq, pnode);
            }
            else
            {
                bit = DATA_BIT_MAX;
            }       
            j += sprintf(buf+j, "%lld\t%d\r\n", bit, rank);
            
            cur_vec = st_vec_r.down;
        }
        
        j += sprintf(buf+j, "\r\n");
        fwrite(buf, j, 1, fp);
        
    }
    fclose(fp);
    return;
}

char *test_read_test_case(char *file_name)
{
    FILE *fp;
    char *data_buf;
    u64 flen;

    fp=fopen(file_name,"r");
    if(NULL==fp)
    {
        return -1;
    }
    fseek(fp,0L,SEEK_END);
    flen=ftell(fp); 

    data_buf = (char *)kmalloc(flen,GFP_ATOMIC);
    if(data_buf == NULL)
    {
        fclose(fp);
        return 0;
    }
    fread(data_buf, flen, 1, fp);
    fclose(fp);
    return data_buf;
}
#endif

char *acmp;
char *bbcmp;
vec_cmpret_t *rcmp;

int test_init(void)
{
    int i=0;

    *(acmp+i)=0x11;i++;
    *(acmp+i)=0x12;i++;
    *(acmp+i)=0x13;i++;
    *(acmp+i)=0x14;i++;
    *(acmp+i)=0x15;i++;
    *(acmp+i)=0x16;i++;
    *(acmp+i)=0x17;i++;
    *(acmp+i)=0x18;i++;

    *(acmp+i)=0xf0;i++;
    *(acmp+i)=0xf1;i++;
    *(acmp+i)=0xf2;i++;
    *(acmp+i)=0xf3;i++;
    *(acmp+i)=0xf4;i++;
    *(acmp+i)=0xf5;i++;
    *(acmp+i)=0xf6;i++;
    *(acmp+i)=0xf7;i++;

    memcpy(bbcmp, acmp, 16);

    *(bbcmp+8)=0x0;
    *(bbcmp+9)=0x0;
    *(bbcmp+10)=0x0;
    *(bbcmp+11)=0x0;
    *(bbcmp+12)=0x6;

    return 0;
}

#if 0
int main()
{
    //    int flag = 1;
    //    spt_vec_t_r tmp_vec_r={5,10,4,65535};
    //    spt_vec_t *pvec_a;0x123406789abcedf; 0x12345678abcedf
    //    int vec_a;
        unsigned long long indata = 0x123406789abcedf;
    //    unsigned long long deldata;

#if 1
    acmp = (char *)malloc(4096);
    bbcmp = (char *)malloc(4096);
    rcmp = (vec_cmpret_t *)malloc(sizeof(vec_cmpret_t));
    if(acmp == NULL || bbcmp == NULL || rcmp == NULL)
    {
        printk("\r\n@@@@@@@@@@");
        return -1;
    }

    test_init();
#endif


    pgclst = cluster_init();
    if(pgclst == NULL)
    
        assert(0);
        printk("\r\n%d\t%s", __LINE__, __FUNCTION__);
        return 1;
    }

    while(1)
    {
      //  sleep(2);
        insert_data(&pgclst,(char *)&indata);
        debug_cluster_travl(pgclst);        
    }

    return 0;
}

//#include <stdio.h>

u64 gdata;
query_info_t g_qinfo = {0};
#if 0
void *spt_thread(void *arg)
{
    cpu_set_t mask;
    int i,j, ret;
//    u64 order_id;
//    spt_vec *pvec;
    //spt_vec *pvec, *pid_2_ptr;
    //spt_dh *pdb, *pdbid_2_ptr;
    
//    u64 start, end;
    i = (long)arg;

    g_thrd_id = i;
    CPU_ZERO(&mask); 
    CPU_SET(i,&mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
    {
        printk("warning: could not set CPU affinity, continuing...\n");
    }
#if   0  
    gdata = 0xee;
    pvec = (spt_vec *)vec_id_2_ptr(pgclst, pgclst->vec_head);
    g_qinfo.op = SPT_OP_INSERT;
    g_qinfo.signpost = 0;
    g_qinfo.start_vec = pvec;
    g_qinfo.endbit = DATA_BIT_MAX+1;
    g_qinfo.data = (char *)&gdata;
#endif
    printk("writefn%d,start\n",i);
//    spt_thread_start();
#if 0
    buf = (int *)malloc(4*1000000);
    if(buf == NULL)
        spt_debug("OOM\r\n");

    for(order_id=0;order_id<100;order_id ++)
    {
        #if 0
        for(i=0;i<10000;i++)
        {
            vec = vec_alloc_combo(&pgclst, &pvec);
            if(pvec != NULL)
            {
                pid_2_ptr = (spt_vec *)vec_id_2_ptr(pgclst, vec);
                if(pid_2_ptr != pvec)
                {
                    spt_debug("vec:%d pvec:%p pid_2_ptr:%p\r\n", vec,pvec,pid_2_ptr);
                }
                buf[i] = vec;
            }

        }
        for(i=0;i<10000;i++)
        {
            vec = buf[i];
            vec_free_to_buf(pgclst, vec, g_thrd_id);
        }
        #else
        for(i=0;i<100000;i++)
        {
            db = db_alloc(&pgclst, (char **)&pdb);
            if(pdb != NULL)
            {
                pdbid_2_ptr = (spt_dh *)db_id_2_ptr(pgclst, db);
                if(pdbid_2_ptr != pdb)
                {
                    spt_debug("db:%d pdb:%p pdbid_2_ptr:%p\r\n", db,pdb,pdbid_2_ptr);
                }
                buf[i] = db;
            }

        }
        for(i=0;i<100000;i++)
        {
            db = buf[i];
            db_free_to_buf(pgclst, db);
        }

        #endif
    }
#endif

    for(i=0;i<100000;i++)
    {
        while(SPT_OK != spt_thread_start(g_thrd_id))
        {
            sleep(1);
        }
        for(j=0;j<DBG_DATA_NUM;j++)
        {
            if(rsv_list_fill_cnt(pgclst, g_thrd_id) > 0)
            {
                spt_debug("@@@@@@@@@@@\r\n");
                while(1);
            }
            ret = insert_data(pgclst, (char *)&g_data[j], spt_bottom_get_key);
            if(ret == SPT_OK)
            {
                atomic64_add(1,(atomic64_t *)&g_dbg_info.insert_ok);
            }
            else
            {
                atomic64_add(1,(atomic64_t *)&g_dbg_info.insert_fail);
                if(ret == SPT_WAIT_AMT)
                {
                    atomic64_add(1,(atomic64_t *)&g_dbg_info.ret_wait);
                    goto thread_exit;
                }
                if(ret == SPT_NOMEM)
                {
                    atomic64_add(1,(atomic64_t *)&g_dbg_info.ret_nomem);
                    goto thread_exit;
                }                
            }
        }
        #if 1
        for(j=0;j<DBG_DATA_NUM;j++)
        {
            if(rsv_list_fill_cnt(pgclst, g_thrd_id) > 0)
            {
                spt_debug("@@@@@@@@@@@\r\n");
                while(1);
            }        
            ret = delete_data(pgclst, (char *)&g_data[j], spt_bottom_get_key);
            if(ret == SPT_OK)
            {
                atomic64_add(1,(atomic64_t *)&g_dbg_info.delete_ok);
            }
            else
            {
                atomic64_add(1,(atomic64_t *)&g_dbg_info.delete_fail);
                if(ret == SPT_WAIT_AMT)
                {
                    atomic64_add(1,(atomic64_t *)&g_dbg_info.ret_wait);
                    goto thread_exit;
                }
                if(ret == SPT_NOMEM)
                {
                    atomic64_add(1,(atomic64_t *)&g_dbg_info.ret_nomem);
                    goto thread_exit;
                }                
            }
        }
        #endif
thread_exit:
        spt_thread_exit(g_thrd_id);
    }
//    spt_thread_exit(g_thrd_id);
    spt_debug("test%d done\r\n", g_thrd_id);
    while(1)
    {

    }
    return 0;    
}

void *spt_thread(void *arg)
{
    cpu_set_t mask;
    int i,j, ret;
    char *pret;
//    u64 order_id;
//    spt_vec *pvec;
    //spt_vec *pvec, *pid_2_ptr;
    //spt_dh *pdb, *pdbid_2_ptr;
    
//    u64 start, end;
    i = (long)arg;

    g_thrd_id = i;
    CPU_ZERO(&mask); 
    CPU_SET(i,&mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
    {
        printk("warning: could not set CPU affinity, continuing...\n");
    }
    printk("writefn%d,start\n",i);
//    spt_thread_start();

    for(i=0;i<100000;i++)
    {
        spt_thread_start(g_thrd_id);
        for(j=0;j<TEST_DATA_MAX;j++)
        {
insert_again:            
            pret = debug_insert(pgclst, (char *)&gd[j]);
            if(pret != NULL)
            {
                atomic64_add(1,(atomic64_t *)&g_dbg_info.insert_ok);
            }
            else
            {
                atomic64_add(1,(atomic64_t *)&g_dbg_info.insert_fail);
                ret = spt_get_errno();
                if(ret == SPT_MASKED)
                {
                    spt_thread_exit(g_thrd_id);
                    spt_thread_start(g_thrd_id);
                    goto insert_again;
                }
                if(ret == SPT_WAIT_AMT)
                {
                    atomic64_add(1,(atomic64_t *)&g_dbg_info.ret_wait);
                    spt_thread_exit(g_thrd_id);
                    spt_thread_start(g_thrd_id);
                    goto insert_again;
                }
                if(ret == SPT_NOMEM)
                {
                    atomic64_add(1,(atomic64_t *)&g_dbg_info.ret_nomem);
                    spt_thread_exit(g_thrd_id);
                    goto thread_exit;
                }                
            }
        }
        #if 0
        for(j=0;j<DBG_DATA_NUM;j++)
        {
            ret = delete_data(pgclst, (char *)&gd[j], spt_bottom_get_key);
            if(ret == SPT_OK)
            {
                atomic64_add(1,(atomic64_t *)&g_dbg_info.delete_ok);
            }
            else
            {
                atomic64_add(1,(atomic64_t *)&g_dbg_info.delete_fail);
                if(ret == SPT_WAIT_AMT)
                {
                    atomic64_add(1,(atomic64_t *)&g_dbg_info.ret_wait);
                    goto thread_exit;
                }
                if(ret == SPT_NOMEM)
                {
                    atomic64_add(1,(atomic64_t *)&g_dbg_info.ret_nomem);
                    spt_thread_exit(g_thrd_id);
                    goto thread_exit;
                }                
            }
        }
        #endif
        spt_thread_exit(g_thrd_id);
    }
thread_exit:
    spt_debug("test%d done\r\n", g_thrd_id);
    while(1)
    {

    }
    return 0;    
}

#endif

#if 0
int main()
{
    long num, thread_num;
    int err;
    pthread_t ntid;
    cpu_set_t mask;

    CPU_ZERO(&mask); 
    thread_num = 3;
    g_thrd_id = thread_num-1;
    CPU_SET(thread_num, &mask);

    pgclst = spt_cluster_init(0,DATA_BIT_MAX, thread_num, 
                              spt_bottom_get_key,
                              default_end_get_key,
                              free_data,
                              spt_bottom_construct_data);
    if(pgclst == NULL)
    {
        spt_debug("cluster_init err\r\n");
        return 1;
    }
//    pgclst->freedata = free_data;

    g_thrd_h = spt_thread_init(thread_num);
    if(g_thrd_h == NULL)
    {
        spt_debug("spt_thread_init err\r\n");
        return 1;
    }
    //test_vec_alloc_n_times(&pgclst);
//    spt_first_level_clst_init(0, 4, thread_num);
//    debug_insert_all();
    debug_gd_init();


    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
    {
        printk("warning: could not set CPU affinity, continuing...\n");
    }

#if 1
    for(num=0; num <thread_num-1; num++)
    {
        err = pthread_create(&ntid, NULL, spt_thread, (void *)num);
        if (err != 0)
            printk("can't create thread: %s\n", strerror(err));
    }
#endif

    while(1)
    {
        //sleep(1);
        spt_divided_scan(pgclst);
    }
    
    return 0;
}
#endif
#endif



