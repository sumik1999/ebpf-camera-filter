#include "filter_manager.h"

#include <stddef.h>

#include "packet_parser.h"
#include "vm_loader.h" /* CRC-32 */

_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2, "active policy pointer must be lock-free");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "RX bank references must be lock-free");

static uint16_t get_le16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put_le16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put_le32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static void critical_enter(filter_manager_t *m) { if (m->enter_critical) m->enter_critical(m->critical_opaque); }
static void critical_exit(filter_manager_t *m) { if (m->exit_critical) m->exit_critical(m->critical_opaque); }

static filter_update_result_t reject(filter_manager_t *m, filter_update_result_t result)
{
    m->stats.rejected_updates++;
    if (result == FILTER_UPDATE_ERR_AUTH) m->stats.auth_failures++;
    if (result == FILTER_UPDATE_ERR_GENERATION) m->stats.replay_failures++;
    return result;
}

void filter_manager_init(filter_manager_t *m, uint32_t device_id,
                         const uint8_t *key, uint32_t key_len,
                         const vm_program_t *recovery,
                         filter_critical_fn enter_critical,
                         filter_critical_fn exit_critical,
                         void *critical_opaque, uint32_t tick_hz)
{
    if (m == NULL) return;
    for (uint32_t i=0;i<FILTER_PROGRAM_BANKS;i++) {
        m->bank[i].program.insns=m->insns[i]; m->bank[i].program.n_insns=0;
        m->bank[i].program.ctx=VM_CTX_META; m->bank[i].helper_mask=0;
        m->bank[i].generation=0; m->bank[i].hash=0;
        atomic_init(&m->bank[i].readers,0u);
    }
    m->recovery_state.program=recovery?*recovery:(vm_program_t){NULL,0,VM_CTX_META};
    m->recovery_state.helper_mask=recovery?vm_program_required_helpers(recovery):0u;
    m->recovery_state.generation=0; m->recovery_state.hash=0;
    atomic_init(&m->recovery_state.readers,0u);
    m->previous=NULL; m->highest_generation=0;
    atomic_init(&m->active,NULL);
    m->device_id=device_id;
    m->auth_key_len=(uint8_t)(key_len>FILTER_AUTH_KEY_MAX?FILTER_AUTH_KEY_MAX:key_len);
    for (uint32_t i=0;i<FILTER_AUTH_KEY_MAX;i++) m->auth_key[i]=(i<m->auth_key_len&&key)?key[i]:0u;
    m->enter_critical=enter_critical; m->exit_critical=exit_critical; m->critical_opaque=critical_opaque;
    m->stats=(filter_manager_stats_t){0,0,0,0,0,0};
    rate_limiter_init(&m->limiter,tick_hz,0);
    rate_limiter_set_global(&m->limiter,0xffffffffu,0xffffffffu);
    for (uint32_t i=0;i<RATE_MAX_SERVICES;i++)
        (void)rate_limiter_set_service(&m->limiter,i,0xffffffffu,0xffffffffu);
    if (recovery != NULL && recovery->ctx == VM_CTX_META && vm_verify(recovery)==VM_VERIFY_OK)
        atomic_store_explicit(&m->active,&m->recovery_state,memory_order_release);
}

const filter_active_t *filter_manager_active(const filter_manager_t *m)
{
    return m?atomic_load_explicit(&m->active,memory_order_acquire):NULL;
}

static const filter_active_t *active_acquire(filter_manager_t *m)
{
    const filter_active_t *active;
    do {
        active=atomic_load_explicit(&m->active,memory_order_acquire);
        if (active==NULL) return NULL;
        atomic_fetch_add_explicit((atomic_uint *)&active->readers,1u,memory_order_acquire);
        if (active==atomic_load_explicit(&m->active,memory_order_acquire)) return active;
        atomic_fetch_sub_explicit((atomic_uint *)&active->readers,1u,memory_order_release);
    } while (1);
}

static void active_release(const filter_active_t *active)
{
    atomic_fetch_sub_explicit((atomic_uint *)&active->readers,1u,memory_order_release);
}

static int free_bank(filter_manager_t *m)
{
    const filter_active_t *active=filter_manager_active(m);
    for (uint32_t i=0;i<FILTER_PROGRAM_BANKS;i++) {
        if (&m->bank[i]!=active && &m->bank[i]!=m->previous &&
            atomic_load_explicit(&m->bank[i].readers,memory_order_acquire)==0u) return (int)i;
    }
    return -1;
}

filter_update_result_t filter_manager_install(filter_manager_t *m,
                                               const uint8_t *msg, uint32_t len,
                                               vm_verify_result_t *why)
{
    if (why) *why=VM_VERIFY_OK;
    if (m==NULL || msg==NULL || len<FILTER_UPDATE_HEADER_LEN) return m?reject(m,FILTER_UPDATE_ERR_SHORT):FILTER_UPDATE_ERR_SHORT;
    if (get_le32(msg)!=FILTER_UPDATE_MAGIC) return reject(m,FILTER_UPDATE_ERR_MAGIC);
    if (msg[4]!=FILTER_UPDATE_FORMAT) return reject(m,FILTER_UPDATE_ERR_FORMAT);
    if (msg[5]!=VM_ISA_VERSION_V2) return reject(m,FILTER_UPDATE_ERR_VERSION);
    if (msg[6]!=VM_CTX_META || msg[7]!=VM_META_ABI_VERSION) return reject(m,FILTER_UPDATE_ERR_CONTEXT);
    uint16_t count=get_le16(msg+8);
    if (count==0u || count>VM_MAX_INSNS || len!=FILTER_UPDATE_HEADER_LEN+8u*count)
        return reject(m,FILTER_UPDATE_ERR_LENGTH);
    uint32_t generation=get_le32(msg+12);
    if (generation<=m->highest_generation) return reject(m,FILTER_UPDATE_ERR_GENERATION);
    if (get_le32(msg+16)!=m->device_id) return reject(m,FILTER_UPDATE_ERR_DEVICE);
    uint32_t helper_mask=get_le32(msg+20);
    const uint8_t *body=msg+FILTER_UPDATE_HEADER_LEN;
    if (vm_crc32(body,8u*count)!=get_le32(msg+24)) return reject(m,FILTER_UPDATE_ERR_CRC);
    uint8_t tag[32];
    filter_hmac_sha256_2(m->auth_key,m->auth_key_len,msg,28u,body,8u*count,tag);
    if (!filter_constant_time_equal(tag,msg+28,32u)) return reject(m,FILTER_UPDATE_ERR_AUTH);

    int bank=free_bank(m);
    /* A third bank can briefly be held by an RX execution that started before
     * two rapid publications. This is backpressure, not a malformed update. */
    if (bank<0) return FILTER_UPDATE_ERR_STORAGE;
    for (uint32_t i=0;i<count;i++) {
        const uint8_t *b=body+8u*i;
        m->insns[bank][i].opcode=b[0];
        m->insns[bank][i].dst_reg=(uint8_t)(b[1]&0x0fu);
        m->insns[bank][i].src_reg=(uint8_t)(b[1]>>4);
        m->insns[bank][i].offset=(int16_t)get_le16(b+2);
        m->insns[bank][i].imm=(int32_t)get_le32(b+4);
    }
    filter_active_t *next=&m->bank[bank];
    next->program.insns=m->insns[bank]; next->program.n_insns=count;
    next->program.ctx=VM_CTX_META; next->helper_mask=helper_mask;
    next->generation=generation; next->hash=get_le32(msg+24);
    vm_verify_result_t vr=vm_verify(&next->program);
    uint32_t required_helpers=vm_program_required_helpers(&next->program);
    if (vr==VM_VERIFY_OK &&
        ((helper_mask & ~((1u << VM_HELPER_COUNT) - 1u)) != 0u ||
         (required_helpers & ~helper_mask) != 0u)) {
        vr=VM_VERIFY_ERR_BAD_HELPER;
    }
    if (why) *why=vr;
    if (vr!=VM_VERIFY_OK) return reject(m,FILTER_UPDATE_ERR_VERIFY);

    const filter_active_t *old=filter_manager_active(m);
    critical_enter(m);
    m->previous=old;
    atomic_store_explicit(&m->active,next,memory_order_release);
    m->highest_generation=generation;
    critical_exit(m);
    m->stats.accepted_updates++;
    return FILTER_UPDATE_OK;
}

int filter_manager_rollback(filter_manager_t *m)
{
    if (m==NULL || m->previous==NULL) return 0;
    critical_enter(m);
    const filter_active_t *current=atomic_load_explicit(&m->active,memory_order_relaxed);
    const filter_active_t *previous=m->previous;
    m->previous=current;
    atomic_store_explicit(&m->active,previous,memory_order_release);
    critical_exit(m);
    m->stats.rollbacks++;
    return 1;
}

void filter_manager_recover(filter_manager_t *m)
{
    if (m==NULL || m->recovery_state.program.insns==NULL ||
        m->recovery_state.program.n_insns==0u) return;
    critical_enter(m);
    const filter_active_t *current=atomic_load_explicit(&m->active,memory_order_relaxed);
    m->previous=current;
    atomic_store_explicit(&m->active,&m->recovery_state,memory_order_release);
    critical_exit(m);
    m->stats.rollbacks++;
}

int filter_manager_run(filter_manager_t *m, const uint8_t *frame,
                       uint32_t frame_len, uint32_t now_tick,
                       vm_run_stats_t *run_stats)
{
    if (m==NULL) return VM_VERDICT_DROP;
    packet_meta_t meta;
    packet_parse_result_t status=packet_parse_ethernet(frame,frame_len,&meta);
    uint8_t encoded[VM_META_SIZE];
    vm_meta_encode(&meta,status,encoded);
    const filter_active_t *active=active_acquire(m);
    if (active==NULL) return VM_VERDICT_DROP;
    rate_limiter_set_now(&m->limiter,now_tick);
    vm_exec_env_t env={rate_limiter_vm_helper,&m->limiter,active->helper_mask};
    vm_run_stats_t local;
    int verdict=vm_interpreter_run_ex(&active->program,encoded,VM_META_SIZE,&env,run_stats?run_stats:&local);
    active_release(active);
    const vm_run_stats_t *st=run_stats?run_stats:&local;
    if (st->aborted_oob||st->aborted_budget||st->aborted_helper) m->stats.vm_faults++;
    return verdict;
}

uint32_t filter_update_build(uint8_t *out, uint32_t cap, uint32_t device_id,
                             uint32_t generation, uint32_t helper_mask,
                             const vm_insn_t *insns, uint16_t count,
                             const uint8_t *key, uint32_t key_len)
{
    uint32_t len=FILTER_UPDATE_HEADER_LEN+8u*count;
    if (out==NULL||insns==NULL||count==0u||count>VM_MAX_INSNS||cap<len) return 0;
    for (uint32_t i=0;i<FILTER_UPDATE_HEADER_LEN;i++) out[i]=0;
    put_le32(out,FILTER_UPDATE_MAGIC); out[4]=FILTER_UPDATE_FORMAT;
    out[5]=VM_ISA_VERSION_V2; out[6]=VM_CTX_META; out[7]=VM_META_ABI_VERSION;
    put_le16(out+8,count); put_le32(out+12,generation); put_le32(out+16,device_id);
    put_le32(out+20,helper_mask);
    uint8_t *body=out+FILTER_UPDATE_HEADER_LEN;
    for (uint32_t i=0;i<count;i++) {
        const vm_insn_t *in=&insns[i]; uint8_t *b=body+8u*i;
        b[0]=in->opcode; b[1]=(uint8_t)(in->dst_reg|(in->src_reg<<4));
        put_le16(b+2,(uint16_t)in->offset); put_le32(b+4,(uint32_t)in->imm);
    }
    put_le32(out+24,vm_crc32(body,8u*count));
    filter_hmac_sha256_2(key,key_len,out,28u,body,8u*count,out+28);
    return len;
}

const char *filter_update_strerror(filter_update_result_t r)
{
    switch(r) {
    case FILTER_UPDATE_OK:return "ok"; case FILTER_UPDATE_ERR_SHORT:return "short";
    case FILTER_UPDATE_ERR_MAGIC:return "magic"; case FILTER_UPDATE_ERR_FORMAT:return "format";
    case FILTER_UPDATE_ERR_DEVICE:return "device"; case FILTER_UPDATE_ERR_VERSION:return "version";
    case FILTER_UPDATE_ERR_CONTEXT:return "context"; case FILTER_UPDATE_ERR_LENGTH:return "length";
    case FILTER_UPDATE_ERR_GENERATION:return "generation/replay"; case FILTER_UPDATE_ERR_CRC:return "crc";
    case FILTER_UPDATE_ERR_AUTH:return "authentication"; case FILTER_UPDATE_ERR_VERIFY:return "verify";
    case FILTER_UPDATE_ERR_STORAGE:return "storage"; default:return "unknown";
    }
}
