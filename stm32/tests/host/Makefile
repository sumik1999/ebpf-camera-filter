# Host-side unit tests for the VM engine (no STM32 hardware needed).
# Usage:  make -C tests/host        -> builds and runs
#         make -C tests/host asan   -> same, with AddressSanitizer/UBSan

FW      := ../../firmware
CC      ?= gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -Werror -O2 -pthread \
           -I$(FW)/VM_Engine -I$(FW)/DDoS_Prototype -I$(FW)/Network_Hooks \
           -I$(FW)/Filter
SRCS    := test_vm.c test_hooks.c test_native.c test_loader.c test_packet_parser.c \
           test_ingress_filter.c test_dynamic_filter.c test_filter_concurrency.c \
           $(FW)/VM_Engine/vm_verifier.c $(FW)/VM_Engine/vm_interpreter.c \
           $(FW)/VM_Engine/vm_slot.c $(FW)/VM_Engine/vm_loader.c \
           $(FW)/Filter/packet_parser.c $(FW)/Filter/vm_meta_abi.c \
           $(FW)/Filter/filter_policy.c $(FW)/Filter/ingress_filter.c \
           $(FW)/Filter/rate_limit.c $(FW)/Filter/filter_crypto.c \
           $(FW)/Filter/filter_manager.c $(FW)/Filter/filter_management_task.c \
           $(FW)/Filter/filter_freertos.c \
           $(FW)/Network_Hooks/xdp_dma_hook.c $(FW)/Network_Hooks/lwip_ip4_hook.c \
           $(FW)/DDoS_Prototype/ddos_native.c $(FW)/DDoS_Prototype/ddos_prototype_init.c

.PHONY: all run asan tsan clean
all: run

test_vm: $(SRCS) test_common.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

run: test_vm
	./test_vm

# -fno-sanitize-recover: any sanitizer finding (e.g. undefined behaviour) aborts with a
# non-zero exit, so it fails the run instead of only printing a warning.
asan: $(SRCS)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -o test_vm_asan $(SRCS)
	./test_vm_asan

tsan: $(SRCS)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=thread -fno-omit-frame-pointer -o test_vm_tsan $(SRCS)
	./test_vm_tsan

clean:
	rm -f test_vm test_vm_asan test_vm_tsan
