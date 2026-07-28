################################################################################
# Airport eBPF filters
################################################################################

AIRPORT_EBPF_VERSION = 1.0.0
AIRPORT_EBPF_SITE = $(TOPDIR)/../ebpf
AIRPORT_EBPF_SITE_METHOD = local

# No staging install needed
AIRPORT_EBPF_INSTALL_STAGING = NO
AIRPORT_EBPF_INSTALL_TARGET = YES

# These ELF objects contain Linux BPF bytecode rather than AArch64 machine
# code. Exclude this package's install directory from Buildroot's target ELF
# architecture check; the check otherwise rejects valid BPF objects.
AIRPORT_EBPF_BIN_ARCH_EXCLUDE = /usr/local/sbin/

# Architecture for eBPF compilation (always target bpf bytecode)
AIRPORT_EBPF_EBPF_ARCH = bpf

# eBPF programs require clang/llvm for compilation
AIRPORT_EBPF_DEPENDENCIES = host-clang libbpf

# Compile target-independent BPF bytecode with Buildroot's host clang. libbpf
# installs bpf_helpers.h and related headers into the staging sysroot.
AIRPORT_EBPF_CFLAGS = -O2 -g -target $(AIRPORT_EBPF_EBPF_ARCH) \
	-D__TARGET_ARCH_arm64 -I$(STAGING_DIR)/usr/include \
	-I$(@D)/include -I$(@D)/tc

AIRPORT_EBPF_HOST_CLANG = $(HOST_DIR)/bin/clang

define AIRPORT_EBPF_BUILD_CMDS
	$(AIRPORT_EBPF_HOST_CLANG) $(AIRPORT_EBPF_CFLAGS) -c \
		$(@D)/xdp/frontend.bpf.c \
		-o $(@D)/xdp_frontend.bpf.o
	$(AIRPORT_EBPF_HOST_CLANG) $(AIRPORT_EBPF_CFLAGS) -c \
		$(@D)/xdp/standalone/cms_filter.bpf.c \
		-o $(@D)/xdp_cms_filter.bpf.o
	$(AIRPORT_EBPF_HOST_CLANG) $(AIRPORT_EBPF_CFLAGS) -c \
		$(@D)/xdp/standalone/elastic_filter.bpf.c \
		-o $(@D)/xdp_elastic_filter.bpf.o
	$(AIRPORT_EBPF_HOST_CLANG) $(AIRPORT_EBPF_CFLAGS) -c \
		$(@D)/tc/cms_filter.bpf.c \
		-o $(@D)/tc_cms_filter.bpf.o
	$(AIRPORT_EBPF_HOST_CLANG) $(AIRPORT_EBPF_CFLAGS) -c \
		$(@D)/tc/elastic_filter.bpf.c \
		-o $(@D)/tc_elastic_filter.bpf.o
endef

define AIRPORT_EBPF_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/xdp_frontend.bpf.o \
		$(TARGET_DIR)/usr/local/sbin/xdp_frontend.bpf.o
	$(INSTALL) -D -m 0644 $(@D)/xdp_cms_filter.bpf.o \
		$(TARGET_DIR)/usr/local/sbin/xdp_cms_filter.bpf.o
	$(INSTALL) -D -m 0644 $(@D)/xdp_elastic_filter.bpf.o \
		$(TARGET_DIR)/usr/local/sbin/xdp_elastic_filter.bpf.o
	$(INSTALL) -D -m 0644 $(@D)/tc_cms_filter.bpf.o \
		$(TARGET_DIR)/usr/local/sbin/tc_cms_filter.bpf.o
	$(INSTALL) -D -m 0644 $(@D)/tc_elastic_filter.bpf.o \
		$(TARGET_DIR)/usr/local/sbin/tc_elastic_filter.bpf.o
	$(INSTALL) -D -m 0755 $(TOPDIR)/../scripts/deployment/blocklistctl.py \
		$(TARGET_DIR)/usr/sbin/airport-blocklist
endef

$(eval $(generic-package))
