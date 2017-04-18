#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <pone/lf_rwq.h>
#include <linux/vmalloc.h>

u32 mydebug[5][65536];

unsigned long inq_max = 0;
unsigned long soft_inq_max = 0;
void lfrwq_pre_alloc(lfrwq_t* qh);

unsigned long inq_step1 = 0;
unsigned long inq_step2 = 0;

#if 0
static unsigned long long rdtsc(void)
{
    unsigned int lo,hi;
    asm volatile
    (
     "rdtsc":"=a"(lo),"=d"(hi)
    );
    return (unsigned long long)hi<<32|lo;
}
#endif

u64 lfrwq_get_blk_idx(lfrwq_t* qh,u64 idx)
{
    return (idx >> qh->blk_pow);
}


u64 lfrwq_alloc_r_idx(lfrwq_t *qh)
{
    u64 idx;
    idx = atomic64_add_return(1,(atomic64_t *)&qh->r_idx) - 1;
    return idx; 
}
u64 lfrwq_get_w_idx(lfrwq_t *qh)
{
	return qh->w_idx;
}

void lfrwq_set_r_max_idx(lfrwq_t *qh,u64 r_idx)
{
    qh->r_max_idx_period = r_idx;
}

u64 lfrwq_get_r_max_idx(lfrwq_t *qh)
{
    return qh->r_max_idx_period;
}




u64 lfrwq_deq_by_idx(lfrwq_t *qh,u64 idx,void **ppdata)
{
    volatile u64 data;
    u32 r_cnt = 10;

    idx = idx&(qh->len - 1);
    do
    {
        data = qh->q[idx];
        barrier();
        r_cnt--;
        if(r_cnt == 0)
        {
            return idx;
        }
    }while(data == 0);

    qh->q[idx] = 0;
    *ppdata = (void *)data;
    atomic64_add(1, (atomic64_t *)&qh->dbg_r_total);
    return idx;
}

u64 lfrwq_deq(lfrwq_t* qh, void **ppdata)
{
    u64 idx;
    volatile u64 data;
    u32 r_cnt = 10; 
//    q = qh->q;
    idx = atomic64_add_return(1,(atomic64_t *)&qh->r_idx) - 1;
    idx = idx&(qh->len - 1);
    do
    {
        data = qh->q[idx];
        barrier();
        r_cnt--;
        if(r_cnt == 0)
        {
            return idx;
        }    
    }while(data == 0);

//    data = atomic64_xchg((atomic64_t *)&q[idx], 0);
    qh->q[idx] = 0;
    *ppdata = (void *)data;
    atomic64_add(1, (atomic64_t *)&qh->dbg_r_total);
    return idx; 
}


int lfrwq_soft_inq(lfrwq_t *qh,u64 w_idx)
{
    u64 idx,laps;
    volatile u64 rd_cnt;
    u32 blk_idx;
    u32 w_num = 0;
    
    //printk("idx is %lld\r\n",w_idx);
    idx = w_idx;
    laps = idx >> qh->q_pow;
    idx = idx&(qh->len - 1);
    blk_idx = idx >> qh->blk_pow;

    if((idx&(qh->blk_len-1)) == 0)
    {
        return -1 ;
    }
    
    rd_cnt = qh->r_cnt[blk_idx];
    if((rd_cnt >> qh->blk_pow) < laps)
    {
        printk("blk idex is %d,rd_cnt is %lld\r\n",blk_idx,rd_cnt);
        return -1;
    }
    printk("soft in q w_idex is %lld\r\n",w_idx);
    w_num = qh->blk_len - (idx&(qh->blk_len -1));
    printk("soft inq w_num is %d\r\n",w_num);

    while(w_num)
    {         
        idx = atomic64_cmpxchg((atomic64_t *)&qh->w_idx ,w_idx,w_idx+1);
        if(idx != w_idx)
        {
            return -1;
        }   
        
        idx = idx&(qh->len - 1);
            
        if(qh->q[idx] != 0)
        {   
            lfrwq_debug("inq overlap find");
            return -1;
        }
        qh->q[idx] = 0xABABABABBABABABA;
        w_idx++;
        w_num--;    

    }
	return 0;
}

#if 1
int lfrwq_inq(lfrwq_t *qh,void *data)
{
    volatile u64 w_idx,idx,laps;
    u32 blk_idx;
    volatile u64 rd_cnt;
    u32 count = 0;
    unsigned long long begin;
    inq_step1 = 0;
    inq_step2 = 0;
    while(1)
    {
        begin = rdtsc();
     
        w_idx = idx = qh->w_idx;
        laps = idx >> qh->q_pow;
        idx = idx&(qh->len - 1);
        blk_idx = idx >> qh->blk_pow;
        count++; 
        rd_cnt = qh->r_cnt[blk_idx];
        if((rd_cnt >> qh->blk_pow) < laps)
        {
            //printk("blk idex is %d,rd_cnt is %lld\r\n",blk_idx,rd_cnt);
            //printk("r_idx is %lld\r\n",qh->r_idx);
            return -1 ;
        }
        idx = atomic64_cmpxchg((atomic64_t *)&qh->w_idx ,w_idx,w_idx+1);
        inq_step1 += rdtsc() - begin;
        begin = rdtsc();         
            
        if(idx != w_idx)
        {
            continue;
        }   
        idx = idx&(qh->len - 1); 
        if(qh->q[idx] != 0)
        {   
            lfrwq_debug("inq overlap find");
            return -1;
        }
        qh->q[idx] =(long)data;

        if((idx&(qh->blk_len - 1)) == 0)
        {
            lfrwq_pre_alloc(qh);
        }
        if(count >inq_max)
        {
            inq_max = count;
        }
        inq_step2 += rdtsc() - begin;
        return 0;
    }
}
#endif

#if 0
int lfrwq_inq(lfrwq_t* qh, void *data)
{
    u64 idx, laps;
    volatile u64 rd_cnt;
    u32 blk_idx;
    unsigned long long begin;
    unsigned long long end;
    
    begin = rdtsc();
    idx = atomic64_add_return(1,(atomic64_t *)&qh->w_idx) - 1;
    laps = idx >> qh->q_pow;
    idx = idx&(qh->len - 1);
    blk_idx = idx >> qh->blk_pow;
    
    do
    {
        rd_cnt = qh->r_cnt[blk_idx];
    }while((rd_cnt >> qh->blk_pow) < laps);
    inq_step1 = rdtsc() -begin;
    
    begin = rdtsc(); 
    if(qh->q[idx] != 0)
    {
        lfrwq_debug("inq overlap find");
        //assert(0);
    }
    qh->q[idx] = (u64)data;

    if((idx&(qh->blk_len - 1)) == 0)
    {
        lfrwq_pre_alloc(qh);
    }
    inq_step2 = rdtsc() - begin;    
    return 0;
}
#endif

void lfrwq_add_rcnt(lfrwq_t* qh, u32 total, u32 cnt_idx)
{
    atomic64_add(total,(atomic64_t *)&qh->r_cnt[cnt_idx]);
    return;
}


int lfrwq_get_rpermit(lfrwq_t* qh)
{
    int permit, left, suggest;
try_again:    
    suggest = qh->r_pmt_sgest;
    left = atomic_sub_return(suggest,(atomic_t *)&qh->r_permit);
    if(left + suggest <= 0)
    {
        if(atomic_add_return(suggest, (atomic_t *)&qh->r_permit)>0)
            goto try_again;
        return 0;
    }
    if(left < 0)
    {
        atomic_sub(left, (atomic_t *)&qh->r_permit);
        permit = left + suggest;
        return permit;
    }
    return suggest;
}

void lfrwq_pre_alloc(lfrwq_t* qh)
{
    int alloc;
    
    alloc = qh->blk_len;
    atomic_add((int)alloc, (atomic_t *)&qh->r_permit);
    atomic64_add(alloc, (atomic64_t *)&qh->dbg_p_total);        
    return;
}

lfrwq_t* lfrwq_init(u32 q_len, u32 blk_len, u32 readers)
{
    lfrwq_t *qh;
    u32 quo, blk_cnt, pow1, pow2, total_len;
	int page_num;
    quo = q_len;
    pow1 = pow2 = 0;
    while(quo > 1)
    {
        if(quo%2 != 0)
        {
            lfrwq_debug("input err:q_len\n");
            goto init_err;
        }
        quo = quo/2;
        pow1++;
    }

    quo = blk_len;
    while(quo > 1)
    {
        if(quo%2 != 0)
        {
            lfrwq_debug("input err:blk_len\n");
            goto init_err;
        }
        quo = quo/2;
        pow2++;
    }

    blk_cnt = q_len/blk_len;
    total_len = sizeof(u64)*q_len + sizeof(lfrwq_t)+sizeof(u64)*(blk_cnt);
    total_len = (1+(total_len -1)/PAGE_SIZE)*PAGE_SIZE;
    qh = vmalloc(total_len);
    if(!qh)
    {
        return NULL;
    }
    memset((void *)qh, 0, total_len);
#if 0
	page_num = total_len/PAGE_SIZE;
    while(page_num)
    {
        SetPageReserved(virt_to_page((char*)qh + (page_num -1)*PAGE_SIZE));
        page_num--;
    }
#endif 
    qh->r_cnt = (u64 *)((long)qh + sizeof(u64)*q_len + sizeof(lfrwq_t));
    #if 0
    if(qh->r_cnt == NULL)
        goto free_qh;
    #endif
    
	qh->r_idx = 0;
    qh->w_idx = 0;
    qh->len = q_len;
    qh->q_pow = pow1;
    qh->blk_len = blk_len;
    qh->blk_pow = pow2;
    qh->readers = readers;
    qh->blk_cnt = blk_cnt;
    qh->r_permit = 0;
    qh->r_pmt_sgest = blk_len/qh->readers;
    qh->dbg_r_total = 0;
    qh->dbg_p_total = 0;
    qh->dbg_get_pmt_total = 0;
    qh->r_max_idx_period = 0;
    #if 0
    memset((void *)qh->q, 0, q_len*sizeof(u64));
    memset((void *)qh->r_cnt, 0, blk_cnt*sizeof(u64));
    #endif
    return qh;
#if 0    
free_qh:
    free(qh);
#endif
init_err:    
    return NULL;
}


#if 0

lfrwq_t *gqh;

void *writefn(void *arg)
{
    cpu_set_t mask;
    int i, j;
    char *p;
//    int (*inq)(lfrwq_t* , void*);
    
    i = (long)arg;
    if(i<8)
        j=0;
    else
        j=1;
    
    CPU_ZERO(&mask); 
    CPU_SET(j,&mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
    {
        printf("warning: could not set CPU affinity, continuing...\n");
    }
    
    printf("writefn%d,start\n",i);
#if 0
    if(lfrwq_get_token(gqh) == 1)
    {
        inq = lfrwq_inq_m;
    }
    else
    {
        inq = lfrwq_inq;
    }
#endif
    for(i=0;i<1000000000;i++)
    {
        if(0 != lfrwq_inq(gqh, (void *)(long)(i+1)))
        {
            lfrwq_debug("inq fail\n");
            break;
        }
#if 0    
        if(0 != inq(gqh, (void *)(long)(i+1)))
        {
            lfrwq_debug("inq fail\n");
            break;
        }
#endif
    }
    
    while(1)
    {
        sleep(1);
    }
    return 0;    
}

void *readfn(void *arg)
{
    cpu_set_t mask;
    int i, j;
    u32 local_pmt, blk, tmp_blk, cnt;
    u64 *pdata;

    i = (long)arg;

    if(i<24)
        j=2;
    else
        j=3;
   
    CPU_ZERO(&mask); 
    CPU_SET(j,&mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
    {
        printf("warning: could not set CPU affinity, continuing...\n");
    }
    
    printf("readfn%d,start\n",i);

    cnt = 0;
    while(1)
    {
        local_pmt = lfrwq_get_rpermit(gqh);
        atomic64_add(local_pmt,(atomic64_t *)&gqh->dbg_get_pmt_total);
        if(local_pmt > 0)
        {
            blk = lfrwq_deq(gqh, (void **)&pdata);
            local_pmt--;
            cnt++;
        }
        while(local_pmt > 0)
        {
            tmp_blk = lfrwq_deq(gqh, (void **)&pdata);            
            if(blk != tmp_blk)
            {
                lfrwq_add_rcnt(gqh, cnt, blk);
 #if 0               
                mydebug[i-2][j] = blk;
                mydebug[i-2][j+1] = cnt;
                j=(j+2)%20480;
#endif
                cnt = 0;
                blk = tmp_blk;
            }
            cnt++;
            local_pmt--;
        }
        if(cnt != 0)
        {
            lfrwq_add_rcnt(gqh, cnt, blk);
#if 0            
            mydebug[i-2][j] = blk;
            mydebug[i-2][j+1] = cnt;
            j=(j+2)%20480;
#endif
            cnt = 0;
        }
        usleep(1);
    }
    return 0;    
}

#if 0
int main()
{
    long num;
    int err;
    pthread_t ntid;
    cpu_set_t mask;

    gqh = lfrwq_init(65536, 1024, 16);
    if(gqh == NULL)
        lfrwq_debug("create q return null\n");

    CPU_ZERO(&mask); 
    memset(mydebug, 0, sizeof(mydebug));
#if 1
    for(num=0; num <16; num++)
    {
        err = pthread_create(&ntid, NULL, writefn, (void *)num);
        if (err != 0)
            printf("can't create thread: %s\n", strerror(err));
    }
    for(; num <32; num++)
    {
        err = pthread_create(&ntid, NULL, readfn, (void *)num);
        if (err != 0)
            printf("can't create thread: %s\n", strerror(err));
    }    
#endif

    while(1)
    {
        sleep(1);
    }
    
    return 0;
}
#else
static void sig_child(int signo);

int main(int argc, char **argv)  
{
    struct {
        union{
            struct{ 
                int *p1;
                long long1;
            };
            int *p2;
            int *p3;
        };
        int *p4;
        int *p5;
    }haha;
    memset(&haha, 0, sizeof(haha));
    printf("size:%d\n", sizeof(haha));
    while (1)
    {
        sleep(1);
    }
    return 0;
}

int main1(int argc, char **argv)  
{  
    long num;
    pid_t pid;
    signal(SIGCHLD,sig_child);
    
    gqh = lfrwq_init(65536, 1024, 2);
    if(gqh == NULL)
        lfrwq_debug("create q return null\n");

    for(num=0; num < 2; num++)
    {
        pid = fork();
        if(pid < 0)
        {
            printf("fork fail\n");
        }
        else if(pid == 0) 
        {
            sleep(2);
            writefn((void *)num);
        }  
        else
        {  
            ;
        }

    }

    for(; num < 4; num++)
    {
        pid = fork();
        if(pid < 0)
        {
            printf("fork fail\n");
        }
        else if(pid == 0) 
        {
            readfn((void *)num);
        }  
        else
        {  
            ;
        }

    }
    while(1)
    {
        sleep(1);
    }
    return 0;
}  

static void sig_child(int signo)
{
    pid_t pid;
    int stat;
    //处理僵尸进程
    while ((pid = waitpid(-1, &stat, WNOHANG)) >0)
        printf("child %d terminated.\n", pid);
}


#endif
#endif


