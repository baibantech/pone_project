#include "spt_dep.h"
#include "spt_thread.h"

spt_thrd_t *g_thrd_h;

spt_thrd_t *spt_thread_init(int thread_num)
{
    g_thrd_h = spt_malloc(sizeof(spt_thrd_t));
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
    spt_atomic64_set_bit(thread, (atomic64_t *)&g_thrd_h->online_map);
    smp_mb();
    return 0;
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
            spt_assert(0);
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

