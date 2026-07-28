SHELL := /bin/bash

# Airport eBPF Defense – Mac / Lima Makefile
# All targets that need root are prefixed with sudo.
# Defended cases use an XDP validation front end followed by TC analytics.

.PHONY: check setup show cleanup \
        compile compile-xdp-frontend compile-cms compile-elastic compile-tc-cms compile-tc-elastic \
        run run-all stress-matrix summarize \
        visual-demo visual-replay sketch-demo validate

# ── Environment check ────────────────────────────────────────────────────
check:
	@echo "Run inside Lima/OrbStack Linux guest, not on macOS host."
	sudo bash scripts/validation/check-env.sh

# ── Topology ──────────────────────────────────────────────────────────────
setup:
	sudo bash scripts/network/setup.sh

show:
	bash scripts/network/show-topology.sh

cleanup:
	sudo bash scripts/network/cleanup.sh

# ── Compile eBPF objects (optional: attach scripts compile automatically) ─
compile: compile-xdp-frontend compile-cms compile-elastic compile-tc-cms compile-tc-elastic

compile-xdp-frontend:
	@echo "Compiling XDP pipeline front end …"
	$(eval ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/'))
	$(eval TRIPLE := $(shell uname -m | sed 's/x86_64/x86_64-linux-gnu/;s/aarch64/aarch64-linux-gnu/'))
	clang -O2 -g -target bpf \
	    -D__TARGET_ARCH_$(ARCH) \
	    -I/usr/include/$(TRIPLE) -I/usr/include \
	    -Iebpf/include -Iebpf/tc \
	    -c ebpf/xdp/frontend.bpf.c -o ebpf/xdp/frontend.bpf.o

compile-cms:
	@echo "Compiling XDP CMS filter …"
	$(eval ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/'))
	$(eval TRIPLE := $(shell uname -m | sed 's/x86_64/x86_64-linux-gnu/;s/aarch64/aarch64-linux-gnu/'))
	clang -O2 -g -target bpf \
	    -D__TARGET_ARCH_$(ARCH) \
	    -I/usr/include/$(TRIPLE) -I/usr/include \
	    -Iebpf/include \
	    -c ebpf/xdp/standalone/cms_filter.bpf.c -o ebpf/xdp/standalone/cms_filter.bpf.o

compile-elastic:
	@echo "Compiling XDP Elastic filter …"
	$(eval ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/'))
	$(eval TRIPLE := $(shell uname -m | sed 's/x86_64/x86_64-linux-gnu/;s/aarch64/aarch64-linux-gnu/'))
	clang -O2 -g -target bpf \
	    -D__TARGET_ARCH_$(ARCH) \
	    -I/usr/include/$(TRIPLE) -I/usr/include \
	    -Iebpf/include \
	    -c ebpf/xdp/standalone/elastic_filter.bpf.c -o ebpf/xdp/standalone/elastic_filter.bpf.o

compile-tc-cms:
	@echo "Compiling TC CMS filter …"
	$(eval ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/'))
	$(eval TRIPLE := $(shell uname -m | sed 's/x86_64/x86_64-linux-gnu/;s/aarch64/aarch64-linux-gnu/'))
	clang -O2 -g -target bpf \
	    -D__TARGET_ARCH_$(ARCH) \
	    -I/usr/include/$(TRIPLE) -I/usr/include \
	    -Iebpf/include -Iebpf/tc \
	    -c ebpf/tc/cms_filter.bpf.c -o ebpf/tc/cms_filter.bpf.o

compile-tc-elastic:
	@echo "Compiling TC Elastic filter …"
	$(eval ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/'))
	$(eval TRIPLE := $(shell uname -m | sed 's/x86_64/x86_64-linux-gnu/;s/aarch64/aarch64-linux-gnu/'))
	clang -O2 -g -target bpf \
	    -D__TARGET_ARCH_$(ARCH) \
	    -I/usr/include/$(TRIPLE) -I/usr/include \
	    -Iebpf/include -Iebpf/tc \
	    -c ebpf/tc/elastic_filter.bpf.c -o ebpf/tc/elastic_filter.bpf.o

# ── Single experiment run ─────────────────────────────────────────────────
run:
	sudo bash scripts/experiments/run.sh

# ── Stress matrix (all cases × all PPS levels) ──────────────────────────
stress-matrix:
	sudo bash scripts/experiments/stress-matrix.sh

# ── Summarise results ─────────────────────────────────────────────────────
summarize:
	python3 eval/summarize_results.py --results-dir eval/results

# ── Visual demo ───────────────────────────────────────────────────────────
visual-demo:
	sudo bash scripts/experiments/visual-demo.sh

visual-replay:
	python3 eval/visual_attack_sim.py --results-dir eval/results

# ── Sketch collision demo (user-space, no root needed) ────────────────────
sketch-demo:
	python3 eval/sketch_collision_demo.py

# ── Syntax / compile validation ───────────────────────────────────────────
validate:
	find scripts policy -type f -name '*.sh' -print0 | xargs -0 bash -n
	bash -n config/defaults.env
	python3 -m py_compile traffic/*.py eval/*.py
	@echo "Syntax OK"
