#ifndef ETHERNETIF_SIM_H
#define ETHERNETIF_SIM_H

#include <stdint.h>
#include "lwip/netif.h"
#include "eth_hw_sim.h"

/* Which RX task loop to run (D5). */
typedef enum {
    RX_LOOP_ST = 0,     /* ST v2.1.0: do { p = low_level_input(); ... } while (p != NULL); */
    RX_LOOP_FIXED = 1,  /* D5 fix: keep reading while HAL_ETH_ReadData() returns HAL_OK    */
} rx_loop_t;

typedef struct {
    uint32_t rx_alloc_errors;   /* RX_POOL empty at refill time                  */
    uint32_t pool_in_use_max;   /* most RX_POOL buffers in use at once           */
    uint32_t tx_frames, tx_arp, tx_echo, tx_ping_reply, tx_icmp_unreach, tx_ctrl, tx_other;
    uint32_t delivered;         /* frames handed to lwIP (netif->input)          */
} ethif_stats_t;

extern sim_eth_t EthHandle;
extern volatile int RxPktSemaphore;    /* binary semaphore: 1 = given        */
extern ethif_stats_t g_ethif;
#if SIM_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
extern SemaphoreHandle_t g_rx_sem;      /* given by the RX interrupt, taken by the RX task */
#endif

err_t ethernetif_init(struct netif *netif);

/* Body of ST's ethernetif_input task after the semaphore was taken: read
 * frames from the driver and pass them to lwIP, using the chosen loop. */
void ethernetif_rx_drain(struct netif *netif, rx_loop_t loop);

#endif /* ETHERNETIF_SIM_H */
