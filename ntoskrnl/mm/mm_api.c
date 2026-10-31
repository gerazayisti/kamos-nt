/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Public memory manager interface (sdk/include/kamos/mm.h) on a global context. Owner: A04.
 *
 * The kernel sets the context once during initialisation (MiSetGlobalContext). The functions
 * of mm.h that need the fault-fixup of A03 (MmCopyFromUser, MmCopyToUser) and the kernel stacks
 * are not here yet: see docs/architecture/memory.md.
 */
#include "mm_internal.h"

static MM_CONTEXT *MiGlobal;

void MiSetGlobalContext(MM_CONTEXT *Mm)
{
    MiGlobal = Mm;
}

static MM_CONTEXT *MiMm(void)
{
    if (MiGlobal == NULL)
        MI_STOP(MM_STOP_SPACE_BAD, 0, 0, 0);
    return MiGlobal;
}

static ADDRESS_SPACE *MiCurrentSpace(void)
{
    MM_CONTEXT *mm = MiMm();
    if (mm->Current == NULL)
        MI_STOP(MM_STOP_SPACE_BAD, 0, 1, 0);
    return mm->Current;
}

void *MmAllocatePool(POOL_TYPE Type, SIZE_T Size, ULONG Tag)
{
    return MiPoolAllocate(&MiMm()->Pool, Type, Size, Tag);
}

void MmFreePool(void *Block, ULONG Tag)
{
    MiPoolFree(&MiMm()->Pool, Block, Tag);
}

NTSTATUS MmAllocatePhysicalPage(PHYSICAL_ADDRESS *Page)
{
    if (Page == NULL)
        return STATUS_INVALID_PARAMETER;
    return MiAllocateFrame(MiMm()->Frames, MmFrameActive, true, Page);
}

void MmFreePhysicalPage(PHYSICAL_ADDRESS Page)
{
    (void)MiDereferenceFrame(MiMm()->Frames, Page, MmFrameActive);
}

ADDRESS_SPACE *MmGetKernelAddressSpace(void)
{
    return &MiMm()->KernelSpace;
}

ADDRESS_SPACE *MmGetCurrentAddressSpace(void)
{
    return MiCurrentSpace();
}

NTSTATUS MmCreateAddressSpace(ADDRESS_SPACE **Space)
{
    return MiCreateAddressSpace(MiMm(), Space);
}

void MmDestroyAddressSpace(ADDRESS_SPACE *Space)
{
    MiDestroyAddressSpace(Space);
}

void MmSwitchAddressSpace(ADDRESS_SPACE *Space)
{
    MiSwitchAddressSpace(Space);
}

NTSTATUS MmMapPagesInSpace(ADDRESS_SPACE *Space, void *Virtual, PHYSICAL_ADDRESS Physical,
                           SIZE_T Count, ULONG Prot)
{
    return MiMapPages(Space, (ULONG_PTR)Virtual, Physical, Count, Prot);
}

NTSTATUS MmUnmapPagesInSpace(ADDRESS_SPACE *Space, void *Virtual, SIZE_T Count)
{
    return MiUnmapPages(Space, (ULONG_PTR)Virtual, Count);
}

NTSTATUS MmMapPages(void *Virtual, PHYSICAL_ADDRESS Physical, SIZE_T Count, ULONG Prot)
{
    return MiMapPages(MiCurrentSpace(), (ULONG_PTR)Virtual, Physical, Count, Prot);
}

NTSTATUS MmUnmapPages(void *Virtual, SIZE_T Count)
{
    return MiUnmapPages(MiCurrentSpace(), (ULONG_PTR)Virtual, Count);
}

NTSTATUS MmProtectPages(void *Virtual, SIZE_T Count, ULONG Prot)
{
    return MiProtectPages(MiCurrentSpace(), (ULONG_PTR)Virtual, Count, Prot);
}

/* Returns NULL beyond the 8 TiB of the direct map. */
void *MmPhysicalToVirtual(PHYSICAL_ADDRESS Physical)
{
    if (Physical > MI_DIRECT_MAP_LAST - MI_DIRECT_MAP_BASE)
        return NULL;
    return (void *)(ULONG_PTR)(MI_DIRECT_MAP_BASE + Physical);
}

/* Returns MI_INVALID_PHYSICAL (all ones) when the address is not mapped (defect D10). */
PHYSICAL_ADDRESS MmVirtualToPhysical(const void *Virtual)
{
    ULONG_PTR va = (ULONG_PTR)Virtual;
    if (MiIsDirectMap(va))
        return va - MI_DIRECT_MAP_BASE;
    PHYSICAL_ADDRESS pa;
    if (!NT_SUCCESS(MiTranslate(MiCurrentSpace(), va, &pa, NULL)))
        return MI_INVALID_PHYSICAL;
    return pa;
}
