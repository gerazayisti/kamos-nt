/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Independent expectations for x86-64 4-level page-table entries. Owner: A10.
 * Written from the bit layout of the Intel SDM vol. 3 (paging chapter, "Format of a PTE"):
 * bit 0 P, bit 1 R/W, bit 2 U/S, bit 63 XD (NX), physical address in bits 12 to 51.
 * Deliberately shares nothing with the memory manager's own macros: a test calls
 * x86o_pte(...) and compares it with the entry read back from the simulated memory.
 * Status: Designed. Its constants have not been compared with the SDM text yet (see
 * docs/testing/evidence.md when the comparison is recorded).
 */
#ifndef KAMOS_X86_ORACLE_H
#define KAMOS_X86_ORACLE_H

#include <stdint.h>

#define X86O_P   (UINT64_C(1) << 0)
#define X86O_RW  (UINT64_C(1) << 1)
#define X86O_US  (UINT64_C(1) << 2)
#define X86O_XD  (UINT64_C(1) << 63)
#define X86O_ADDR_MASK UINT64_C(0x000FFFFFFFFFF000)

/* Expected leaf entry for a 4 KiB page. */
static inline uint64_t x86o_pte(uint64_t phys, int write, int exec, int user)
{
    return (phys & X86O_ADDR_MASK) | X86O_P | (write ? X86O_RW : 0) | (user ? X86O_US : 0) |
           (exec ? 0 : X86O_XD);
}

/* A virtual address is canonical in 48-bit mode when bits 63..47 are all equal. */
static inline int x86o_canonical48(uint64_t va)
{
    uint64_t top = va >> 47;
    return top == 0 || top == UINT64_C(0x1FFFF);
}

/* Index of a virtual address at paging level 4 (PML4) down to 1 (PT). */
static inline unsigned x86o_index(uint64_t va, int level)
{
    return (unsigned)((va >> (12 + 9 * (level - 1))) & 0x1FF);
}

#endif /* KAMOS_X86_ORACLE_H */
