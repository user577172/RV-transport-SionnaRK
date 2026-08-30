##
## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0
##

CPU_ONLY ?= 1
export SIONNA_RK_CPU_ONLY := $(CPU_ONLY)

.PHONY: doc prepare-system sionna-rk build-gnb

prepare-system:
	./scripts/configure-system.sh
	@if [ "$(CPU_ONLY)" != "1" ]; then \
		./scripts/build-custom-kernel.sh; \
		./scripts/install-custom-kernel.sh; \
		echo "Reboot to load the new kernel and continue the installation."; \
	else \
		echo "CPU-only mode: skipping NVIDIA L4T custom kernel steps."; \
	fi

sionna-rk:
	./scripts/quickstart-oai.sh
	./scripts/generate-configs.sh
	./plugins/common/build_all_plugins.sh --host

build-gnb:
	./scripts/build-oai-native.sh ext/openairinterface5g

doc: FORCE
	cd doc && ./build_docs.sh

test:
	./plugins/common/build_all_plugins.sh --host
	./plugins/testing/run_all_tests.sh --host

FORCE:
