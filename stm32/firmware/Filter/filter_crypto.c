#include "filter_crypto.h"

#include <stddef.h>

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    uint32_t used;
} sha256_ctx_t;

static const uint32_t k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}

static void sha_init(sha256_ctx_t *c)
{
    static const uint32_t init[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                                      0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    for (uint32_t i=0;i<8;i++) c->h[i]=init[i];
    c->bytes=0; c->used=0;
}

static void sha_block(sha256_ctx_t *c, const uint8_t *p)
{
    uint32_t w[64];
    for (uint32_t i=0;i<16;i++) w[i]=be32(p+4u*i);
    for (uint32_t i=16;i<64;i++) {
        uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    uint32_t a=c->h[0],b=c->h[1],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7],cc=c->h[2];
    for (uint32_t i=0;i<64;i++) {
        uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25);
        uint32_t ch=(e&f)^((~e)&g);
        uint32_t t1=h+s1+ch+k[i]+w[i];
        uint32_t s0=rotr(a,2)^rotr(a,13)^rotr(a,22);
        uint32_t maj=(a&b)^(a&cc)^(b&cc);
        uint32_t t2=s0+maj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d;
    c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=h;
}

static void sha_update(sha256_ctx_t *c, const uint8_t *data, uint32_t len)
{
    if (data == NULL && len != 0u) return;
    c->bytes += len;
    while (len != 0u) {
        uint32_t take=64u-c->used;
        if (take>len) take=len;
        for (uint32_t i=0;i<take;i++) c->block[c->used+i]=data[i];
        c->used+=take; data+=take; len-=take;
        if (c->used==64u) { sha_block(c,c->block); c->used=0; }
    }
}

static void sha_final(sha256_ctx_t *c, uint8_t out[32])
{
    uint64_t bits=c->bytes*8u;
    c->block[c->used++]=0x80u;
    if (c->used>56u) {
        while (c->used<64u) c->block[c->used++]=0;
        sha_block(c,c->block); c->used=0;
    }
    while (c->used<56u) c->block[c->used++]=0;
    for (uint32_t i=0;i<8;i++) c->block[63u-i]=(uint8_t)(bits>>(8u*i));
    sha_block(c,c->block);
    for (uint32_t i=0;i<8;i++) put32(out+4u*i,c->h[i]);
}

void filter_sha256(const uint8_t *data, uint32_t len, uint8_t out[32])
{
    sha256_ctx_t c; sha_init(&c); sha_update(&c,data,len); sha_final(&c,out);
}

void filter_hmac_sha256_2(const uint8_t *key, uint32_t key_len,
                          const uint8_t *a, uint32_t a_len,
                          const uint8_t *b, uint32_t b_len, uint8_t out[32])
{
    uint8_t kh[32], pad[64], inner[32];
    if (key_len>64u) { filter_sha256(key,key_len,kh); key=kh; key_len=32u; }
    for (uint32_t i=0;i<64;i++) pad[i]=(uint8_t)(0x36u ^ (i<key_len?key[i]:0u));
    sha256_ctx_t c; sha_init(&c); sha_update(&c,pad,64); sha_update(&c,a,a_len); sha_update(&c,b,b_len); sha_final(&c,inner);
    for (uint32_t i=0;i<64;i++) pad[i]=(uint8_t)(0x5cu ^ (i<key_len?key[i]:0u));
    sha_init(&c); sha_update(&c,pad,64); sha_update(&c,inner,32); sha_final(&c,out);
}

int filter_constant_time_equal(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    if (a==NULL || b==NULL) return 0;
    uint8_t diff=0;
    for (uint32_t i=0;i<len;i++) diff|=(uint8_t)(a[i]^b[i]);
    return diff==0u;
}
