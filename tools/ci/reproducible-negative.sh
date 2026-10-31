#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Negative test of reproducible.sh (BOARD D14): with the EFI platform installed and the ISO built
# for every platform (GRUB_DIR empty), the two ISOs differ, and reproducible.sh must say so. This
# script passes only when reproducible.sh fails. Without /usr/lib/grub/x86_64-efi the condition
# cannot arise and the script reports SKIP (exit 0), which is not a pass. Owner: A02.
set -uo pipefail

cd "$(dirname "$0")/../.."
if [ ! -d /usr/lib/grub/x86_64-efi ]; then
    echo "reproducible-negative: SKIP (no /usr/lib/grub/x86_64-efi on this host)"; exit 0
fi
if KAMOS_MAKE_ARGS="GRUB_DIR=" tools/ci/reproducible.sh >/tmp/repro-neg.$$ 2>&1; then
    cat /tmp/repro-neg.$$; rm -f /tmp/repro-neg.$$
    echo "reproducible-negative: FAIL (reproducible.sh accepted an EFI-enabled ISO)"; exit 1
fi
grep -E 'FAIL|differing bytes' /tmp/repro-neg.$$; rm -f /tmp/repro-neg.$$
echo "reproducible-negative: PASS (reproducible.sh rejected the all-platform ISO)"
