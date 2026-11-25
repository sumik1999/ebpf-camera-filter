/* lwipopts.h -- lwIP options for the simulated firmware.
 *
 * Copied from ST's example lwipopts.h (LwIP_UDPTCP_Echo_Server_Netconn_RTOS,
 * coremw-apps v2.1.0) wherever the option affects the receive path, so the
 * instruction counts reflect ST's configuration. Deliberate differences:
 *   - NO_SYS 1 unless SIM_RTOS=1 (the FreeRTOS build, plan step 9c)
 *   - LWIP_DHCP 0: static IP 192.168.10.2 (our plan); no DHCP traffic
 *   - LWIP_TCP 0, no netconn: the simulation uses a UDP echo on port 7
 *   - our two hook lines (D7: no #include of lwIP headers here)
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#ifndef SIM_RTOS
#define SIM_RTOS 0
#endif

#if SIM_RTOS
#define NO_SYS                  0
#define SYS_LIGHTWEIGHT_PROT    1
#define TCPIP_THREAD_NAME       "TCP/IP"
#define TCPIP_THREAD_STACKSIZE  2048        /* ST: 2048 bytes */
#define TCPIP_MBOX_SIZE         6           /* ST */
#define TCPIP_THREAD_PRIO       40          /* ST: osPriorityHigh (40), below the RX task (48) */
#define DEFAULT_THREAD_STACKSIZE 1024
#define LWIP_TCPIP_CORE_LOCKING 1           /* ST */
#define LWIP_NETCONN            0
#else
#define NO_SYS                  1
#define SYS_LIGHTWEIGHT_PROT    0
#define LWIP_NETCONN            0
#endif
#define LWIP_SOCKET             0

/* Memory (ST values) */
#if defined(__aarch64__) || defined(__x86_64__)
#define MEM_ALIGNMENT           8           /* Mac host build: 64-bit pointers */
#else
#define MEM_ALIGNMENT           4           /* ST value (Cortex-M33) */
#endif
#define MEM_SIZE                (14*1024)
#define PBUF_POOL_BUFSIZE       1536
#define LWIP_SUPPORT_CUSTOM_PBUF 1          /* ST's zero-copy RX_POOL */

/* Protocols */
#define LWIP_ARP                1
#define LWIP_ICMP               1
#define LWIP_UDP                1
#define UDP_TTL                 255
#define LWIP_TCP                0
#define LWIP_DHCP               0
#define LWIP_IGMP               0
#define LWIP_DNS                0
#define LWIP_RAW                0
#define LWIP_IPV6               0
#define LWIP_STATS              0
#define LWIP_NETIF_LINK_CALLBACK 0

/* Checksums: as ST with CHECKSUM_BY_HARDWARE (the MAC checks/inserts them) */
#define CHECKSUM_GEN_IP         0
#define CHECKSUM_GEN_UDP        0
#define CHECKSUM_GEN_TCP        0
#define CHECKSUM_CHECK_IP       0
#define CHECKSUM_CHECK_UDP      0
#define CHECKSUM_CHECK_TCP      0
#define CHECKSUM_GEN_ICMP       1
#define CHECKSUM_CHECK_ICMP     0

/* XDP: the late hook (D7: only these two lines; no lwIP #include here) */
#define LWIP_HOOK_FILENAME "lwip_ip4_hook.h"
#define LWIP_HOOK_IP4_INPUT(p, inp) lwip_ip4_hook_input((p), (inp))

#endif /* LWIPOPTS_H */
