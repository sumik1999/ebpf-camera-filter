# Simulated firmware (ST driver model + real lwIP 2.2.1 + our hooks), built
# for the Mac so it can be debugged quickly and run under the sanitizers.
# The same sources are built for the emulated Cortex-M33 by sim/qemu_m33.
#
#   make -C sim/eth_sim            build + run every trace in every config (quick check)
#   make -C sim/eth_sim asan       the same with AddressSanitizer/UBSan

VENDOR  ?= $(HOME)/Projects/vendor/stm32h5-classic-coremw-apps
LWIP    := $(VENDOR)/Middlewares/Third_Party/LwIP/src
R       := ../..
FW      := $(R)/firmware
CC      ?= cc
INC     := -Iport -I. -I$(R)/sim/qemu_m33 -I$(FW)/VM_Engine -I$(FW)/Network_Hooks -I$(FW)/DDoS_Prototype -I$(LWIP)/include
OURFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Werror
LWIPFLAGS := -std=gnu11 -O2 -g -w

OUR_SRCS := sim_main.c sim_common.c sim_app.c ethernetif_sim.c eth_hw_sim.c \
            $(FW)/VM_Engine/vm_verifier.c $(FW)/VM_Engine/vm_interpreter.c $(FW)/VM_Engine/vm_slot.c $(FW)/VM_Engine/vm_loader.c \
            $(FW)/Network_Hooks/xdp_dma_hook.c $(FW)/Network_Hooks/lwip_ip4_hook.c \
            $(FW)/Network_Hooks/lwip_ip4_hook_glue.c \
            $(FW)/DDoS_Prototype/ddos_native.c $(FW)/DDoS_Prototype/ddos_prototype_init.c
LWIP_SRCS := $(wildcard $(LWIP)/core/*.c) $(wildcard $(LWIP)/core/ipv4/*.c) $(LWIP)/netif/ethernet.c

OUT := build
TRACES := $(OUT)/traces

.PHONY: all run asan clean traces
all: run

$(OUT)/lwip.a: $(LWIP_SRCS) port/lwipopts.h port/arch/cc.h
	mkdir -p $(OUT)/lwip
	@for f in $(LWIP_SRCS); do $(CC) $(LWIPFLAGS) $(EXTRA) $(INC) -c $$f -o $(OUT)/lwip/$$(basename $$f .c).o || exit 1; done
	ar rcs $@ $(OUT)/lwip/*.o

$(OUT)/sim: $(OUR_SRCS) $(OUT)/lwip.a *.h port/*.h
	$(CC) $(OURFLAGS) $(EXTRA) $(INC) -o $@ $(OUR_SRCS) $(OUT)/lwip.a

traces:
	python3 $(R)/bench/gen_traces.py $(TRACES) > /dev/null

run: $(OUT)/sim traces
	@for t in legit attack mix90 odd; do for c in 0 1 2 3 4; do \
	  $(OUT)/sim cfg=$$c loop=fixed burst=1 trace=$(TRACES)/$$t.pcap | grep '^RESULT,[0-9]' | sed "s/^RESULT/$$t/"; \
	done; done

asan:
	$(MAKE) clean
	$(MAKE) run EXTRA="-O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" \
	        CC="$(CC)" LDFLAGS="-fsanitize=address,undefined"

clean:
	rm -rf $(OUT)/sim $(OUT)/lwip $(OUT)/lwip.a $(OUT)/*.dSYM
