/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Phase 1 tests: entry state, mappings (W^X), boot information, formatter.
 * Oracles: control registers and page-table bits read from the hardware (Intel SDM vol. 3 ch. 2
 * and 4), not the values the boot code wrote.
 */

#include "ktest_internal.h"

#define KERNEL_VMA 0xFFFFFFFF80000000ull

extern char __kernel_phys_start[];
extern char __kernel_phys_end[];
extern char __text_start[];
extern char __rodata_start[];
extern char __data_start[];

static void BootEntry(void)
{
    KtNoteStr("banner", "ok");
    KTX_CHECK_EQ(KiBootMagic, 0x36D76289u);
}

static void BootCpuState(void)
{
    ULONG64 cr0      = ArchReadCr0();
    ULONG64 cr4      = ArchReadCr4();
    ULONG64 efer     = ArchReadMsr(MSR_EFER);
    ULONG64 want0    = CR0_PE | CR0_PG | CR0_WP;
    ULONG64 wantEfer = EFER_LME | EFER_LMA | EFER_NXE;

    KTX_CHECK_EQ(cr0 & want0, want0);
    KTX_CHECK_EQ(cr4 & CR4_PAE, CR4_PAE);
    KTX_CHECK_EQ(efer & wantEfer, wantEfer);
    KtNoteHex("cr4", cr4);
}

/* Effective rights of a virtual address from a walk of the live tables. */
typedef struct _WALK {
    BOOLEAN Present;
    BOOLEAN Writable;
    BOOLEAN NoExecute;
} WALK;

static const ULONG64 *TablePtr(ULONG64 Phys)
{
    if (Phys >= (ULONG64)(uintptr_t)__kernel_phys_start &&
        Phys < (ULONG64)(uintptr_t)__kernel_phys_end) {
        return (const ULONG64 *)(Phys + KERNEL_VMA); /* the identity alias is gone */
    }
    return (const ULONG64 *)(uintptr_t)Phys;
}

static WALK Walk(ULONG64 Va)
{
    WALK    w     = {false, true, false};
    ULONG64 table = ArchReadCr3() & 0x000FFFFFFFFFF000ull;

    for (int shift = 39; shift >= 12; shift -= 9) {
        ULONG64 e = TablePtr(table)[(Va >> shift) & 511];

        if ((e & 1) == 0) {
            w.Present = false;
            return w;
        }
        w.Writable  = w.Writable && ((e & 2) != 0);
        w.NoExecute = w.NoExecute || ((e >> 63) != 0);
        if ((shift == 30 || shift == 21) && (e & 0x80) != 0) {
            break; /* large page: the leaf */
        }
        table = e & 0x000FFFFFFFFFF000ull;
    }
    w.Present = true;
    return w;
}

static const char     RodataProbe[] = "rodata probe";
static volatile ULONG DataProbe     = 1;

static void MapTextIsRx(void)
{
    WALK w = Walk((ULONG64)(uintptr_t)__text_start);

    KTX_REQUIRE(w.Present);
    KTX_CHECK(!w.Writable);
    KTX_CHECK(!w.NoExecute);
}

static void MapRodataIsRNx(void)
{
    WALK w = Walk((ULONG64)(uintptr_t)RodataProbe);

    KTX_REQUIRE(w.Present);
    KTX_CHECK(!w.Writable);
    KTX_CHECK(w.NoExecute);
}

static void MapDataIsRwNx(void)
{
    WALK w = Walk((ULONG64)(uintptr_t)&DataProbe);

    KTX_REQUIRE(w.Present);
    KTX_CHECK(w.Writable);
    KTX_CHECK(w.NoExecute);
}

static void MapNullGuardAbsent(void)
{
    KTX_CHECK(!Walk(0).Present);
    KTX_CHECK(!Walk(0x8).Present);
    KTX_CHECK(!Walk(0xFFFF).Present);
    KTX_CHECK(Walk(0x10000).Present); /* just above the guard: legacy area */
}

static void MapImageAliasRemoved(void)
{
    KTX_CHECK(!Walk((ULONG64)(uintptr_t)__kernel_phys_start).Present);
    KTX_CHECK(!Walk(((ULONG64)(uintptr_t)__kernel_phys_end) - 8).Present);
}

static void MapIdentityIsNx(void)
{
    WALK w  = Walk(0xB8000);       /* VGA text: low 2 MiB, 4 KiB page */
    WALK hi = Walk(0x40000000ull); /* 1 GiB: a 2 MiB page */

    KTX_REQUIRE(w.Present);
    KTX_CHECK(w.NoExecute);
    KTX_REQUIRE(hi.Present);
    KTX_CHECK(hi.NoExecute);
}

static void BootInfoShape(void)
{
    const BOOT_INFORMATION *bi             = HalGetBootInformation();
    ULONG64                 usable         = 0;
    BOOLEAN                 containsKernel = false;
    ULONG64                 ks             = (ULONG64)(uintptr_t)__kernel_phys_start;
    ULONG64                 ke             = (ULONG64)(uintptr_t)__kernel_phys_end;

    KTX_REQUIRE(bi->RegionCount >= 1 && bi->RegionCount <= BOOT_MAX_MEMORY_REGIONS);
    for (ULONG i = 0; i < bi->RegionCount; i++) {
        const BOOT_MEMORY_REGION *r = &bi->Regions[i];

        KTX_CHECK(r->Length != 0);
        if (i > 0) {
            const BOOT_MEMORY_REGION *p = &bi->Regions[i - 1];
            KTX_CHECK(p->Base + p->Length <= r->Base); /* sorted and disjoint */
        }
        if (r->Type == BootMemoryUsable) {
            usable += r->Length;
            if (r->Base <= ks && ke <= r->Base + r->Length) {
                containsKernel = true;
            }
        }
    }
    KTX_CHECK(usable >= (256ull << 20)); /* QEMU gives 512 MiB */
    KTX_CHECK(containsKernel);
    KtNoteU64("regions", bi->RegionCount);
    KtNoteU64("usable_mib", usable >> 20);
}

static void BootInfoModule(void)
{
    const BOOT_INFORMATION *bi = HalGetBootInformation();

    KTX_REQUIRE(bi->ModuleCount == 1);
    KTX_CHECK(bi->Modules[0].Length > 0);
    KTX_CHECK(bi->Modules[0].Name[0] != '\0');
    KtNoteStr("module", bi->Modules[0].Name);
    KtNoteHex("module_base", bi->Modules[0].Base);
}

static void RtlFormatCases(void)
{
    char   b[64];
    size_t n;

    n = RtlFormat(b, sizeof b, "%d|%u|%x|%X|%#x|%5d|%-5d|%05d", -42, 42u, 0xBEEFu, 0xBEEFu, 0xBEEFu,
                  7, 7, -7);
    KTX_CHECK(memcmp(b, "-42|42|beef|BEEF|0xbeef|    7|7    |-0007", n + 1) == 0);
    n = RtlFormat(b, sizeof b, "%lx %lu %016lx %s %c %%", 0xFFFFFFFF80000000ul,
                  18446744073709551615ul, 0x1234ul, "str", 'z');
    KTX_CHECK(memcmp(b, "ffffffff80000000 18446744073709551615 0000000000001234 str z %", n + 1) ==
              0);
    n = RtlFormat(b, 8, "%s", "truncated");
    KTX_CHECK_EQ(n, 9);
    KTX_CHECK(memcmp(b, "truncat", 8) == 0);
    n = RtlFormat(b, sizeof b, "%p", (void *)0x1000);
    KTX_CHECK(memcmp(b, "0x0000000000001000", n + 1) == 0);
}

const KTX_CASE KtBootCases[] = {
    {"boot", "entry", BootEntry},
    {"boot", "cpu_state", BootCpuState},
    {"boot", "bootinfo_shape", BootInfoShape},
    {"boot", "bootinfo_module", BootInfoModule},
    {"boot", "rtl_format", RtlFormatCases},
    {"map", "text_rx", MapTextIsRx},
    {"map", "rodata_r_nx", MapRodataIsRNx},
    {"map", "data_rw_nx", MapDataIsRwNx},
    {"map", "null_guard_absent", MapNullGuardAbsent},
    {"map", "image_alias_removed", MapImageAliasRemoved},
    {"map", "identity_nx", MapIdentityIsNx},
};
const size_t KtBootCaseCount = KAMOS_ARRAY_SIZE(KtBootCases);
