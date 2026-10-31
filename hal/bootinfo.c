/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Multiboot2 boot information parser (ADR-0001, boot.h, threat model B1).
 *
 * Pure: depends only on <kamos/boot.h> and the C headers, so tests/hal/host builds it with the
 * host compiler and feeds it hostile structures. Every read is bounded by the total size, which
 * is itself bounded by the caller. Source: Multiboot2 specification 2.0, section 3.6.
 */

#include <kamos/boot.h>

#include "halp_bootinfo.h"

#define MB2_TAG_END         0u
#define MB2_TAG_MODULE      3u
#define MB2_TAG_MMAP        6u
#define MB2_TAG_FRAMEBUFFER 8u
#define MB2_TAG_ACPI_OLD    14u
#define MB2_TAG_ACPI_NEW    15u

static uint32_t Rd32(const uint8_t *P)
{
    return (uint32_t)P[0] | ((uint32_t)P[1] << 8) | ((uint32_t)P[2] << 16) | ((uint32_t)P[3] << 24);
}

static uint64_t Rd64(const uint8_t *P)
{
    return (uint64_t)Rd32(P) | ((uint64_t)Rd32(P + 4) << 32);
}

static BOOT_MEMORY_TYPE MapType(uint32_t Raw)
{
    switch (Raw) {
    case 1:
        return BootMemoryUsable;
    case 3:
        return BootMemoryAcpiReclaimable;
    case 4:
        return BootMemoryAcpiNvs;
    case 5:
        return BootMemoryBad;
    default:
        return BootMemoryReserved;
    }
}

/* Inserts a region keeping the array sorted, disjoint and merged (same type). */
static HALP_BOOTINFO_RESULT AddRegion(BOOT_INFORMATION *Out, uint64_t Base, uint64_t Length,
                                      BOOT_MEMORY_TYPE Type)
{
    uint64_t last;
    ULONG    i;
    ULONG    pos;

    if (Length == 0) {
        return BootInfoOk;
    }
    if (Length - 1 > UINT64_MAX - Base) {
        return BootInfoWrap;
    }
    last = Base + (Length - 1);

    i = 0;
    while (i < Out->RegionCount) {
        BOOT_MEMORY_REGION *e        = &Out->Regions[i];
        uint64_t            eLast    = e->Base + (e->Length - 1);
        int                 overlaps = (e->Base <= last) && (Base <= eLast);
        int                 touches  = (eLast != UINT64_MAX && eLast + 1 == Base) ||
                      (last != UINT64_MAX && last + 1 == e->Base);

        if (overlaps && e->Type != Type) {
            return BootInfoOverlap;
        }
        if ((overlaps || touches) && e->Type == Type) {
            if (e->Base < Base) {
                Base = e->Base;
            }
            if (eLast > last) {
                last = eLast;
            }
            for (ULONG j = i + 1; j < Out->RegionCount; j++) {
                Out->Regions[j - 1] = Out->Regions[j];
            }
            Out->RegionCount--;
            continue; /* the slot i now holds the next entry */
        }
        i++;
    }

    if (Out->RegionCount >= BOOT_MAX_MEMORY_REGIONS) {
        return BootInfoTooManyRegions;
    }
    pos = 0;
    while (pos < Out->RegionCount && Out->Regions[pos].Base < Base) {
        pos++;
    }
    for (ULONG j = Out->RegionCount; j > pos; j--) {
        Out->Regions[j] = Out->Regions[j - 1];
    }
    Out->Regions[pos].Base   = Base;
    Out->Regions[pos].Length = (last - Base) + 1; /* 0 only for the whole 2^64 range */
    Out->Regions[pos].Type   = Type;
    Out->RegionCount++;
    return BootInfoOk;
}

static int Checksum(const uint8_t *P, uint32_t Length)
{
    uint8_t sum = 0;

    for (uint32_t i = 0; i < Length; i++) {
        sum = (uint8_t)(sum + P[i]);
    }
    return sum == 0;
}

static HALP_BOOTINFO_RESULT ParseModule(const uint8_t *Tag, uint32_t Size, BOOT_INFORMATION *Out)
{
    uint32_t     start;
    uint32_t     end;
    uint32_t     n;
    BOOT_MODULE *m;

    if (Size < 17) {
        return BootInfoBadModule;
    }
    if (Out->ModuleCount >= BOOT_MAX_MODULES) {
        return BootInfoTooManyModules;
    }
    start = Rd32(Tag + 8);
    end   = Rd32(Tag + 12);
    if (end <= start) {
        return BootInfoBadModule;
    }
    /* The command line must end with NUL inside the tag and fit the name field. */
    for (n = 0; n < Size - 16; n++) {
        if (Tag[16 + n] == '\0') {
            break;
        }
    }
    if (n == Size - 16 || n >= BOOT_MODULE_NAME_MAX) {
        return BootInfoBadModule;
    }
    m         = &Out->Modules[Out->ModuleCount];
    m->Base   = start;
    m->Length = (uint64_t)end - start;
    for (uint32_t i = 0; i <= n; i++) {
        m->Name[i] = (char)Tag[16 + i];
    }
    for (uint32_t i = n + 1; i < BOOT_MODULE_NAME_MAX; i++) {
        m->Name[i] = '\0';
    }
    Out->ModuleCount++;
    return BootInfoOk;
}

static HALP_BOOTINFO_RESULT ParseMemoryMap(const uint8_t *Tag, uint32_t Size, BOOT_INFORMATION *Out)
{
    uint32_t entrySize;
    uint32_t count;

    if (Size < 16) {
        return BootInfoBadMemoryMap;
    }
    entrySize = Rd32(Tag + 8);
    if (Rd32(Tag + 12) != 0 || entrySize < 24 || (entrySize % 8) != 0 ||
        ((Size - 16) % entrySize) != 0) {
        return BootInfoBadMemoryMap;
    }
    count = (Size - 16) / entrySize;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t       *e = Tag + 16 + (uint64_t)i * entrySize;
        HALP_BOOTINFO_RESULT r = AddRegion(Out, Rd64(e), Rd64(e + 8), MapType(Rd32(e + 16)));
        if (r != BootInfoOk) {
            return r;
        }
    }
    return BootInfoOk;
}

static HALP_BOOTINFO_RESULT ParseFramebuffer(const uint8_t *Tag, uint32_t Size,
                                             BOOT_INFORMATION *Out)
{
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  type;

    if (Size < 32) {
        return BootInfoBadFramebuffer;
    }
    addr   = Rd64(Tag + 8);
    pitch  = Rd32(Tag + 16);
    width  = Rd32(Tag + 20);
    height = Rd32(Tag + 24);
    bpp    = Tag[28];
    type   = Tag[29];
    if (type == 2) { /* EGA text: no pixel framebuffer */
        return BootInfoOk;
    }
    if (type > 1 || bpp == 0 || bpp > 64 || width == 0 || height == 0 ||
        (uint64_t)pitch < (((uint64_t)width * bpp) + 7) / 8) {
        return BootInfoBadFramebuffer;
    }
    Out->Framebuffer.Present      = true;
    Out->Framebuffer.BitsPerPixel = bpp;
    Out->Framebuffer.Width        = width;
    Out->Framebuffer.Height       = height;
    Out->Framebuffer.Pitch        = pitch;
    Out->Framebuffer.Base         = addr;
    return BootInfoOk;
}

static HALP_BOOTINFO_RESULT ParseRsdp(const uint8_t *Tag, uint32_t Size, uint64_t TagPhysical,
                                      BOOT_INFORMATION *Out)
{
    const uint8_t *r     = Tag + 8;
    uint32_t       avail = Size - 8;

    if (avail < 20 || r[0] != 'R' || r[1] != 'S' || r[2] != 'D' || r[3] != ' ' || r[4] != 'P' ||
        r[5] != 'T' || r[6] != 'R' || r[7] != ' ' || !Checksum(r, 20)) {
        return BootInfoBadRsdp;
    }
    if (r[15] >= 2) { /* revision 2: extended checksum over Length bytes */
        uint32_t len;
        if (avail < 36) {
            return BootInfoBadRsdp;
        }
        len = Rd32(r + 20);
        if (len < 36 || len > avail || !Checksum(r, len)) {
            return BootInfoBadRsdp;
        }
    }
    Out->AcpiRsdp = TagPhysical + 8; /* the loader's copy; the HAL swaps it for the original */
    return BootInfoOk;
}

HALP_BOOTINFO_RESULT HalpParseMultiboot2(const void *Mbi, ULONG64 MbiPhysical,
                                         ULONG64 AvailableBytes, ULONG64 KernelStart,
                                         ULONG64 KernelEnd, BOOT_INFORMATION *Out)
{
    const uint8_t    *base = Mbi;
    uint64_t          total;
    uint64_t          off;
    int               sawEnd = 0;
    int               sawMap = 0;
    BOOT_INFORMATION *o      = Out;

    for (size_t i = 0; i < sizeof *o; i++) {
        ((uint8_t *)o)[i] = 0;
    }
    if (Mbi == NULL || (MbiPhysical & 7u) != 0 || AvailableBytes < 16) {
        return BootInfoBadPointer;
    }
    total = Rd32(base);
    if (total < 16 || (total & 7u) != 0 || total > AvailableBytes ||
        total > HALP_BOOTINFO_MAX_SIZE) {
        return BootInfoBadSize;
    }

    off = 8;
    while (off + 8 <= total) {
        const uint8_t       *tag  = base + off;
        uint32_t             type = Rd32(tag);
        uint32_t             size = Rd32(tag + 4);
        HALP_BOOTINFO_RESULT r    = BootInfoOk;

        if (size < 8 || size > total - off) {
            return BootInfoBadTag;
        }
        if (type == MB2_TAG_END) {
            if (size != 8) {
                return BootInfoBadTag;
            }
            sawEnd = 1;
            break;
        }
        switch (type) {
        case MB2_TAG_MODULE:
            r = ParseModule(tag, size, o);
            break;
        case MB2_TAG_MMAP:
            if (sawMap) {
                r = BootInfoBadMemoryMap;
            } else {
                sawMap = 1;
                r      = ParseMemoryMap(tag, size, o);
            }
            break;
        case MB2_TAG_FRAMEBUFFER:
            r = ParseFramebuffer(tag, size, o);
            break;
        case MB2_TAG_ACPI_OLD:
        case MB2_TAG_ACPI_NEW:
            r = ParseRsdp(tag, size, MbiPhysical + off, o);
            break;
        default:
            break; /* command line, memory sizes, boot device, ...: not used */
        }
        if (r != BootInfoOk) {
            return r;
        }
        off += ((uint64_t)size + 7u) & ~(uint64_t)7u;
    }
    if (!sawEnd) {
        return BootInfoNoEndTag;
    }

    {
        int usable = 0;
        for (ULONG i = 0; i < o->RegionCount; i++) {
            if (o->Regions[i].Type == BootMemoryUsable) {
                usable = 1;
            }
        }
        if (!usable) {
            return BootInfoNoUsableMemory;
        }
    }

    /* A module must lie inside usable memory and must not overlap the kernel image. */
    for (ULONG m = 0; m < o->ModuleCount; m++) {
        uint64_t mb     = o->Modules[m].Base;
        uint64_t ml     = mb + o->Modules[m].Length - 1;
        int      inside = 0;

        for (ULONG i = 0; i < o->RegionCount; i++) {
            const BOOT_MEMORY_REGION *g = &o->Regions[i];
            if (g->Type == BootMemoryUsable && mb >= g->Base && ml <= g->Base + (g->Length - 1)) {
                inside = 1;
            }
        }
        if (!inside) {
            return BootInfoBadModule;
        }
        if (KernelEnd > KernelStart && mb < KernelEnd && ml >= KernelStart) {
            return BootInfoBadModule;
        }
    }
    return BootInfoOk;
}

const char *HalpBootInfoResultName(HALP_BOOTINFO_RESULT Result)
{
    switch (Result) {
    case BootInfoOk:
        return "ok";
    case BootInfoBadMagic:
        return "bad loader magic";
    case BootInfoBadPointer:
        return "bad pointer";
    case BootInfoBadSize:
        return "bad total size";
    case BootInfoBadTag:
        return "bad tag";
    case BootInfoNoEndTag:
        return "no end tag";
    case BootInfoBadMemoryMap:
        return "bad memory map tag";
    case BootInfoTooManyRegions:
        return "too many memory regions";
    case BootInfoWrap:
        return "region wraps past 2^64";
    case BootInfoOverlap:
        return "overlapping regions of different types";
    case BootInfoNoUsableMemory:
        return "no usable memory";
    case BootInfoTooManyModules:
        return "too many modules";
    case BootInfoBadModule:
        return "bad module";
    case BootInfoBadFramebuffer:
        return "bad framebuffer";
    case BootInfoBadRsdp:
        return "bad ACPI RSDP";
    default:
        return "unknown";
    }
}
