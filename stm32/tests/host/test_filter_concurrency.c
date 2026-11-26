#include "test_common.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

#include "filter_management_task.h"
#include "meta_filter_bytecode.h"

#define STRESS_UPDATES 500u

typedef struct {
    pthread_mutex_t mutex;
    filter_manager_t manager;
    filter_management_task_t task;
    atomic_uint processed;
    atomic_uint producer_done;
    atomic_uint helper_faults;
    uint8_t key[24];
} stress_ctx_t;

static void stress_lock(void *opaque)
{
    stress_ctx_t *ctx=(stress_ctx_t *)opaque;
    (void)pthread_mutex_lock(&ctx->mutex);
}

static void stress_unlock(void *opaque)
{
    stress_ctx_t *ctx=(stress_ctx_t *)opaque;
    (void)pthread_mutex_unlock(&ctx->mutex);
}

static void *stress_producer(void *opaque)
{
    stress_ctx_t *ctx=(stress_ctx_t *)opaque;
    static const vm_insn_t pass_program[]={
        {VM_OP_RET,0,0,0,VM_VERDICT_PASS},
    };
    static const vm_insn_t helper_program[]={
        {VM_OP_MOV_K,1,0,0,0},
        {VM_OP_CALL,0,0,0,VM_HELPER_RATE_SERVICE},
        {VM_OP_RET,0,0,0,VM_VERDICT_PASS},
    };
    uint8_t package[FILTER_UPDATE_MAX_MSG];
    for (uint32_t generation=1;generation<=STRESS_UPDATES;generation++) {
        const int use_helper=(generation&1u)!=0u;
        const vm_insn_t *program=use_helper?helper_program:pass_program;
        uint16_t count=(uint16_t)(use_helper?3u:1u);
        uint32_t mask=use_helper?VM_HELPER_BIT(VM_HELPER_RATE_SERVICE):0u;
        uint32_t len=filter_update_build(package,sizeof(package),0x87654321u,
                                         generation,mask,program,count,
                                         ctx->key,sizeof(ctx->key));
        while (!filter_management_submit(&ctx->task,package,len)) sched_yield();
    }
    atomic_store_explicit(&ctx->producer_done,1u,memory_order_release);
    return NULL;
}

static void *stress_consumer(void *opaque)
{
    stress_ctx_t *ctx=(stress_ctx_t *)opaque;
    while (atomic_load_explicit(&ctx->processed,memory_order_relaxed)<STRESS_UPDATES) {
        if (filter_management_process_one(&ctx->task))
            atomic_fetch_add_explicit(&ctx->processed,1u,memory_order_release);
        else
            sched_yield();
    }
    return NULL;
}

static void *stress_rx(void *opaque)
{
    stress_ctx_t *ctx=(stress_ctx_t *)opaque;
    const uint8_t arp[60]={0xff,0xff,0xff,0xff,0xff,0xff,2,0,0,0,0,1,
                           0x08,0x06};
    do {
        vm_run_stats_t stats;
        int verdict=filter_manager_run(&ctx->manager,arp,sizeof(arp),0,&stats);
        if (verdict!=VM_VERDICT_PASS && verdict!=VM_VERDICT_DROP)
            atomic_fetch_add_explicit(&ctx->helper_faults,1u,memory_order_relaxed);
        if (stats.aborted_helper)
            atomic_fetch_add_explicit(&ctx->helper_faults,1u,memory_order_relaxed);
    } while (!atomic_load_explicit(&ctx->producer_done,memory_order_acquire) ||
             atomic_load_explicit(&ctx->processed,memory_order_acquire)<STRESS_UPDATES);
    return NULL;
}

void test_filter_concurrency(void)
{
    printf("[dynamic concurrency] queue, publication and RX stress\n");
    stress_ctx_t ctx={0};
    for (uint32_t i=0;i<sizeof(ctx.key);i++) ctx.key[i]=(uint8_t)(0x31u+i);
    CHECK(pthread_mutex_init(&ctx.mutex,NULL)==0,"stress critical-section mutex initialises");
    filter_manager_init(&ctx.manager,0x87654321u,ctx.key,sizeof(ctx.key),
                        &meta_filter_program,stress_lock,stress_unlock,&ctx,1000u);
    filter_management_task_init(&ctx.task,&ctx.manager,NULL,NULL,NULL);
    atomic_init(&ctx.processed,0u);
    atomic_init(&ctx.producer_done,0u);
    atomic_init(&ctx.helper_faults,0u);

    pthread_t producer,consumer,rx;
    int created=pthread_create(&producer,NULL,stress_producer,&ctx)==0 &&
                pthread_create(&consumer,NULL,stress_consumer,&ctx)==0 &&
                pthread_create(&rx,NULL,stress_rx,&ctx)==0;
    CHECK(created,"stress producer, management and RX threads start");
    if (created) {
        (void)pthread_join(producer,NULL);
        (void)pthread_join(consumer,NULL);
        (void)pthread_join(rx,NULL);
        const filter_active_t *active=filter_manager_active(&ctx.manager);
        CHECK(atomic_load_explicit(&ctx.processed,memory_order_relaxed)==STRESS_UPDATES &&
              active!=NULL && active->generation==STRESS_UPDATES,
              "concurrent queue preserves all update generations in FIFO order");
        CHECK(atomic_load_explicit(&ctx.helper_faults,memory_order_relaxed)==0u &&
              ctx.manager.stats.rejected_updates==0u,
              "RX observes complete program/capability descriptors during updates");
    }
    (void)pthread_mutex_destroy(&ctx.mutex);
}
