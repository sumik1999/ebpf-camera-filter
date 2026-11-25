#!/bin/sh
# xc_check.sh -- cross-compile check of our firmware code for the Cortex-M33.
#
# Compiles VM_Engine, Network_Hooks and DDoS_Prototype with the Arm GNU
# toolchain and -Werror. The lwIP glue (lwip_ip4_hook_glue.c) and lwIP's own
# ip4.c with our hook registered are compiled against the pinned ST package
# (lwIP 2.2.1 with the example's lwipopts.h). Prints code size and stack use.
#
# Usage (from the repo root):  sh tools/xc_check.sh
# Objects go to build/xc/ (git-ignored).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VENDOR=${VENDOR:-$HOME/Projects/vendor/stm32h5-classic-coremw-apps}
EX=$VENDOR/Projects/NUCLEO-H563ZI/Applications/LwIP/LwIP_UDPTCP_Echo_Server_Netconn_RTOS
MW=$VENDOR/Middlewares/Third_Party
ARM_BIN=${ARM_BIN:-/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin}
CC=$ARM_BIN/arm-none-eabi-gcc
SIZE=$ARM_BIN/arm-none-eabi-size
OUT=$ROOT/build/xc

CFLAGS="-std=gnu11 -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -O2 -Wall -Wextra -Werror -fstack-usage"
# Our paths are relative to build/xc: the repo path may contain spaces.
R=../..
OURINC="-I$R/firmware/VM_Engine -I$R/firmware/Network_Hooks -I$R/firmware/DDoS_Prototype"
LWIPINC="-DSTM32H563xx -DUSE_HAL_DRIVER -I$EX/LWIP/Target -I$EX/Core/Inc -I$MW/LwIP/src/include -I$MW/LwIP/system \
 -I$MW/FreeRTOS/Source/include -I$MW/FreeRTOS/Source/CMSIS_RTOS_V2 -I$MW/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure \
 -I$VENDOR/Drivers/CMSIS/Include -I$VENDOR/Drivers/CMSIS/Core/Include -I$VENDOR/Drivers/STM32H5xx_HAL_Driver/Inc \
 -I$VENDOR/Drivers/CMSIS/Device/ST/STM32H5xx/Include"

rm -rf "$OUT"; mkdir -p "$OUT"; cd "$OUT"
"$CC" --version | head -1

# Pure C: no vendor headers at all.
for f in $R/firmware/VM_Engine/*.c $R/firmware/Network_Hooks/xdp_dma_hook.c \
         $R/firmware/Network_Hooks/lwip_ip4_hook.c $R/firmware/DDoS_Prototype/*.c; do
    [ -f "$f" ] || continue
    "$CC" $CFLAGS $OURINC -c "$f"
done

# Needs lwIP headers.
"$CC" $CFLAGS $OURINC $LWIPINC -c $R/firmware/Network_Hooks/lwip_ip4_hook_glue.c

# lwIP's ip4.c (vendor code, not edited) with our hook registered the way
# lwipopts.h will do it (D7). Vendor warnings are not ours to fix, so no -Werror.
"$CC" ${CFLAGS%-Werror*} -fstack-usage $OURINC $LWIPINC \
    '-DLWIP_HOOK_FILENAME="lwip_ip4_hook.h"' \
    '-DLWIP_HOOK_IP4_INPUT(p,inp)=lwip_ip4_hook_input((p),(inp))' \
    -c "$MW/LwIP/src/core/ipv4/ip4.c" -o ip4_with_hook.o
echo "lwIP ip4.c compiled with LWIP_HOOK_IP4_INPUT -> lwip_ip4_hook_input"
if "$ARM_BIN/arm-none-eabi-nm" -u ip4_with_hook.o | grep -q lwip_ip4_hook_input; then
    echo "  ip4.o calls lwip_ip4_hook_input: yes"
else
    echo "  ip4.o does NOT call lwip_ip4_hook_input"; exit 1
fi

echo; echo "--- code size (our objects)"
"$SIZE" $(ls *.o | grep -v ip4_with_hook)
echo; echo "--- stack per function (bytes)"
cat $(ls *.su | grep -v "^ip4_with_hook") | sed "s|^$R/||"
