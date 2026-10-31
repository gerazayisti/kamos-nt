/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * One translation unit with the memory manager sources and the host support, so that the
 * harness links a single file per test (the .srcs lists) and the sources are built with the
 * debug profile (ASSERT active) whatever flags the harness uses.
 */
#define KAMOS_DEBUG 1

#include "../../../ntoskrnl/mm/mm_api.c"
#include "../../../ntoskrnl/mm/mm_frame.c"
#include "../../../ntoskrnl/mm/mm_pagetable.c"
#include "../../../ntoskrnl/mm/mm_pool.c"
#include "../../../ntoskrnl/mm/mm_space.c"
#include "mm_host.c"
