#include "test_common.h"

#include "filter_crypto.h"
#include "filter_management_task.h"
#include "ingress_filter.h"
#include "meta_filter_bytecode.h"
#include "vm_meta_abi.h"

static filter_policy_t matching_native_policy(void)
{
    filter_policy_t p;
    filter_policy_monitor(&p);
    p.drop_malformed=1; p.drop_unsupported=1; p.drop_ipv6=1; p.drop_fragments=1;
    p.enforce_vlan_allowlist=1; p.allow_untagged=1; p.vlan_count=1;
    p.allowed_vlans[0]=META_POLICY_VLAN;
    p.enforce_protocol_allowlist=1; p.allow_arp=1; p.allow_icmp=1;
    p.allow_tcp=1; p.allow_udp=1; p.enforce_service_allowlist=1; p.service_count=2;
    p.services[0]=(filter_service_t){PACKET_IPPROTO_UDP,0,META_POLICY_UDP_PORT};
    p.services[1]=(filter_service_t){PACKET_IPPROTO_TCP,0,META_POLICY_TCP_PORT};
    return p;
}

static uint32_t make_packet(uint8_t *f, uint16_t etype, uint8_t proto, uint16_t port)
{
    memset(f,0,128); memset(f,0xff,6); f[6]=2; put16(f+12,etype);
    if (etype!=PACKET_ETHERTYPE_IPV4) return 60;
    uint8_t *ip=f+14; uint32_t l4=proto==PACKET_IPPROTO_TCP?20u:proto==PACKET_IPPROTO_ICMP?4u:8u;
    ip[0]=0x45; put16(ip+2,(uint16_t)(20u+l4)); ip[8]=64; ip[9]=proto;
    ip[12]=10; ip[15]=2; ip[16]=10; ip[19]=1;
    uint8_t *h=ip+20;
    if (proto==PACKET_IPPROTO_TCP||proto==PACKET_IPPROTO_UDP) { put16(h,40000); put16(h+2,port); }
    if (proto==PACKET_IPPROTO_UDP) put16(h+4,8);
    if (proto==PACKET_IPPROTO_TCP) { h[12]=0x50; h[13]=2; }
    return 14u+20u+l4;
}

static int s_enter, s_exit;
static void enter(void *p) { (void)p; s_enter++; }
static void leave(void *p) { (void)p; s_exit++; }

static int s_result_calls;
static filter_update_result_t s_last_result;
static void result_cb(void *p, filter_update_result_t result, vm_verify_result_t why)
{
    (void)p; (void)why; s_result_calls++; s_last_result=result;
}

static uint32_t s_rng=0x91a2b3c4u;
static uint32_t rnd32(void)
{
    s_rng^=s_rng<<13; s_rng^=s_rng>>17; s_rng^=s_rng<<5; return s_rng;
}

void test_dynamic_filter(void)
{
    printf("[dynamic] metadata ABI, ISA v2, authenticated A/B updates and rate helpers\n");

    /* Canonical ABI is independent of packet_meta_t padding and host endian. */
    packet_meta_t meta={0}; uint8_t encoded[VM_META_SIZE];
    meta.l3_type=PACKET_L3_IPV4; meta.ip_version=4; meta.protocol=17;
    meta.vlan_depth=1; meta.outer_vlan_id=0x123; meta.src_port=0x4567; meta.dst_port=0x89ab;
    meta.ip_total_len=0x0102; meta.src_ip4=0x0a000002u; meta.dst_ip4=0xc0a80101u;
    meta.frame_len=0x11223344u; meta.l4_present=1;
    vm_meta_encode(&meta,PACKET_PARSE_OK,encoded);
    CHECK(encoded[0]==VM_META_ABI_VERSION && encoded[12]==0x01 && encoded[13]==0x23 &&
          encoded[14]==0x45 && encoded[15]==0x67 && encoded[16]==0x89 && encoded[17]==0xab &&
          encoded[20]==0x0a && encoded[23]==0x02 && encoded[28]==0x11 && encoded[31]==0x44,
          "canonical metadata offsets and big-endian fields are stable");

    CHECK(vm_verify(&meta_filter_program)==VM_VERIFY_OK && meta_filter_program.ctx==VM_CTX_META,
          "ISA-v2 metadata program verifies");
    CHECK(vm_program_required_helpers(&meta_filter_program)==
          VM_HELPER_BIT(VM_HELPER_RATE_SERVICE),
          "metadata program declares its required bounded helper");

    static const uint8_t key[]="dynamic-filter-test-key";
    filter_manager_t manager; s_enter=s_exit=0;
    filter_manager_init(&manager,0x12345678u,key,sizeof(key)-1u,&meta_filter_program,
                        enter,leave,NULL,1000u);

    uint8_t frame[128]; uint32_t n;
    n=make_packet(frame,PACKET_ETHERTYPE_IPV4,PACKET_IPPROTO_UDP,53);
    CHECK(filter_manager_run(&manager,frame,n,0,NULL)==VM_VERDICT_PASS,
          "recovery metadata program passes UDP/DNS");
    n=make_packet(frame,PACKET_ETHERTYPE_IPV4,PACKET_IPPROTO_UDP,1900);
    CHECK(filter_manager_run(&manager,frame,n,0,NULL)==VM_VERDICT_DROP,
          "recovery metadata program drops unlisted UDP service");

    /* Native reference and VM program must agree over structured and random frames. */
    ingress_filter_init(); filter_policy_t policy=matching_native_policy();
    CHECK(ingress_filter_configure(&policy),"matching native policy configured");
    uint32_t mismatches=0;
    const struct { uint16_t et; uint8_t proto; uint16_t port; } cases[]={
        {PACKET_ETHERTYPE_ARP,0,0},{PACKET_ETHERTYPE_IPV6,0,0},
        {PACKET_ETHERTYPE_IPV4,PACKET_IPPROTO_ICMP,0},
        {PACKET_ETHERTYPE_IPV4,PACKET_IPPROTO_UDP,53},
        {PACKET_ETHERTYPE_IPV4,PACKET_IPPROTO_UDP,1900},
        {PACKET_ETHERTYPE_IPV4,PACKET_IPPROTO_TCP,443},
        {PACKET_ETHERTYPE_IPV4,PACKET_IPPROTO_TCP,80},
    };
    for (uint32_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        n=make_packet(frame,cases[i].et,cases[i].proto,cases[i].port);
        if (ingress_filter_run(frame,n)!=filter_manager_run(&manager,frame,n,0,NULL)) mismatches++;
    }
    for (uint32_t i=0;i<10000u;i++) {
        n=rnd32()%80u; for (uint32_t j=0;j<n;j++) frame[j]=(uint8_t)rnd32();
        if (ingress_filter_run(frame,n)!=filter_manager_run(&manager,frame,n,0,NULL)) mismatches++;
    }
    CHECK(mismatches==0,"native reference and metadata VM agree on 10,007 frames");

    /* SHA-256 and HMAC known-answer tests. */
    static const uint8_t sha_abc[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    uint8_t digest[32]; filter_sha256((const uint8_t *)"abc",3,digest);
    CHECK(filter_constant_time_equal(digest,sha_abc,32),"SHA-256 known answer");
    static const uint8_t hmac_quick[32]={0xf7,0xbc,0x83,0xf4,0x30,0x53,0x84,0x24,0xb1,0x32,0x98,0xe6,0xaa,0x6f,0xb1,0x43,0xef,0x4d,0x59,0xa1,0x49,0x46,0x17,0x59,0x97,0x47,0x9d,0xbc,0x2d,0x1a,0x3c,0xd8};
    const uint8_t *quick=(const uint8_t *)"The quick brown fox jumps over the lazy dog";
    filter_hmac_sha256_2((const uint8_t *)"key",3,quick,20,quick+20,23,digest);
    CHECK(filter_constant_time_equal(digest,hmac_quick,32),"HMAC-SHA256 known answer");

    /* Authenticated generation update, replay rejection, rollback and recovery. */
    static const vm_insn_t pass_all[]={ {VM_OP_RET,0,0,0,VM_VERDICT_PASS} };
    uint8_t package[FILTER_UPDATE_MAX_MSG];
    uint32_t package_len=filter_update_build(package,sizeof(package),0x12345678u,1u,0u,
                                             pass_all,1,key,sizeof(key)-1u);
    vm_verify_result_t why;
    CHECK(filter_manager_install(&manager,package,package_len,&why)==FILTER_UPDATE_OK &&
          manager.active_generation==1u && s_enter==s_exit && s_enter>0,
          "authenticated generation installs with RTOS critical pointer swap");
    CHECK(filter_manager_run(&manager,frame,n,0,NULL)==VM_VERDICT_PASS,
          "new pass-all program is active");
    CHECK(filter_manager_install(&manager,package,package_len,&why)==FILTER_UPDATE_ERR_GENERATION,
          "replayed generation is rejected");
    package[28]^=1u;
    put16(package+12,2u); /* corrupting generation/tag: generation 2 reaches auth check */
    CHECK(filter_manager_install(&manager,package,package_len,&why)==FILTER_UPDATE_ERR_AUTH,
          "bad HMAC is rejected");
    package[28]^=1u;
    CHECK(filter_manager_rollback(&manager) && manager.active_generation==0u,
          "rollback restores the previous recovery program");
    n=make_packet(frame,PACKET_ETHERTYPE_IPV4,PACKET_IPPROTO_UDP,1900);
    CHECK(filter_manager_run(&manager,frame,n,0,NULL)==VM_VERDICT_DROP,
          "rollback restores strict verdict");
    CHECK(filter_manager_rollback(&manager) && manager.active_generation==1u,
          "rollback can atomically return to the updated generation");
    filter_manager_recover(&manager);
    CHECK(manager.active_generation==0u,"explicit recovery restores built-in policy");

    package_len=filter_update_build(package,sizeof(package),0x12345678u,2u,0u,
                                    meta_filter_insns,meta_filter_program.n_insns,
                                    key,sizeof(key)-1u);
    CHECK(filter_manager_install(&manager,package,package_len,&why)==FILTER_UPDATE_ERR_VERIFY &&
          why==VM_VERIFY_ERR_BAD_HELPER,
          "metadata update is refused when its helper capability is omitted");
    package_len=filter_update_build(package,sizeof(package),0x12345678u,2u,
                                    VM_HELPER_BIT(VM_HELPER_RATE_SERVICE),
                                    meta_filter_insns,meta_filter_program.n_insns,
                                    key,sizeof(key)-1u);
    CHECK(filter_manager_install(&manager,package,package_len,&why)==FILTER_UPDATE_OK &&
          manager.active_generation==2u,
          "authenticated metadata bytecode loads into an inactive bank and activates");

    /* Bounded rate helper: service capacity, refill, global and source paths. */
    CHECK(rate_limiter_set_service(&manager.limiter,META_RATE_CLASS_DNS,2u,1u),
          "DNS service bucket configured");
    n=make_packet(frame,PACKET_ETHERTYPE_IPV4,PACKET_IPPROTO_UDP,53);
    CHECK(filter_manager_run(&manager,frame,n,0,NULL)==VM_VERDICT_PASS &&
          filter_manager_run(&manager,frame,n,0,NULL)==VM_VERDICT_PASS &&
          filter_manager_run(&manager,frame,n,0,NULL)==VM_VERDICT_DROP,
          "VM service helper enforces bounded burst");
    CHECK(filter_manager_run(&manager,frame,n,1000,NULL)==VM_VERDICT_PASS,
          "service helper refills from wrap-safe ticks");
    rate_limiter_t direct; rate_limiter_init(&direct,1000,0); rate_limiter_set_global(&direct,1,0);
    uint32_t regs[VM_NUM_REGS]={0}; regs[1]=1; int ok=0;
    CHECK(rate_limiter_vm_helper(&direct,VM_HELPER_RATE_GLOBAL,regs,&ok)==1u && ok &&
          rate_limiter_vm_helper(&direct,VM_HELPER_RATE_GLOBAL,regs,&ok)==0u,
          "global rate helper is bounded");
    rate_limiter_init(&direct,1000,0); rate_limiter_set_global(&direct,1,0); regs[1]=0x0a000002u; regs[2]=1;
    CHECK(rate_limiter_vm_helper(&direct,VM_HELPER_RATE_SOURCE,regs,&ok)==1u &&
          rate_limiter_vm_helper(&direct,VM_HELPER_RATE_SOURCE,regs,&ok)==0u,
          "fixed source-table helper is bounded");
    rate_limiter_init(&direct,1000u,0xfffffe0bu);
    CHECK(rate_limiter_set_service(&direct,0,1,1),"wrap test service configured");
    regs[1]=0;
    CHECK(rate_limiter_vm_helper(&direct,VM_HELPER_RATE_SERVICE,regs,&ok)==1u &&
          rate_limiter_vm_helper(&direct,VM_HELPER_RATE_SERVICE,regs,&ok)==0u,
          "wrap test consumes initial token");
    rate_limiter_set_now(&direct,499u);
    CHECK(rate_limiter_vm_helper(&direct,VM_HELPER_RATE_SERVICE,regs,&ok)==1u,
          "token refill uses wrap-safe unsigned tick subtraction");

    /* Management queue copies packages and processes them outside RX/ISR. */
    filter_management_task_t task; s_result_calls=0;
    filter_management_task_init(&task,&manager,NULL,result_cb,NULL);
    package_len=filter_update_build(package,sizeof(package),0x12345678u,3u,0u,
                                    pass_all,1,key,sizeof(key)-1u);
    CHECK(filter_management_submit(&task,package,package_len) &&
          filter_management_process_one(&task) && s_result_calls==1 && s_last_result==FILTER_UPDATE_OK,
          "management task installs a queued authenticated update");
}
