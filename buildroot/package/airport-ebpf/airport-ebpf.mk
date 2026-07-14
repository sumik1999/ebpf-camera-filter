################################################################################
# Airport eBPF filters
################################################################################

AIRPORT_EBPF_VERSION = 1.0.0
AIRPORT_EBPF_SITE = $(TOPDIR)/..
AIRPORT_EBPF_SITE_METHOD = local

# No staging install needed
AIRPORT_EBPF_INSTALL_STAGING = NO
AIRPORT_EBPF_INSTALL_TARGET = YES

# Architecture for eBPF compilation (always target bpf bytecode)
AIRPORT_EBPF_EBPF_ARCH = bpf

# Cross-compile flags for eBPF programs
AIRPORT_EBPF_CFLAGS = -O2 -g -target $(AIRPORT_EBPF_EBPF_ARCH) -D__TARGET_ARCH_arm64 -I$(STAGING_DIR)/usr/include

define AIRPORT_EBPF_BUILD_CMDS
	$(TARGET_CC) $(AIRPORT_EBPF_CFLAGS) \
		$(AIRPORT_EBPF_SRCDIR)/xdp_cms_filter.bpf.c \
		-o $(@D)/xdp_cms_filter.bpf.o 2>/dev/null || \
	$(HOSTCC) $(AIRPORT_EBPF_CFLAGS) \
		$(AIRPORT_EBPF_SRCDIR)/xdp_cms_filter.bpf.c \
		-o $(@D)/xdp_cms_filter.bpf.o 2>/dev/null || true
	$(TARGET_CC) $(AIRPORT_EBPF_CFLAGS) \
		$(AIRPORT_EBPF_SRCDIR)/xdp_elastic_filter.bpf.c \
		-o $(@D)/xdp_elastic_filter.bpf.o 2>/dev/null || \
	$(HOSTCC) $(AIRPORT_EBPF_CFLAGS) \
		$(AIRPORT_EBPF_SRCDIR)/xdp_elastic_filter.bpf.c \
		-o $(@D)/xdp_elastic_filter.bpf.o 2>/dev/null || true
endef

define AIRPORT_EBPF_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/xdp_cms_filter.bpf.o \
		$(TARGET_DIR)/usr/local/sbin/xdp_cms_filter.bpf.o
	$(INSTALL) -D -m 0644 $(@D)/xdp_elastic_filter.bpf.o \
		$(TARGET_DIR)/usr/local/sbin/xdp_elastic_filter.bpf.o
endef

$(eval $(generic-package))
