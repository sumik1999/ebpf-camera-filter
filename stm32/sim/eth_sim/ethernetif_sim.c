/*
 * ethernetif_sim.c -- the receive side of ST's ethernetif.c (NUCLEO-H563ZI
 * LwIP_UDPTCP_Echo_Server_Netconn_RTOS, coremw-apps v2.1.0, read 28 Oct 2026),
 * re-created for the simulation. Same structures and same order of
 * operations: zero-copy RX_POOL of 12 x RxBuff_t (1000-byte buffers), the
 * three HAL RX callbacks, low_level_input() and the RX task loop.
 *
 * Changes against ST's code, each marked "XDP:":
 *   - HAL_ETH_RxLinkCallback() calls the early hook on a frame's first
 *     segment (the 4 lines documented in firmware/Network_Hooks/xdp_dma_hook.h)
 *   - the RX loop exists in ST's form and in the D5-fixed form
 *   - low_level_output() only classifies and counts frames (no TX hardware)
 */
#include <string.h>
#include <stddef.h>

#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/memp.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "ethernetif_sim.h"
#include "xdp_dma_hook.h"

#if SIM_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
SemaphoreHandle_t g_rx_sem;                       /* ST: RxPktSemaphore (binary) */
#define RX_SEM_GIVE() ((void)xSemaphoreGive(g_rx_sem))
#else
#define RX_SEM_GIVE() (RxPktSemaphore = 1)
#endif

#define ETH_RX_BUFFER_SIZE   1000u   /* ST */
#define ETH_RX_BUFFER_CNT    12u     /* ST */

typedef struct {
    struct pbuf_custom pbuf_custom;
    uint8_t buff[(ETH_RX_BUFFER_SIZE + 31) & ~31] __attribute__((aligned(32)));
} RxBuff_t;

LWIP_MEMPOOL_DECLARE(RX_POOL, ETH_RX_BUFFER_CNT, sizeof(RxBuff_t), "Zero-copy RX PBUF pool");

typedef enum { RX_ALLOC_OK = 0, RX_ALLOC_ERROR = 1 } RxAllocStatusTypeDef;
static uint8_t RxAllocStatus;
static uint32_t s_pool_in_use;

sim_eth_t EthHandle;
volatile int RxPktSemaphore;
ethif_stats_t g_ethif;

static void pbuf_free_custom(struct pbuf *p)
{
    struct pbuf_custom *custom_pbuf = (struct pbuf_custom *)p;
    LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);
    s_pool_in_use--;
    if (RxAllocStatus == RX_ALLOC_ERROR) {
        RxAllocStatus = RX_ALLOC_OK;
        RX_SEM_GIVE();                            /* osSemaphoreRelease(RxPktSemaphore) */
    }
}

void HAL_ETH_RxCpltCallback(sim_eth_t *heth)      /* ISR context on the board */
{
    (void)heth;
#if SIM_RTOS
    BaseType_t woken = pdFALSE;                   /* osSemaphoreRelease() from the ISR */
    xSemaphoreGiveFromISR(g_rx_sem, &woken);
    portYIELD_FROM_ISR(woken);
#else
    RxPktSemaphore = 1;                           /* osSemaphoreRelease(RxPktSemaphore) */
#endif
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    struct pbuf_custom *p = LWIP_MEMPOOL_ALLOC(RX_POOL);
    if (p) {
        *buff = (uint8_t *)p + offsetof(RxBuff_t, buff);
        p->custom_free_function = pbuf_free_custom;
        pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, p, *buff, ETH_RX_BUFFER_SIZE);
        if (++s_pool_in_use > g_ethif.pool_in_use_max) g_ethif.pool_in_use_max = s_pool_in_use;
    } else {
        RxAllocStatus = RX_ALLOC_ERROR;
        g_ethif.rx_alloc_errors++;
        *buff = NULL;
    }
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
    struct pbuf **ppStart = (struct pbuf **)pStart;
    struct pbuf **ppEnd = (struct pbuf **)pEnd;
    struct pbuf *p = (struct pbuf *)(buff - offsetof(RxBuff_t, buff));

    /* XDP: early filter on the first segment of each frame */
    if (*ppStart == NULL && xdp_hook_run(buff, Length) == VM_VERDICT_DROP) {
        pbuf_free(p);   /* custom pbuf -> back to RX_POOL */
        return;         /* nothing linked: lwIP never sees the frame */
    }

    p->next = NULL;
    p->tot_len = 0;
    p->len = Length;
    if (!*ppStart) {
        *ppStart = p;
    } else {
        (*ppEnd)->next = p;
    }
    *ppEnd = p;
    for (p = *ppStart; p != NULL; p = p->next) {
        p->tot_len += Length;
    }
}

static struct pbuf *low_level_input(struct netif *netif)
{
    struct pbuf *p = NULL;
    (void)netif;
    if (RxAllocStatus == RX_ALLOC_OK) {
        HAL_ETH_ReadData(&EthHandle, (void **)&p);
    }
    return p;
}

void ethernetif_rx_drain(struct netif *netif, rx_loop_t loop)
{
    struct pbuf *p = NULL;
    if (loop == RX_LOOP_ST) {
        do {                                       /* ST v2.1.0, unchanged */
            p = low_level_input(netif);
            if (p != NULL) {
                g_ethif.delivered++;
                if (netif->input(p, netif) != ERR_OK) {
                    pbuf_free(p);
                }
            }
        } while (p != NULL);
    } else {
        /* XDP: D5 fix -- drain on the ReadData status, so a dropped frame
         * (HAL_OK with p == NULL) does not stop the loop. */
        while (RxAllocStatus == RX_ALLOC_OK && HAL_ETH_ReadData(&EthHandle, (void **)&p) == HAL_OK) {
            if (p != NULL) {
                g_ethif.delivered++;
                if (netif->input(p, netif) != ERR_OK) {
                    pbuf_free(p);
                }
            }
            p = NULL;
        }
    }
}

/* XDP: no TX hardware in the simulation -- classify and count what lwIP sends. */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    uint8_t h[40];
    (void)netif;
    memset(h, 0, sizeof h);
    pbuf_copy_partial(p, h, sizeof h, 0);
    g_ethif.tx_frames++;
    uint16_t et = (uint16_t)((h[12] << 8) | h[13]);
    if (et == 0x0806) {
        g_ethif.tx_arp++;
    } else if (et == 0x0800 && h[23] == 17 && ((h[34] << 8) | h[35]) == 7) {
        g_ethif.tx_echo++;
    } else if (et == 0x0800 && h[23] == 17 && ((h[34] << 8) | h[35]) == 9000) {
        g_ethif.tx_ctrl++;
    } else if (et == 0x0800 && h[23] == 1 && h[34] == 0) {
        g_ethif.tx_ping_reply++;
    } else if (et == 0x0800 && h[23] == 1 && h[34] == 3) {
        g_ethif.tx_icmp_unreach++;
    } else {
        g_ethif.tx_other++;
    }
    return ERR_OK;
}

err_t ethernetif_init(struct netif *netif)
{
    static const uint8_t mac[6] = { 0x00, 0x80, 0xE1, 0x00, 0x05, 0x77 };   /* ST example MAC */
    netif->name[0] = 's';
    netif->name[1] = 't';
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, mac, 6);
    netif->mtu = 1500;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

    LWIP_MEMPOOL_INIT(RX_POOL);
    RxAllocStatus = RX_ALLOC_OK;
    s_pool_in_use = 0;
    memset(&g_ethif, 0, sizeof g_ethif);
    RxPktSemaphore = 0;
#if SIM_RTOS
    g_rx_sem = xSemaphoreCreateBinary();
    if (g_rx_sem == NULL) return ERR_MEM;
#endif
    sim_eth_start(&EthHandle, ETH_RX_BUFFER_SIZE);
    return ERR_OK;
}
