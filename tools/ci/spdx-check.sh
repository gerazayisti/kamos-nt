#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Every tracked source, header, script, linker script and Makefile carries an SPDX line
# (ADR-0004). Markdown and the LICENSE text are not checked. Owner: A02, provenance by A10.
set -euo pipefail

cd "$(dirname "$0")/../.."
missing=0
while IFS= read -r f; do
    if ! grep -q 'SPDX-License-Identifier' "$f"; then
        echo "missing SPDX line: $f"; missing=1
    fi
done < <(git ls-files | grep -E '\.(c|h|S|asm|ld|sh|mk)$|(^|/)Makefile$')
[ "$missing" -eq 0 ] && echo "spdx-check: PASS"
exit $missing
