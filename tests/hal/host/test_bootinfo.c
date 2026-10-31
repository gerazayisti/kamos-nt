/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Host tests of the Multiboot2 parser (hal/bootinfo.c) against hostile structures (threat model
 * B1, oracle O10). Every blob is built here, byte by byte, from the Multiboot2 specification 2.0
 * section 3.6; the expected result of each is written by hand, not taken from the parser.
 */
#define KTEST_HOST 1
#include <ktest.h>
#include <string.h>

#include "../../../hal/halp_bootinfo.h"

#define MB 0x100000ull

typedef struct {
    uint8_t b[16384];
    size_t  n;
} Blob;

static void P32(Blob *x, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        x->b[x->n++] = (uint8_t)(v >> (8 * i));
    }
}

static void P64(Blob *x, uint64_t v)
{
    P32(x, (uint32_t)v);
    P32(x, (uint32_t)(v >> 32));
}

static void Pad8(Blob *x)
{
    while (x->n % 8) {
        x->b[x->n++] = 0;
    }
}

static void Begin(Blob *x)
{
    memset(x, 0, sizeof *x);
    P32(x, 0); /* total, patched by End */
    P32(x, 0);
}

static void End(Blob *x)
{
    P32(x, 0);
    P32(x, 8);
    x->b[0] = (uint8_t)x->n;
    x->b[1] = (uint8_t)(x->n >> 8);
    x->b[2] = (uint8_t)(x->n >> 16);
    x->b[3] = (uint8_t)(x->n >> 24);
}

typedef struct {
    uint64_t base, len;
    uint32_t type;
} Ent;

static void Mmap(Blob *x, const Ent *e, int count, uint32_t entSize, uint32_t version)
{
    uint32_t size = 16 + (uint32_t)count * entSize;
    P32(x, 6);
    P32(x, size);
    P32(x, entSize);
    P32(x, version);
    for (int i = 0; i < count; i++) {
        P64(x, e[i].base);
        P64(x, e[i].len);
        P32(x, e[i].type);
        P32(x, 0);
        for (uint32_t k = 24; k < entSize; k++) {
            x->b[x->n++] = 0;
        }
    }
    Pad8(x);
}

static void Module(Blob *x, uint32_t start, uint32_t end, const char *name, int terminate)
{
    uint32_t len = (uint32_t)strlen(name) + (terminate ? 1u : 0u);
    P32(x, 3);
    P32(x, 16 + len);
    P32(x, start);
    P32(x, end);
    memcpy(&x->b[x->n], name, len);
    x->n += len;
    Pad8(x);
}

static HALP_BOOTINFO_RESULT Parse(Blob *x, BOOT_INFORMATION *out)
{
    return HalpParseMultiboot2(x->b, 0x9000, x->n, 1 * MB, 2 * MB, out);
}

static const Ent GoodMap[] = {
    {0x0, 0x9FC00, 1},
    {0x9FC00, 0x400, 2},
    {0xF0000, 0x10000, 2},
    {0x100000, 0x1FF00000, 1},
};

static void GoodBlob(Blob *x)
{
    Begin(x);
    Mmap(x, GoodMap, 4, 24, 0);
    Module(x, 0x300000, 0x301000, "initrd", 1);
    End(x);
}

static void test_good(void)
{
    Blob             x;
    BOOT_INFORMATION bi;

    GoodBlob(&x);
    KT_REQUIRE_EQ(Parse(&x, &bi), BootInfoOk);
    KT_CHECK_EQ(bi.RegionCount, 4);
    KT_CHECK_EQ(bi.Regions[3].Base, 0x100000);
    KT_CHECK_EQ(bi.Regions[3].Length, 0x1FF00000);
    KT_CHECK_EQ(bi.Regions[3].Type, BootMemoryUsable);
    KT_CHECK_EQ(bi.Regions[1].Type, BootMemoryReserved);
    KT_CHECK_EQ(bi.ModuleCount, 1);
    KT_CHECK_EQ(bi.Modules[0].Base, 0x300000);
    KT_CHECK_EQ(bi.Modules[0].Length, 0x1000);
    KT_CHECK(strcmp(bi.Modules[0].Name, "initrd") == 0);
    KT_CHECK(!bi.Framebuffer.Present);
    KT_CHECK_EQ(bi.AcpiRsdp, 0);
}

static void test_sort_drop_merge(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    /* Unsorted; a zero-length entry; two touching usable entries; two overlapping usable. */
    static const Ent m[] = {
        {0x200000, 0x100000, 1}, {0x100000, 0x100000, 1}, {0x500000, 0, 2},
        {0x0, 0x9F000, 1},       {0x700000, 0x200000, 3}, {0x800000, 0x200000, 3},
    };

    Begin(&x);
    Mmap(&x, m, 6, 24, 0);
    End(&x);
    KT_REQUIRE_EQ(Parse(&x, &bi), BootInfoOk);
    KT_REQUIRE_EQ(bi.RegionCount, 3);
    KT_CHECK_EQ(bi.Regions[0].Base, 0x0);
    KT_CHECK_EQ(bi.Regions[0].Length, 0x9F000);
    KT_CHECK_EQ(bi.Regions[1].Base, 0x100000);
    KT_CHECK_EQ(bi.Regions[1].Length, 0x200000);
    KT_CHECK_EQ(bi.Regions[2].Base, 0x700000);
    KT_CHECK_EQ(bi.Regions[2].Length, 0x300000);
    KT_CHECK_EQ(bi.Regions[2].Type, BootMemoryAcpiReclaimable);
}

static void test_overlap_different_type(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    static const Ent m[] = {{0x100000, 0x100000, 1}, {0x180000, 0x1000, 2}};

    Begin(&x);
    Mmap(&x, m, 2, 24, 0);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoOverlap);
}

static void test_touching_different_type_ok(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    static const Ent m[] = {{0x100000, 0x100000, 1}, {0x200000, 0x1000, 2}};

    Begin(&x);
    Mmap(&x, m, 2, 24, 0);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoOk);
    KT_CHECK_EQ(bi.RegionCount, 2);
}

static void test_wrap(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    static const Ent wrap[] = {{0x100000, 0x100000, 1}, {0xFFFFFFFFFFFFF000ull, 0x2000, 2}};
    static const Ent edge[] = {{0x100000, 0x100000, 1}, {0xFFFFFFFFFFFFF000ull, 0x1000, 2}};

    Begin(&x);
    Mmap(&x, wrap, 2, 24, 0);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoWrap);
    Begin(&x);
    Mmap(&x, edge, 2, 24, 0);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoOk); /* the last byte is 2^64 - 1: no wrap */
}

static void test_too_many_regions(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    Ent              m[200];

    /* Alternating types, disjoint and not touching in a mergeable way: 129 stay 129. */
    for (int i = 0; i < 129; i++) {
        m[i].base = 0x100000ull + (uint64_t)i * 0x2000;
        m[i].len  = 0x1000;
        m[i].type = (i % 2) ? 2 : 1;
    }
    Begin(&x);
    Mmap(&x, m, 129, 24, 0);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoTooManyRegions);
    /* 128 exactly fits. */
    Begin(&x);
    Mmap(&x, m, 128, 24, 0);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoOk);
    KT_CHECK_EQ(bi.RegionCount, 128);
}

static void test_many_mergeable_regions_ok(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    Ent              m[200];

    /* 200 touching usable entries merge into one: the limit applies after merging. */
    for (int i = 0; i < 200; i++) {
        m[i].base = 0x100000ull + (uint64_t)i * 0x1000;
        m[i].len  = 0x1000;
        m[i].type = 1;
    }
    Begin(&x);
    Mmap(&x, m, 200, 24, 0);
    End(&x);
    KT_REQUIRE_EQ(Parse(&x, &bi), BootInfoOk);
    KT_CHECK_EQ(bi.RegionCount, 1);
    KT_CHECK_EQ(bi.Regions[0].Length, 200 * 0x1000);
}

static void test_no_usable(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    static const Ent m[] = {{0x100000, 0x100000, 2}};

    Begin(&x);
    Mmap(&x, m, 1, 24, 0);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoNoUsableMemory);
    Begin(&x); /* no memory map tag at all */
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoNoUsableMemory);
}

static void test_size_fields(void)
{
    Blob             x;
    BOOT_INFORMATION bi;

    GoodBlob(&x);
    KT_REQUIRE_EQ(HalpParseMultiboot2(x.b, 0x9000, x.n - 8, 0, 0, &bi), BootInfoBadSize); /* cut */
    KT_CHECK_EQ(HalpParseMultiboot2(x.b, 0x9000, 15, 0, 0, &bi), BootInfoBadPointer);
    GoodBlob(&x);
    x.b[0] += 4; /* total not a multiple of 8 */
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadSize);
    GoodBlob(&x);
    x.b[0] = 8; /* total below the minimum */
    x.b[1] = 0;
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadSize);
    GoodBlob(&x);
    x.b[0] = 0xF8;
    x.b[1] = 0xFF;
    x.b[2] = 0xFF;
    x.b[3] = 0xFF; /* about 4 GiB */
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadSize);
    KT_CHECK_EQ(HalpParseMultiboot2(NULL, 0x9000, 4096, 0, 0, &bi), BootInfoBadPointer);
    GoodBlob(&x);
    KT_CHECK_EQ(HalpParseMultiboot2(x.b, 0x9004, x.n, 0, 0, &bi), BootInfoBadPointer);
}

static void test_tag_sizes(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    size_t           tag0;

    GoodBlob(&x);
    tag0          = 8; /* the memory map tag starts right after the 8-byte header */
    x.b[tag0 + 4] = 0; /* size 0 */
    x.b[tag0 + 5] = 0;
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadTag);
    GoodBlob(&x);
    x.b[tag0 + 4] = 7; /* size below the header */
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadTag);
    GoodBlob(&x);
    x.b[tag0 + 4] = 0xFF; /* beyond the structure */
    x.b[tag0 + 5] = 0xFF;
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadTag);
    GoodBlob(&x);
    x.b[x.n - 4] = 9; /* end tag of size 9 */
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadTag);
}

static void test_no_end_tag(void)
{
    Blob             x;
    BOOT_INFORMATION bi;

    GoodBlob(&x);
    x.b[x.n - 8] = 1; /* end tag type replaced by a command line tag of size 8 */
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoNoEndTag);
}

static void test_mmap_shape(void)
{
    Blob             x;
    BOOT_INFORMATION bi;

    Begin(&x);
    Mmap(&x, GoodMap, 4, 16, 0); /* entry size below 24 */
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadMemoryMap);
    Begin(&x);
    Mmap(&x, GoodMap, 4, 28, 0); /* not a multiple of 8 */
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadMemoryMap);
    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 1); /* unknown version */
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadMemoryMap);
    Begin(&x);
    Mmap(&x, GoodMap, 4, 32, 0); /* a larger entry size is legal */
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoOk);
    GoodBlob(&x);
    x.b[8 + 4] += 4; /* size not a whole number of entries */
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadMemoryMap);
    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    Mmap(&x, GoodMap, 4, 24, 0); /* two memory map tags */
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadMemoryMap);
}

static void test_modules(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    char             longname[40];

    memset(longname, 'n', sizeof longname);
    longname[32] = '\0'; /* 32 characters: no room for the NUL in a 32-byte field */
    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    Module(&x, 0x300000, 0x301000, longname, 1);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadModule);

    longname[31] = '\0'; /* 31 characters fit */
    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    Module(&x, 0x300000, 0x301000, longname, 1);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoOk);

    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    Module(&x, 0x300000, 0x301000, "unterminated", 0);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadModule);

    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    Module(&x, 0x301000, 0x300000, "backwards", 1);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadModule);

    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    Module(&x, 0x300000, 0x300000, "empty", 1);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadModule);

    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    for (int i = 0; i < 9; i++) {
        Module(&x, 0x300000 + 0x1000u * (uint32_t)i, 0x301000 + 0x1000u * (uint32_t)i, "m", 1);
    }
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoTooManyModules);

    Begin(&x); /* module in reserved memory */
    Mmap(&x, GoodMap, 4, 24, 0);
    Module(&x, 0xF0000, 0xF1000, "rom", 1);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadModule);

    Begin(&x); /* module partly beyond usable memory */
    Mmap(&x, GoodMap, 4, 24, 0);
    Module(&x, 0x1FFFF000, 0x20200000, "tail", 1);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadModule);

    Begin(&x); /* module overlapping the kernel image range [1 MiB, 2 MiB) */
    Mmap(&x, GoodMap, 4, 24, 0);
    Module(&x, 0x1FF000, 0x201000, "overlap", 1);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadModule);

    Begin(&x); /* module right after the kernel range is fine */
    Mmap(&x, GoodMap, 4, 24, 0);
    Module(&x, 0x200000, 0x201000, "after", 1);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoOk);
}

static void test_framebuffer_rsdp(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    uint8_t          rsdp[20] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
    uint8_t          sum      = 0;

    for (int i = 0; i < 20; i++) {
        sum = (uint8_t)(sum + rsdp[i]);
    }
    rsdp[8] = (uint8_t)(0 - sum); /* checksum byte */

    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    P32(&x, 14);
    P32(&x, 8 + 20);
    memcpy(&x.b[x.n], rsdp, 20);
    x.n += 20;
    Pad8(&x);
    P32(&x, 8); /* framebuffer 1024x768x32, pitch 4096 */
    P32(&x, 32);
    P64(&x, 0xFD000000ull);
    P32(&x, 4096);
    P32(&x, 1024);
    P32(&x, 768);
    x.b[x.n++] = 32;
    x.b[x.n++] = 1;
    x.b[x.n++] = 0;
    x.b[x.n++] = 0;
    Pad8(&x);
    End(&x);
    KT_REQUIRE_EQ(Parse(&x, &bi), BootInfoOk);
    KT_CHECK_EQ(bi.AcpiRsdp, 0x9000 + 8 + 112 + 8); /* header, memory map tag, tag header */
    KT_CHECK(bi.Framebuffer.Present);
    KT_CHECK_EQ(bi.Framebuffer.Width, 1024);
    KT_CHECK_EQ(bi.Framebuffer.Pitch, 4096);
    KT_CHECK_EQ(bi.Framebuffer.Base, 0xFD000000ull);

    /* Bad checksum. */
    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    rsdp[8] ^= 1;
    P32(&x, 15);
    P32(&x, 8 + 20);
    memcpy(&x.b[x.n], rsdp, 20);
    x.n += 20;
    Pad8(&x);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadRsdp);

    /* Pitch smaller than a scanline. */
    Begin(&x);
    Mmap(&x, GoodMap, 4, 24, 0);
    P32(&x, 8);
    P32(&x, 32);
    P64(&x, 0xFD000000ull);
    P32(&x, 100);
    P32(&x, 1024);
    P32(&x, 768);
    x.b[x.n++] = 32;
    x.b[x.n++] = 1;
    x.b[x.n++] = 0;
    x.b[x.n++] = 0;
    Pad8(&x);
    End(&x);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoBadFramebuffer);
}

static void test_input_not_modified_and_bounded(void)
{
    Blob             x;
    BOOT_INFORMATION bi;
    uint8_t          copy[sizeof x.b];

    /* The parser reads only within the declared size: poison the bytes past the end. */
    GoodBlob(&x);
    memset(&x.b[x.n], 0xA5, sizeof x.b - x.n);
    memcpy(copy, x.b, sizeof copy);
    KT_CHECK_EQ(Parse(&x, &bi), BootInfoOk);
    KT_CHECK(memcmp(copy, x.b, sizeof copy) == 0);
}

int main(void)
{
    static const kt_case cases[] = {
        KT_CASE("bootinfo", test_good),
        KT_CASE("bootinfo", test_sort_drop_merge),
        KT_CASE("bootinfo", test_overlap_different_type),
        KT_CASE("bootinfo", test_touching_different_type_ok),
        KT_CASE("bootinfo", test_wrap),
        KT_CASE("bootinfo", test_too_many_regions),
        KT_CASE("bootinfo", test_many_mergeable_regions_ok),
        KT_CASE("bootinfo", test_no_usable),
        KT_CASE("bootinfo", test_size_fields),
        KT_CASE("bootinfo", test_tag_sizes),
        KT_CASE("bootinfo", test_no_end_tag),
        KT_CASE("bootinfo", test_mmap_shape),
        KT_CASE("bootinfo", test_modules),
        KT_CASE("bootinfo", test_framebuffer_rsdp),
        KT_CASE("bootinfo", test_input_not_modified_and_bounded),
    };
    return kt_run(cases, sizeof cases / sizeof cases[0]) ? 1 : 0;
}
