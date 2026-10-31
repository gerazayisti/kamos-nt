/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Independent walker over the simulated page tables. Owner: A04.
 *
 * Shares no code with ntoskrnl/mm/: it reads entries from the simulated physical memory and
 * judges them with x86_oracle.h (A10, from the Intel SDM bit layout). A dump lists every present
 * leaf and counts the defects: an entry that is not exactly what the layout requires, a table
 * that is empty (invariant I), referenced twice, or not a PageTable frame in the database.
 */
#ifndef KAMOS_TESTS_MM_DUMP_H
#define KAMOS_TESTS_MM_DUMP_H

#include <stdlib.h>
#include <string.h>

#include <x86_oracle.h>

#include "mm_host.h"

#define DUMP_MAX_LEAVES (1u << 15)
#define DUMP_MAX_TABLES 2048u

typedef struct DumpLeaf {
    uint64_t va;
    uint64_t pte;
} DumpLeaf;

typedef struct Dump {
    unsigned n;
    unsigned tables;      /* tables found, root included */
    unsigned kernelPdpts; /* of which PDPTs of the kernel half */
    unsigned defects;
    unsigned firstDefectKind;
    uint64_t seen[DUMP_MAX_TABLES];
    DumpLeaf leaf[DUMP_MAX_LEAVES];
} Dump;

static inline uint64_t *dump_tbl(uint64_t pa)
{
    return g_hw.PhysicalToPointer(NULL, pa, 4096);
}

static inline void dump_defect(Dump *d, unsigned kind)
{
    if (d->defects++ == 0)
        d->firstDefectKind = kind;
}

static inline int dump_empty(const uint64_t *t)
{
    for (unsigned i = 0; i < 512; i++)
        if (t[i] != 0)
            return 0;
    return 1;
}

/* Visits one table; level 4 = PML4. */
static inline void dump_level(const MM_FRAME_DB *db, Dump *d, uint64_t pa, int level,
                              uint64_t vaBase, int kernel, int exemptEmpty)
{
    for (unsigned i = 0; i < d->tables; i++)
        if (d->seen[i] == pa) {
            dump_defect(d, 1); /* referenced twice */
            return;
        }
    if (d->tables >= DUMP_MAX_TABLES) {
        dump_defect(d, 2);
        return;
    }
    d->seen[d->tables++] = pa;
    const MM_PFN *p      = MiGetFrame(db, pa);
    if (p == NULL || p->State != MmFramePageTable || p->RefCount != 1)
        dump_defect(d, 3); /* not a page-table frame */

    const uint64_t *t = dump_tbl(pa);
    if (level < 4 && !exemptEmpty && dump_empty(t))
        dump_defect(d, 4); /* invariant I */

    unsigned from = 0, to = 512;
    if (level == 4)
        to = kernel ? 512 : 256;
    for (unsigned i = from; i < to; i++) {
        uint64_t e = t[i];
        if (e == 0)
            continue;
        uint64_t va = vaBase | ((uint64_t)i << (12 + 9 * (level - 1)));
        if (level == 4 && i >= 256)
            va |= UINT64_C(0xFFFF000000000000);
        int isKernelHalf = level == 4 ? i >= 256 : kernel;
        if (level > 1) {
            uint64_t expect =
                (e & X86O_ADDR_MASK) | X86O_P | X86O_RW | (isKernelHalf ? 0 : X86O_US);
            if (e != expect) {
                dump_defect(d, 5); /* wrong bits (PS, A, XD, no RW...) */
                continue;
            }
            int exempt = (level == 4 && i >= 256);
            if (exempt)
                d->kernelPdpts++;
            dump_level(db, d, e & X86O_ADDR_MASK, level - 1, va, isKernelHalf, exempt);
        } else {
            if (!(e & X86O_P) ||
                (e & ~(X86O_P | X86O_RW | X86O_US | X86O_XD | X86O_ADDR_MASK)) != 0 ||
                ((e & X86O_US) != 0) == isKernelHalf)
                dump_defect(d, 6); /* extra bits, or user bit on the wrong half */
            if (d->n < DUMP_MAX_LEAVES) {
                d->leaf[d->n].va  = va;
                d->leaf[d->n].pte = e;
                d->n++;
            } else {
                dump_defect(d, 7);
            }
        }
    }
}

static inline void dump_space(const MM_FRAME_DB *db, uint64_t root, int kernelHalf, Dump *d)
{
    d->n = d->tables = d->kernelPdpts = d->defects = d->firstDefectKind = 0;
    dump_level(db, d, root, 4, 0, kernelHalf, 0);
    (void)kernelHalf;
}

static inline int dump_same(const Dump *a, const Dump *b)
{
    return a->n == b->n && a->tables == b->tables && a->defects == b->defects &&
           memcmp(a->leaf, b->leaf, a->n * sizeof a->leaf[0]) == 0;
}

/* Finds the leaf entry of va in a dump (0 if none). */
static inline uint64_t dump_find(const Dump *d, uint64_t va)
{
    for (unsigned i = 0; i < d->n; i++)
        if (d->leaf[i].va == va)
            return d->leaf[i].pte;
    return 0;
}

/* Single-address walk with the oracle's indexes, no allocation: leaf entry, 0 if not present. */
static inline uint64_t oracle_leaf(uint64_t root, uint64_t va)
{
    uint64_t t = root;
    for (int level = 4; level >= 1; level--) {
        uint64_t e = dump_tbl(t)[x86o_index(va, level)];
        if (!(e & X86O_P))
            return 0;
        if (level == 1)
            return e;
        t = e & X86O_ADDR_MASK;
    }
    return 0;
}

#endif /* KAMOS_TESTS_MM_DUMP_H */
