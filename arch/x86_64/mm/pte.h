/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * x86-64 four-level page-table entry format. Owner: A04.
 * Source: Intel SDM vol. 3A chapter 4 (paging, 4-level format) and AMD APM vol. 2 chapter 5.
 * Written from the layout in docs/coordination/methodology/W1-A04.md, section 2.
 *
 *   bit 0 P, bit 1 R/W, bit 2 U/S, bit 3 PWT, bit 4 PCD, bit 5 A, bit 6 D (leaf only),
 *   bit 7 PS (PDPTE/PDE: large page; PML4E: must be 0) or PAT (PTE), bit 8 G (leaf only),
 *   bits 51:12 physical address, bits 62:59 protection key (leaf), bit 63 XD.
 */
#ifndef KAMOS_ARCH_X86_64_MM_PTE_H
#define KAMOS_ARCH_X86_64_MM_PTE_H

#include <stdint.h>

#define MI_PTE_PRESENT           (UINT64_C(1) << 0)
#define MI_PTE_WRITE             (UINT64_C(1) << 1)
#define MI_PTE_USER              (UINT64_C(1) << 2)
#define MI_PTE_ACCESSED          (UINT64_C(1) << 5)
#define MI_PTE_DIRTY             (UINT64_C(1) << 6)
#define MI_PTE_LARGE             (UINT64_C(1) << 7)
#define MI_PTE_NX                (UINT64_C(1) << 63)
#define MI_PTE_ADDR_MASK         UINT64_C(0x000FFFFFFFFFF000)
#define MI_PTE_LARGE2M_ADDR_MASK UINT64_C(0x000FFFFFFFE00000)
#define MI_PTE_LARGE1G_ADDR_MASK UINT64_C(0x000FFFFFC0000000)

#define MI_ENTRIES_PER_TABLE 512u

/* Index of a virtual address at level 4 (PML4) down to level 1 (PT). */
#define MI_VA_INDEX(Va, Level) ((unsigned)(((Va) >> (12u + 9u * ((Level) - 1u))) & 0x1FFu))

/* A virtual address is canonical (48-bit) when bits 63:47 are all equal. */
#define MI_VA_IS_CANONICAL(Va)                                                                     \
    (((uint64_t)(Va) >> 47) == 0 || ((uint64_t)(Va) >> 47) == UINT64_C(0x1FFFF))

#endif /* KAMOS_ARCH_X86_64_MM_PTE_H */
