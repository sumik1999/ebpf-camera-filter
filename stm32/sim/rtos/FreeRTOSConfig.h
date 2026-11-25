/*
 * FreeRTOSConfig.h -- FreeRTOS 11.2.0 configuration for the simulated
 * firmware on QEMU mps2-an505 (Cortex-M33).
 *
 * Copied from ST's example (NUCLEO-H563ZI LwIP_UDPTCP_Echo_Server_Netconn_RTOS,
 * coremw-apps v2.1.0, Core/Inc/FreeRTOSConfig.h) value by value, except
 * where marked "DIFF:".
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

#define configENABLE_TRUSTZONE                   0
/* DIFF: QEMU's AN505 CPU starts and stays in the Secure state; the NTZ port
 * must then use secure EXC_RETURN values. On the STM32 (TrustZone off) ST uses 0. */
#define configRUN_FREERTOS_SECURE_ONLY           1
/* DIFF (the D13 fix): ST has 0 while building with -mfloat-abi=hard, so FPU
 * registers would not be saved on a task switch. */
#define configENABLE_FPU                         1
#define configENABLE_MPU                         0
#define configENABLE_MVE                         0

#define configUSE_PREEMPTION                     1
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configKERNEL_PROVIDED_STATIC_MEMORY      1
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
/* DIFF: QEMU's AN505 SysTick runs from the 25 MHz system clock (ST: SystemCoreClock, 250 MHz). */
#define configCPU_CLOCK_HZ                       ( 25000000UL )
#define configTICK_RATE_HZ                       ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                     ( 56 )
#define configMINIMAL_STACK_SIZE                 ( ( uint16_t ) 128 )
/* DIFF: 32 KB instead of 20 KB, for the test-harness feeder task (4 KB stack for stdio). */
#define configTOTAL_HEAP_SIZE                    ( ( size_t ) ( 32 * 1024 ) )
#define configMAX_TASK_NAME_LEN                  ( 16 )
#define configUSE_TRACE_FACILITY                 1
#define configUSE_16_BIT_TICKS                   0
#define configUSE_MUTEXES                        1
#define configQUEUE_REGISTRY_SIZE                8
#define configUSE_RECURSIVE_MUTEXES              1
#define configUSE_COUNTING_SEMAPHORES            1
/* DIFF: 1 (ST: 0) because lwIP's FreeRTOS port still uses old names such as
 * portTICK_RATE_MS; it only adds those alias macros. (ST uses its own CMSIS sys_arch.) */
#define configENABLE_BACKWARD_COMPATIBILITY      1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_MINI_LIST_ITEM                 1
#define configUSE_EVENT_GROUPS                   1
#define configUSE_STREAM_BUFFERS                 1
#define configCHECK_HANDLER_INSTALLATION         1
#define configMESSAGE_BUFFER_LENGTH_TYPE         size_t
#define configRUN_TIME_COUNTER_TYPE              size_t
#define configUSE_CO_ROUTINES                    0
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                ( 2 )
#define configTIMER_QUEUE_LENGTH                 10
#define configTIMER_TASK_STACK_DEPTH             128

/* DIFF: checks ST's example leaves off, switched on so problems are reported. */
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1

/* Interrupt priorities. DIFF: QEMU's AN505 NVIC implements 3 priority bits
 * (the STM32H5 has 4), so ST's levels 15/5 become 7/2 here. Same meaning:
 * the lowest level for the kernel; interrupts at or below MAX_SYSCALL may
 * call FreeRTOS "FromISR" functions. */
#define configPRIO_BITS                          3
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY  7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 2
#define configKERNEL_INTERRUPT_PRIORITY      ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* DIFF: ST's configASSERT just stops; this one also reports where (exit code 5). */
void rtos_assert_failed(const char *file, int line);
#define configASSERT( x ) do { if ( ( x ) == 0 ) rtos_assert_failed( __FILE__, __LINE__ ); } while ( 0 )

#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_xTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTimerPendFunctionCall       1
#define INCLUDE_xQueueGetMutexHolder         1
#define INCLUDE_xSemaphoreGetMutexHolder     1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_eTaskGetState                1
#define INCLUDE_xTaskAbortDelay              1

#endif /* FREERTOS_CONFIG_H */
