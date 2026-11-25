#ifndef ETH_HW_SIM_H
#define ETH_HW_SIM_H

/*
 * eth_hw_sim -- a simulated STM32H5 Ethernet MAC + RX DMA, and the part of
 * ST's HAL that the receive path uses (HAL_ETH_ReadData and the descriptor
 * refill), so ST's ethernetif.c logic can run unchanged in spirit.
 *
 * What is modelled (read from HAL 1.6.0 stm32h5xx_hal_eth.c, 28 Oct 2026):
 *   - a ring of ETH_RX_DESC_CNT (4) RX descriptors, each owned either by the
 *     DMA (empty, waiting) or by software (holds a received segment)
 *   - a frame longer than one buffer uses several descriptors (FD ... LD)
 *   - no free descriptor for a frame => the MAC drops it and counts it
 *     (like the ETH->DMACMFCR missed-frame counter)
 *   - one RX interrupt per frame => HAL_ETH_RxCpltCallback()
 *   - HAL_ETH_ReadData(): one frame per call; calls HAL_ETH_RxLinkCallback()
 *     per segment; returns HAL_OK with *buff == pRxStart, which is NULL if
 *     the link callback linked nothing (our DROP; basis of D5); returns
 *     HAL_ERROR if no complete frame is ready
 *   - refill: HAL_ETH_RxAllocateCallback() only for descriptors whose
 *     buffer was consumed (BackupAddr0 == 0)
 * Not modelled: timing, CRC/checksum hardware, timestamps, TX descriptors.
 */

#include <stdint.h>

#define ETH_RX_DESC_CNT   4u     /* ST example */
#define HAL_OK            0
#define HAL_ERROR         1

typedef struct {
    uint32_t  own;     /* 1 = owned by the DMA (empty, armed)            */
    uint32_t  fd, ld;  /* first / last segment of a frame                */
    uint32_t  pl;      /* packet length so far, incl. this segment (PL)  */
    uintptr_t buf;     /* BackupAddr0: buffer attached (0 = consumed)    */
} sim_desc_t;

typedef struct {
    sim_desc_t desc[ETH_RX_DESC_CNT];
    uint32_t   buf_len;          /* heth.Init.RxBuffLen                     */
    uint32_t   dma_idx;          /* next descriptor the DMA writes          */
    /* HAL's RxDescList fields that ReadData/UpdateDescriptor use */
    uint32_t   RxDescIdx, RxBuildDescIdx, RxBuildDescCnt, RxDataLength;
    void      *pRxStart, *pRxEnd;
    /* statistics */
    uint32_t   missed_frames;    /* like ETH->DMACMFCR                      */
    uint32_t   frames_written;   /* frames the DMA put into buffers         */
} sim_eth_t;

/* ST's callbacks, implemented in ethernetif_sim.c (same signatures as HAL 1.6.0) */
void HAL_ETH_RxAllocateCallback(uint8_t **buff);
void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length);
void HAL_ETH_RxCpltCallback(sim_eth_t *heth);

void sim_eth_start(sim_eth_t *heth, uint32_t buf_len);              /* HAL_ETH_Start_IT */
int  HAL_ETH_ReadData(sim_eth_t *heth, void **pAppBuff);
void sim_eth_refill(sim_eth_t *heth);                               /* ETH_UpdateDescriptor */

/* The "wire": the MAC receives one frame. Returns 1 if stored, 0 if missed. */
int  sim_eth_wire_rx(sim_eth_t *heth, const uint8_t *frame, uint32_t len);

/* Complete frames waiting in the ring (software-owned, starting with FD). */
uint32_t sim_eth_frames_ready(const sim_eth_t *heth);

#endif /* ETH_HW_SIM_H */
