# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Host unit tests with ASan and UBSan. Owner: A10. The top-level Makefile (A02) includes this
# file with:  include tests/framework/host.mk
# Layout and rules: docs/testing/oracles-wave1.md, section 1.4.

.PHONY: test-host test-framework-selftest

test-host:
	@tests/framework/host/run-host-tests.sh

# Parser fixtures only (fast, no compiler needed).
test-framework-selftest:
	@python3 tests/framework/ktest-run.py selftest tests/framework/fixtures
