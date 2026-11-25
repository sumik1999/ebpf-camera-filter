/*
 * eth_hw_sim.c -- see eth_hw_sim.h. HAL_ETH_ReadData() and sim_eth_refill()
 * follow HAL 1.6.0's HAL_ETH_ReadData() / ETH_UpdateDescriptor() step by
 * step (STMicroelectronics, BSD-3-Clause), minus timestamps and registers.
 */
#include <string.h>
#include "eth_hw_sim.h"

#define INCR(i) ((i) = ((i) + 1u) % ETH_RX_DESC_CNT)

void sim_eth_refill(sim_eth_t *heth)
{
    uint32_t idx = heth->RxBuildDescIdx;
    uint32_t count = heth->RxBuildDescCnt;
    int alloc_ok = 1;

    while (count > 0u && alloc_ok) {
        sim_desc_t *d = &heth->desc[idx];
        if (d->buf == 0u) {                       /* buffer was consumed: get a new one */
            uint8_t *buff = NULL;
            HAL_ETH_RxAllocateCallback(&buff);
            if (buff == NULL) {
                alloc_ok = 0;                     /* pool empty: stop, retry later */
            } else {
                d->buf = (uintptr_t)buff;
            }
        }
        if (alloc_ok) {
            d->own = 1u; d->fd = 0u; d->ld = 0u; d->pl = 0u;   /* give it back to the DMA */
            INCR(idx);
            count--;
        }
    }
    if (heth->RxBuildDescCnt != count) {
        heth->RxBuildDescIdx = idx;
        heth->RxBuildDescCnt = count;
    }
}

void sim_eth_start(sim_eth_t *heth, uint32_t buf_len)
{
    memset(heth, 0, sizeof *heth);
    heth->buf_len = buf_len;
    heth->RxBuildDescCnt = ETH_RX_DESC_CNT;       /* all descriptors need a buffer */
    sim_eth_refill(heth);
}

int sim_eth_wire_rx(sim_eth_t *heth, const uint8_t *frame, uint32_t len)
{
    uint32_t nseg = (len + heth->buf_len - 1u) / heth->buf_len;
    if (len == 0u || nseg > ETH_RX_DESC_CNT) {
        heth->missed_frames++;
        return 0;
    }
    /* The DMA needs nseg consecutive armed descriptors, else the frame is lost. */
    for (uint32_t k = 0, i = heth->dma_idx; k < nseg; k++, INCR(i)) {
        if (!heth->desc[i].own) {
            heth->missed_frames++;
            return 0;
        }
    }
    uint32_t off = 0;
    for (uint32_t k = 0; k < nseg; k++) {
        sim_desc_t *d = &heth->desc[heth->dma_idx];
        uint32_t n = (len - off < heth->buf_len) ? len - off : heth->buf_len;
        memcpy((void *)d->buf, frame + off, n);
        off += n;
        d->fd = (k == 0u);
        d->ld = (k == nseg - 1u);
        d->pl = off;
        d->own = 0u;                              /* hand it to software */
        INCR(heth->dma_idx);
    }
    heth->frames_written++;
#if !defined(SIM_RTOS) || !SIM_RTOS
    HAL_ETH_RxCpltCallback(heth);                 /* RX interrupt (no-RTOS model: called directly) */
#endif                                            /* RTOS build: the real NVIC interrupt calls it */
    return 1;
}

int HAL_ETH_ReadData(sim_eth_t *heth, void **pAppBuff)
{
    uint32_t idx = heth->RxDescIdx;
    uint32_t desccnt = 0;
    uint32_t desccntmax = ETH_RX_DESC_CNT - heth->RxBuildDescCnt;
    int ready = 0;

    if (pAppBuff == NULL) {
        return HAL_ERROR;
    }
    while (!heth->desc[idx].own && desccnt < desccntmax && !ready) {
        sim_desc_t *d = &heth->desc[idx];
        if (d->fd || heth->pRxStart != NULL) {   /* process only a frame's first segment
                                                     or a frame already being linked */
            if (d->fd) {
                heth->RxDataLength = 0;
            }
            uint32_t bufflength = d->pl - heth->RxDataLength;
            if (d->ld) {
                ready = 1;
            }
            HAL_ETH_RxLinkCallback(&heth->pRxStart, &heth->pRxEnd,
                                   (uint8_t *)d->buf, (uint16_t)bufflength);
            heth->RxDataLength += bufflength;
            d->buf = 0u;                          /* "Clear buffer pointer" */
        }
        INCR(idx);
        desccnt++;
    }
    heth->RxBuildDescCnt += desccnt;
    if (heth->RxBuildDescCnt != 0u) {
        sim_eth_refill(heth);
    }
    heth->RxDescIdx = idx;
    if (ready) {
        *pAppBuff = heth->pRxStart;               /* NULL if nothing was linked (DROP) */
        heth->pRxStart = NULL;
        return HAL_OK;
    }
    return HAL_ERROR;
}

uint32_t sim_eth_frames_ready(const sim_eth_t *heth)
{
    uint32_t n = 0;
    uint32_t idx = heth->RxDescIdx;
    for (uint32_t k = 0; k < ETH_RX_DESC_CNT - heth->RxBuildDescCnt; k++, INCR(idx)) {
        if (heth->desc[idx].own) break;
        if (heth->desc[idx].fd) n++;
    }
    return n;
}
