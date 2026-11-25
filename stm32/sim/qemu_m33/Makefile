# Run our firmware C code on an emulated Cortex-M33 (QEMU mps2-an505).
#
#   make -C sim/qemu_m33 test    the whole host test suite, on the M33
#   make -C sim/qemu_m33 bench   instructions per packet for each filter path
#
# Uses the same compiler and CPU flags as tools/xc_check.sh. OPT can be
# overridden, e.g. `make bench OPT=-Os` (ST's Release builds use -Os).

ARM_BIN ?= /Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin
CC      := $(ARM_BIN)/arm-none-eabi-gcc
SIZE    := $(ARM_BIN)/arm-none-eabi-size
QEMU    ?= qemu-system-arm
OPT     ?= -O2

R       := ../..
FW      := $(R)/firmware
CPU     := -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
CFLAGS  := -std=gnu11 $(CPU) $(OPT) -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
           -I. -I$(FW)/VM_Engine -I$(FW)/Network_Hooks -I$(FW)/DDoS_Prototype -I$(R)/tests/host
LDFLAGS := $(CPU) -T an505.ld -nostartfiles --specs=nano.specs --specs=rdimon.specs \
           -Wl,--gc-sections -Wl,--no-warn-rwx-segments
QEMUFLAGS := -machine mps2-an505 -nographic -semihosting -no-reboot

FW_SRCS := $(FW)/VM_Engine/vm_verifier.c $(FW)/VM_Engine/vm_interpreter.c $(FW)/VM_Engine/vm_slot.c $(FW)/VM_Engine/vm_loader.c \
           $(FW)/Network_Hooks/xdp_dma_hook.c $(FW)/Network_Hooks/lwip_ip4_hook.c \
           $(FW)/DDoS_Prototype/ddos_native.c $(FW)/DDoS_Prototype/ddos_prototype_init.c
SIM_SRCS := startup.c sim_mark.c
TEST_SRCS := $(R)/tests/host/test_vm.c $(R)/tests/host/test_hooks.c $(R)/tests/host/test_native.c $(R)/tests/host/test_loader.c
OUT     := build

.PHONY: all test bench record clean simrun rtosrun
all: test

$(OUT):
	mkdir -p $(OUT)

$(OUT)/tests.elf: $(TEST_SRCS) $(FW_SRCS) $(SIM_SRCS) an505.ld | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-Map=$(OUT)/tests.map -o $@ $(TEST_SRCS) $(FW_SRCS) $(SIM_SRCS)

$(OUT)/bench.elf: bench_insns.c $(FW_SRCS) $(SIM_SRCS) an505.ld | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-Map=$(OUT)/bench.map -o $@ bench_insns.c $(FW_SRCS) $(SIM_SRCS)

# Exit code of QEMU = exit code of the test suite (0 = all passed).
test: $(OUT)/tests.elf
	$(QEMU) $(QEMUFLAGS) -kernel $<

# One instruction per translation block + an exec trace = one log line per
# executed instruction. count_insns.py turns the trace into a CSV.
bench: $(OUT)/bench.elf
	$(QEMU) $(QEMUFLAGS) -accel tcg,one-insn-per-tb=on -d exec,nochain -D $(OUT)/bench.trace \
	        -kernel $< > $(OUT)/bench.stdout
	python3 count_insns.py $< $(OUT)/bench.trace $(OUT)/bench.stdout "$(OPT)" > $(OUT)/bench.csv
	cat $(OUT)/bench.csv

clean:
	rm -rf $(OUT)

# Save a bench run in eval/raw/<date>_<commit>_qemu_insns<OPT>/ with the
# tool versions and the command used. Commit the code first, so the commit
# hash identifies exactly what was measured (the CSV says "-dirty" otherwise).
record: bench
	d=$(R)/eval/raw/$$(date +%F)_$$(git rev-parse --short HEAD)_qemu_insns$(OPT); \
	mkdir -p $$d && cp $(OUT)/bench.csv $(OUT)/bench.stdout $$d/ && \
	{ echo "date: $$(date '+%F %T %Z')"; echo "commit: $$(git rev-parse HEAD)"; \
	  echo "dirty: $$(git status --porcelain --untracked-files=no | wc -l | tr -d ' ') tracked files changed"; \
	  $(QEMU) --version | head -1; $(CC) --version | head -1; echo "OPT=$(OPT)"; \
	  echo "machine: mps2-an505 (Cortex-M33), -accel tcg,one-insn-per-tb=on -d exec,nochain"; \
	  echo "command: make -C sim/qemu_m33 record OPT=$(OPT)"; } > $$d/meta.txt && \
	echo "saved to $$d"

# ---------------------------------------------------------------------------
# Simulated firmware (sim/eth_sim: ST driver model + lwIP 2.2.1 + our hooks)
# on the emulated Cortex-M33.
#   make simrun ARGS="cfg=4 loop=fixed burst=1 trace=../eth_sim/build/traces/mix90.pcap"
# Trace paths are relative to this directory (QEMU opens them via semihosting).
VENDOR  ?= $(HOME)/Projects/vendor/stm32h5-classic-coremw-apps
LWIP    := $(VENDOR)/Middlewares/Third_Party/LwIP/src
SIM     := ../eth_sim
SIMINC  := -I$(SIM)/port -I$(SIM) -I$(LWIP)/include -I$(FW)/VM_Engine -I$(FW)/Network_Hooks -I$(FW)/DDoS_Prototype -I.
LWIP_SRCS := $(wildcard $(LWIP)/core/*.c) $(wildcard $(LWIP)/core/ipv4/*.c) $(LWIP)/netif/ethernet.c
LWIP_OBJS := $(patsubst %.c,$(OUT)/lwip_arm/%.o,$(notdir $(LWIP_SRCS)))
SIMFW_SRCS := $(SIM)/sim_main.c $(SIM)/sim_common.c $(SIM)/sim_app.c $(SIM)/ethernetif_sim.c $(SIM)/eth_hw_sim.c \
              $(FW_SRCS) $(FW)/Network_Hooks/lwip_ip4_hook_glue.c $(SIM_SRCS)
ARGS    ?= cfg=4 loop=fixed burst=1 trace=../eth_sim/build/traces/mix90.pcap
comma   := ,
empty   :=
space   := $(empty) $(empty)
vpath %.c $(LWIP)/core $(LWIP)/core/ipv4 $(LWIP)/netif

# lwIP is vendor code: built without -Werror (its warnings are not ours to fix).
$(OUT)/lwip_arm/%.o: %.c $(SIM)/port/lwipopts.h $(SIM)/port/arch/cc.h
	@mkdir -p $(OUT)/lwip_arm
	$(CC) -std=gnu11 $(CPU) $(OPT) -g -w -ffunction-sections -fdata-sections $(SIMINC) -c $< -o $@

$(OUT)/lwip_arm.a: $(LWIP_OBJS)
	$(ARM_BIN)/arm-none-eabi-ar rcs $@ $^

$(OUT)/simfw.elf: $(SIMFW_SRCS) $(OUT)/lwip_arm.a $(wildcard $(SIM)/*.h) an505.ld | $(OUT)
	$(CC) $(CFLAGS) -DSIM_QEMU $(SIMINC) $(LDFLAGS) -Wl,-Map=$(OUT)/simfw.map -o $@ $(SIMFW_SRCS) $(OUT)/lwip_arm.a

simrun: $(OUT)/simfw.elf
	$(QEMU) -machine mps2-an505 -nographic -no-reboot \
	  -semihosting-config enable=on,target=native,arg=$(subst $(space),$(comma)arg=,$(strip sim $(ARGS))) -kernel $<

# ---------------------------------------------------------------------------
# The same simulated firmware with FreeRTOS 11.2.0 (plan step 9c).
#   make rtosrun ARGS="cfg=4 loop=fixed burst=1 trace=../eth_sim/build/traces/mix90.pcap"
# -icount ties QEMU's clock (and so SysTick) to the instruction count:
# every run is identical.
FRT     := $(VENDOR)/Middlewares/Third_Party/FreeRTOS/Source
FRT_PORT := $(FRT)/portable/GCC/ARM_CM33_NTZ/non_secure
LWIP_FRT := $(VENDOR)/Middlewares/Third_Party/LwIP/contrib/ports/freertos
RTOSINC := -DSIM_RTOS=1 -I../rtos -I$(FRT)/include -I$(FRT_PORT) -I$(LWIP_FRT)/include $(SIMINC)
FRT_SRCS := $(FRT)/tasks.c $(FRT)/queue.c $(FRT)/list.c $(FRT)/timers.c $(FRT)/event_groups.c \
            $(FRT)/stream_buffer.c $(FRT_PORT)/port.c $(FRT_PORT)/portasm.c $(FRT)/portable/MemMang/heap_4.c
RTOS_VENDOR_SRCS := $(FRT_SRCS) $(LWIP_SRCS) $(LWIP)/api/tcpip.c $(LWIP_FRT)/sys_arch.c
RTOS_VENDOR_OBJS := $(patsubst %.c,$(OUT)/rtos_vendor/%.o,$(notdir $(RTOS_VENDOR_SRCS)))
SIMRTOS_SRCS := ../rtos/rtos_main.c $(SIM)/sim_common.c $(SIM)/sim_app.c $(SIM)/ethernetif_sim.c \
                $(SIM)/eth_hw_sim.c $(FW_SRCS) $(FW)/Network_Hooks/lwip_ip4_hook_glue.c $(SIM_SRCS)
ICOUNT  ?= -icount shift=0
vpath %.c $(FRT) $(FRT_PORT) $(FRT)/portable/MemMang $(LWIP_FRT) $(LWIP)/api

# FreeRTOS and lwIP are vendor code: built without -Werror, not edited.
$(OUT)/rtos_vendor/%.o: %.c ../rtos/FreeRTOSConfig.h $(SIM)/port/lwipopts.h $(SIM)/port/arch/cc.h
	@mkdir -p $(OUT)/rtos_vendor
	$(CC) -std=gnu11 $(CPU) $(OPT) -g -w -ffunction-sections -fdata-sections $(RTOSINC) -c $< -o $@

$(OUT)/rtos_vendor.a: $(RTOS_VENDOR_OBJS)
	$(ARM_BIN)/arm-none-eabi-ar rcs $@ $^

$(OUT)/simrtos.elf: $(SIMRTOS_SRCS) $(OUT)/rtos_vendor.a $(wildcard $(SIM)/*.h) ../rtos/FreeRTOSConfig.h an505.ld | $(OUT)
	$(CC) $(CFLAGS) -DSIM_QEMU $(RTOSINC) $(LDFLAGS) -Wl,-Map=$(OUT)/simrtos.map -o $@ $(SIMRTOS_SRCS) $(OUT)/rtos_vendor.a

rtosrun: $(OUT)/simrtos.elf
	$(QEMU) -machine mps2-an505 -nographic -no-reboot $(ICOUNT) \
	  -semihosting-config enable=on,target=native,arg=$(subst $(space),$(comma)arg=,$(strip sim $(ARGS))) -kernel $<
