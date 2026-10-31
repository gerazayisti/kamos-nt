/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * HAL and architecture private header. Owner: A03. Not part of the public contract (sdk/include).
 * Included by relative path because the kernel build has only -Isdk/include.
 */
#ifndef KAMOS_HALP_H
#define KAMOS_HALP_H

#include <kamos/boot.h>
#include <kamos/bugcheck.h>
#include <kamos/hal.h>
#include <kamos/irql.h>
#include <kamos/types.h>

#include "../ntoskrnl/rtl/rtl.h"
#include "halp_bootinfo.h"

/* ---- Raw CPU access (arch/x86_64/cpu) ---------------------------------------------------- */

static inline ULONG64 ArchReadCr0(void)
{
    ULONG64 v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline ULONG64 ArchReadCr2(void)
{
    ULONG64 v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline ULONG64 ArchReadCr3(void)
{
    ULONG64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void ArchWriteCr3(ULONG64 v)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

static inline ULONG64 ArchReadCr4(void)
{
    ULONG64 v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void ArchWriteCr4(ULONG64 v)
{
    __asm__ volatile("mov %0, %%cr4" : : "r"(v) : "memory");
}

static inline ULONG64 ArchReadMsr(ULONG Msr)
{
    ULONG lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(Msr));
    return ((ULONG64)hi << 32) | lo;
}

static inline void ArchWriteMsr(ULONG Msr, ULONG64 Value)
{
    __asm__ volatile("wrmsr" : : "c"(Msr), "a"((ULONG)Value), "d"((ULONG)(Value >> 32)));
}

static inline ULONG64 ArchReadTsc(void)
{
    ULONG lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((ULONG64)hi << 32) | lo;
}

static inline ULONG64 ArchReadRflags(void)
{
    ULONG64 v;
    __asm__ volatile("pushfq; popq %0" : "=r"(v));
    return v;
}

#define ARCH_RFLAGS_IF (1ull << 9)

/* Raw interrupt flag access, not counted as a HalDisableInterrupts section. */
static inline ULONG64 ArchIrqSave(void)
{
    ULONG64 f = ArchReadRflags();
    __asm__ volatile("cli" : : : "memory");
    return f;
}

static inline void ArchIrqRestore(ULONG64 Flags)
{
    if ((Flags & ARCH_RFLAGS_IF) != 0) {
        __asm__ volatile("sti" : : : "memory");
    }
}

static inline void ArchInvlpg(const void *Address)
{
    __asm__ volatile("invlpg (%0)" : : "r"(Address) : "memory");
}

#define MSR_EFER 0xC0000080u
#define EFER_LME (1ull << 8)
#define EFER_LMA (1ull << 10)
#define EFER_NXE (1ull << 11)
#define CR0_PE   (1ull << 0)
#define CR0_WP   (1ull << 16)
#define CR0_PG   (1ull << 31)
#define CR4_PAE  (1ull << 5)
#define CR4_SMEP (1ull << 20)
#define CR4_SMAP (1ull << 21)

/* Enables SMEP and SMAP when CPUID reports them. Returns the CR4 bits it set. */
ULONG64 ArchInitCpuFeatures(void);

/* ---- Boot (arch/x86_64/boot) ------------------------------------------------------------- */

extern ULONG KiBootMagic;
extern ULONG KiBootMbiPhysical;

/* Removes the identity alias of the image and makes the rest of the low 2 MiB non-executable. */
void ArchFinishBootMappings(void);

_Noreturn void KiStartKernel(ULONG MultibootMagic, ULONG MbiPhysical);

/* ---- Console and formatted output (hal/console.c) ---------------------------------------- */

int  kprintf(const char *Format, ...) __attribute__((format(printf, 1, 2)));
void HalpVgaBugCheckScreen(const char *Title, const char *Line1, const char *Line2);

/* ---- Boot information (hal/bootinfo.c, hal/bootinfo_init.c) ------------------------------ */

/* Validates and stores the loader's structure; bug checks on failure. */
void HalInitializeBootInformation(ULONG Magic, ULONG MbiPhysical);

/* ---- Bug check and test scenarios ---------------------------------------------------------- */

/* Runs the in-kernel tests selected by QEMU fw_cfg (hal/ktest). Never returns. */
_Noreturn void KtMain(void);

/*
 * QEMU fw_cfg: copies file Name into Buffer (NUL-terminated); returns its length, -1 when the
 * device has no such file, -2 when there is no fw_cfg device.
 */
int HalpFwCfgReadFile(const char *Name, char *Buffer, ULONG BufferSize);

#endif /* KAMOS_HALP_H */
