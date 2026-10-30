/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Memory manager (ADR-0005).
 * Owner: A04. Contract state: provisional until the Wave 2 gate.
 */
#ifndef KAMOS_MM_H
#define KAMOS_MM_H

#include <kamos/status.h>
#include <kamos/types.h>

#define PAGE_SIZE  4096u
#define PAGE_SHIFT 12u

typedef enum _POOL_TYPE {
    NonPagedPool = 0,   /* Usable up to DISPATCH_LEVEL. */
    PagedPool    = 1,   /* PASSIVE_LEVEL only; non-paged until a pager exists. */
} POOL_TYPE;

/* Page protection for MmMapPages / MmProtectPages. W and X are mutually exclusive. */
#define MM_PROT_READ    0x1u
#define MM_PROT_WRITE   0x2u
#define MM_PROT_EXECUTE 0x4u
#define MM_PROT_USER    0x8u

/*
 * Pool allocation. Tag: four ASCII characters identifying the caller, for accounting.
 * Returns NULL on failure. NonPagedPool up to DISPATCH_LEVEL, PagedPool at PASSIVE_LEVEL.
 */
void *MmAllocatePool(POOL_TYPE Type, SIZE_T Size, ULONG Tag);
void  MmFreePool(void *Block, ULONG Tag);

/* Physical frames. Up to DISPATCH_LEVEL. STATUS_NO_MEMORY when exhausted. */
NTSTATUS MmAllocatePhysicalPage(PHYSICAL_ADDRESS *Page);
void     MmFreePhysicalPage(PHYSICAL_ADDRESS Page);

/*
 * Maps Count pages of physical memory at a page-aligned virtual address in the current
 * address space. PASSIVE_LEVEL. Errors: STATUS_INVALID_PARAMETER (misaligned, W|X,
 * non-canonical, kernel/user mismatch), STATUS_OBJECT_NAME_COLLISION (already mapped),
 * STATUS_NO_MEMORY (page tables).
 */
NTSTATUS MmMapPages(void *Virtual, PHYSICAL_ADDRESS Physical, SIZE_T Count, ULONG Prot);
NTSTATUS MmUnmapPages(void *Virtual, SIZE_T Count);
NTSTATUS MmProtectPages(void *Virtual, SIZE_T Count, ULONG Prot);

/* Translation through the direct map. Any level. */
void            *MmPhysicalToVirtual(PHYSICAL_ADDRESS Physical);
PHYSICAL_ADDRESS MmVirtualToPhysical(const void *Virtual);

/*
 * Copies between kernel and user memory with fault recovery. PASSIVE_LEVEL.
 * Returns STATUS_ACCESS_VIOLATION if any user byte is unmapped, not user, or out of range.
 */
NTSTATUS MmCopyFromUser(void *KernelDst, const void *UserSrc, SIZE_T Length);
NTSTATUS MmCopyToUser(void *UserDst, const void *KernelSrc, SIZE_T Length);

/* Kernel stacks with a guard page below. PASSIVE_LEVEL. */
void *MmCreateKernelStack(SIZE_T Size);   /* Returns the stack top, NULL on failure. */
void  MmDeleteKernelStack(void *StackTop, SIZE_T Size);

#endif /* KAMOS_MM_H */
