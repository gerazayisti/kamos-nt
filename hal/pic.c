/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Legacy 8259 PIC: remap, masks, EOI, spurious IRQ7 and IRQ15, interrupt connection (hal.h).
 * Source: Intel 8259A data sheet (ICW1 to ICW4, OCW2, OCW3). Every line is a DEVICE_LEVEL
 * interrupt.
 */

#include "halp.h"

#define PIC1_CMD  0x20u
#define PIC1_DATA 0x21u
#define PIC2_CMD  0xA0u
#define PIC2_DATA 0xA1u
#define PIC_EOI   0x20u

typedef struct _LINE {
    HAL_INTERRUPT_HANDLER Handler;
    void                 *Context;
} LINE;

static LINE  Lines[16];
static ULONG SpuriousCount[16];

static void IoWait(void)
{
    HalWritePortUchar(0x80, 0); /* the classic delay: a write to an unused port */
}

void HalpInitPic(void)
{
    HalWritePortUchar(PIC1_CMD, 0x11); /* ICW1: edge triggered, cascade, ICW4 needed */
    IoWait();
    HalWritePortUchar(PIC2_CMD, 0x11);
    IoWait();
    HalWritePortUchar(PIC1_DATA, KI_VECTOR_IRQ_BASE); /* ICW2: vector offsets 0x20 and 0x28 */
    IoWait();
    HalWritePortUchar(PIC2_DATA, KI_VECTOR_IRQ_BASE + 8);
    IoWait();
    HalWritePortUchar(PIC1_DATA, 0x04); /* ICW3: slave on IRQ2 */
    IoWait();
    HalWritePortUchar(PIC2_DATA, 0x02); /* slave identity 2 */
    IoWait();
    HalWritePortUchar(PIC1_DATA, 0x01); /* ICW4: 8086 mode */
    IoWait();
    HalWritePortUchar(PIC2_DATA, 0x01);
    IoWait();
    HalWritePortUchar(PIC1_DATA, 0xFB); /* everything masked except the cascade */
    HalWritePortUchar(PIC2_DATA, 0xFF);
}

static void SetMask(ULONG Line, BOOLEAN Masked)
{
    USHORT port = Line < 8 ? PIC1_DATA : PIC2_DATA;
    UCHAR  bit  = (UCHAR)(1u << (Line & 7));
    UCHAR  mask = HalReadPortUchar(port);

    HalWritePortUchar(port, Masked ? (UCHAR)(mask | bit) : (UCHAR)(mask & ~bit));
}

/* OCW3 read of the in-service (Isr = true) or request register: master in the low byte. */
USHORT HalpPicReadRegister(BOOLEAN Isr)
{
    UCHAR  ocw3 = Isr ? 0x0B : 0x0A;
    USHORT v;

    HalWritePortUchar(PIC1_CMD, ocw3);
    HalWritePortUchar(PIC2_CMD, ocw3);
    v = HalReadPortUchar(PIC1_CMD);
    v = (USHORT)(v | ((USHORT)HalReadPortUchar(PIC2_CMD) << 8));
    return v;
}

ULONG HalpPicSpuriousCount(ULONG Line)
{
    return Line < 16 ? SpuriousCount[Line] : 0;
}

NTSTATUS HalConnectInterrupt(ULONG Line, HAL_INTERRUPT_HANDLER Handler, void *Context)
{
    ULONG64 flags;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (Line >= 16 || Line == 2 || Handler == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    flags = ArchIrqSave();
    if (Lines[Line].Handler != NULL) {
        ArchIrqRestore(flags);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    Lines[Line].Handler = Handler;
    Lines[Line].Context = Context;
    SetMask(Line, false);
    ArchIrqRestore(flags);
    return STATUS_SUCCESS;
}

static void EndOfInterrupt(ULONG Line)
{
    if (Line >= 8) {
        HalWritePortUchar(PIC2_CMD, PIC_EOI);
    }
    HalWritePortUchar(PIC1_CMD, PIC_EOI);
}

void HalpIrqDispatch(KTRAP_FRAME *Tf, KIRQL InterruptedIrql)
{
    ULONG line = (ULONG)(Tf->Vector - KI_VECTOR_IRQ_BASE);

    (void)InterruptedIrql;
    if (line == 7 || line == 15) {
        /* A spurious interrupt leaves its ISR bit clear: no handler, and no EOI for it. */
        if ((HalpPicReadRegister(true) & (1u << line)) == 0) {
            SpuriousCount[line]++;
            if (line == 15) {
                HalWritePortUchar(PIC1_CMD, PIC_EOI); /* the master did see the cascade */
            }
            return;
        }
    }
    if (Lines[line].Handler != NULL) {
        (void)Lines[line].Handler(Lines[line].Context);
    }
    EndOfInterrupt(line);
}
