SHELL := /bin/bash

# Airport eBPF Defense – Mac / Lima Makefile
# All targets that need root are prefixed with sudo.
# The eBPF programs now use XDP (primary) with TC fallback.

.PHONY: check setup show cleanup \
        compile compile-cms compile-elastic \
        run run-all stress-matrix summarize \
        visual-demo visual-replay sketch-demo validate

# ── Environment check ────────────────────────────────────────────────────
check:
	@echo "Run inside Lima/OrbStack Linux guest, not on macOS host."
	sudo bash scripts/check_env.sh

# ── Topology ──────────────────────────────────────────────────────────────
setup:
	sudo bash scripts/setup_ns_tree.sh

show:
	bash scripts/show_topology.sh

cleanup:
	sudo bash scripts/cleanup_ns.sh

# ── Compile eBPF objects (optional: attach scripts compile automatically) ─
compile: compile-cms compile-elastic

compile-cms:
	@echo "Compiling XDP CMS filter …"
	$(eval ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/'))
	$(eval TRIPLE := $(shell uname -m | sed 's/x86_64/x86_64-linux-gnu/;s/aarch64/aarch64-linux-gnu/'))
	clang -O2 -g -target bpf \
	    -D__TARGET_ARCH_$(ARCH) \
	    -I/usr/include/$(TRIPLE) -I/usr/include \
	    -Iebpf \
	    -c ebpf/xdp_cms_filter.bpf.c -o ebpf/xdp_cms_filter.bpf.o

compile-elastic:
	@echo "Compiling XDP Elastic filter …"
	$(eval ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/'))
	$(eval TRIPLE := $(shell uname -m | sed 's/x86_64/x86_64-linux-gnu/;s/aarch64/aarch64-linux-gnu/'))
	clang -O2 -g -target bpf \
	    -D__TARGET_ARCH_$(ARCH) \
	    -I/usr/include/$(TRIPLE) -I/usr/include \
	    -Iebpf \
	    -c ebpf/xdp_elastic_filter.bpf.c -o ebpf/xdp_elastic_filter.bpf.o

# ── Single experiment run ─────────────────────────────────────────────────
run:
	sudo bash scripts/run_experiment.sh

# ── Stress matrix (all cases × all PPS levels) ──────────────────────────
stress-matrix:
	sudo bash scripts/run_stress_matrix.sh

# ── Summarise results ─────────────────────────────────────────────────────
summarize:
	python3 eval/summarize_results.py --results-dir eval/results

# ── Visual demo ───────────────────────────────────────────────────────────
visual-demo:
	sudo bash scripts/run_visual_demo.sh

visual-replay:
	python3 eval/visual_attack_sim.py --results-dir eval/results

# ── Sketch collision demo (user-space, no root needed) ────────────────────
sketch-demo:
	python3 eval/sketch_collision_demo.py

# ── Syntax / compile validation ───────────────────────────────────────────
validate:
	bash -n scripts/*.sh filters/*.sh config.env
	python3 -m py_compile traffic/*.py eval/*.py
	@echo "Syntax OK"
