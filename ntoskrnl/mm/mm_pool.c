/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/*
 * Kernel pool. Owner: A04. See mm_pool.h.
 *
 * Block layout: [header 32][payload][trailer canary 8][padding]. A freed small block keeps its
 * header, stores the free-list link in the first 8 payload bytes and, in debug mode, is filled
 * with 0xDD; the fill is verified when the block is handed out again (write after free).
 */
#include "mm_internal.h"

void *memset(void *Dest, int Value, size_t Count);

#define MI_POOL_MAGIC_ALLOC UINT64_C(0x4C4F4F50414C4C4F) /* "OLLAPOOL" */
#define MI_POOL_MAGIC_FREE  UINT64_C(0x4C4F4F5046524545) /* "EERFPOOL" */
#define MI_POOL_CANARY      UINT64_C(0xA5C3E1F00F1E3C5A)
#define MI_POOL_FILL_ALLOC  0xCDu
#define MI_POOL_FILL_FREE   0xDDu
#define MI_POOL_LARGE       0xFFu
#define MI_POOL_MAX_LARGE   (UINT64_C(1) << 40)

typedef struct _POOL_HEADER {
    ULONG64 Magic; /* Magic ^ address of the header. */
    ULONG   Tag;
    UCHAR   Type;
    UCHAR   Class;
    USHORT  Spare;
    ULONG64 Size;  /* Requested size. */
    ULONG64 Pages; /* Large blocks only. */
} POOL_HEADER;
KAMOS_STATIC_ASSERT(sizeof(POOL_HEADER) == MM_POOL_HEADER_SIZE, "pool header is 32 bytes");

typedef struct _POOL_SLAB {
    struct _POOL_SLAB *Next;
    ULONG              Class;
    ULONG              Spare;
} POOL_SLAB;

static const ULONG MiClassSize[MM_POOL_CLASS_COUNT] = {64, 128, 256, 512, 1024};

#define POOL_STOP(Reason, Addr, Tag, Extra)                                                        \
    KeBugCheckEx(KAMOS_STOP_POOL_CORRUPTION, (ULONG_PTR)(Reason), (ULONG_PTR)(Addr),               \
                 (ULONG_PTR)(Tag), (ULONG_PTR)(Extra))

static ULONG64 MiMagic(ULONG64 Base, const POOL_HEADER *H)
{
    return Base ^ (ULONG64)(ULONG_PTR)H;
}

static ULONG64 MiCanary(const void *Payload)
{
    return MI_POOL_CANARY ^ (ULONG64)(ULONG_PTR)Payload;
}

static UCHAR *MiPayload(POOL_HEADER *H)
{
    return (UCHAR *)H + MM_POOL_HEADER_SIZE;
}

static ULONG MiPayloadCapacity(ULONG Class)
{
    return MiClassSize[Class] - MM_POOL_HEADER_SIZE;
}

static BOOLEAN MiTagValid(ULONG Tag)
{
    for (unsigned i = 0; i < 4; i++) {
        UCHAR c = (UCHAR)(Tag >> (8u * i));
        if (c < 0x20 || c > 0x7E)
            return false;
    }
    return true;
}

static void MiLock(MM_POOL *P)
{
    P->Hw->AcquireLock(P->Hw->Context, LOCK_RANK_POOL, &P->Lock);
}

static void MiUnlock(MM_POOL *P)
{
    P->Hw->ReleaseLock(P->Hw->Context, LOCK_RANK_POOL, &P->Lock);
}

void MiPoolInit(MM_POOL *Pool, const MM_HW_OPS *Hw, const MM_PAGE_PROVIDER *Provider, BOOLEAN Debug)
{
    memset(Pool, 0, sizeof *Pool);
    Pool->Hw       = Hw;
    Pool->Provider = *Provider;
    Pool->Debug    = Debug;
}

/* Finds the accounting slot of a tag, creating it if there is room. Lock held. */
static MM_POOL_TAG_INFO *MiTagSlot(MM_POOL *P, ULONG Tag, BOOLEAN Create)
{
    for (ULONG i = 0; i < P->TagCount; i++)
        if (P->Tags[i].Tag == Tag)
            return &P->Tags[i];
    if (P->TagCount < MM_POOL_MAX_TAGS) {
        if (!Create)
            return NULL;
        MM_POOL_TAG_INFO *t = &P->Tags[P->TagCount++];
        memset(t, 0, sizeof *t);
        t->Tag = Tag;
        return t;
    }
    return &P->Other;
}

static void MiFillFree(MM_POOL *P, POOL_HEADER *H, void *Next)
{
    UCHAR *payload = MiPayload(H);
    if (P->Debug)
        memset(payload, MI_POOL_FILL_FREE, MiPayloadCapacity(H->Class));
    *(void **)(void *)payload = Next;
}

static void MiCheckFillFree(const MM_POOL *P, POOL_HEADER *H)
{
    if (!P->Debug)
        return;
    const UCHAR *payload = MiPayload(H);
    ULONG        cap     = MiPayloadCapacity(H->Class);
    for (ULONG i = sizeof(void *); i < cap; i++)
        if (payload[i] != MI_POOL_FILL_FREE)
            POOL_STOP(MM_POOL_STOP_WRITE_AFTER_FREE, payload, H->Tag, i);
}

/* Cuts a fresh page into blocks of one class and pushes them on the free list. Lock held. */
static void MiCarve(MM_POOL *P, UCHAR *Page, ULONG Class, UCHAR Type)
{
    ULONG      size = MiClassSize[Class];
    POOL_SLAB *slab = (POOL_SLAB *)(void *)Page;
    slab->Next      = P->Slabs;
    slab->Class     = Class;
    slab->Spare     = 0;
    P->Slabs        = slab;
    P->SlabPages++;
    for (ULONG off = size; off + size <= PAGE_SIZE; off += size) {
        POOL_HEADER *h = (POOL_HEADER *)(void *)(Page + off);
        h->Magic       = MiMagic(MI_POOL_MAGIC_FREE, h);
        h->Tag         = 0;
        h->Type        = Type;
        h->Class       = (UCHAR)Class;
        h->Spare       = 0;
        h->Size        = 0;
        h->Pages       = 0;
        MiFillFree(P, h, P->FreeList[Type][Class]);
        P->FreeList[Type][Class] = h;
    }
}

static void MiAccountAlloc(MM_POOL *P, ULONG Tag, SIZE_T Size)
{
    MM_POOL_TAG_INFO *t = MiTagSlot(P, Tag, true);
    t->Allocations++;
    t->BytesInUse += Size;
    if (t->BytesInUse > t->PeakBytes)
        t->PeakBytes = t->BytesInUse;
    P->Live++;
}

void *MiPoolAllocate(MM_POOL *Pool, POOL_TYPE Type, SIZE_T Size, ULONG Tag)
{
    if (Size == 0 || Size > MI_POOL_MAX_LARGE || !MiTagValid(Tag) ||
        (Type != NonPagedPool && Type != PagedPool))
        return NULL;
    if (Pool->Debug && Pool->Hw->GetCurrentIrql != NULL) {
        UCHAR irql = Pool->Hw->GetCurrentIrql(Pool->Hw->Context);
        if ((Type == PagedPool && irql > PASSIVE_LEVEL) || irql > DISPATCH_LEVEL)
            KeBugCheckEx(KAMOS_STOP_IRQL_VIOLATION, irql, Type, Tag, 0);
    }

    POOL_HEADER *h;
    if (Size <= MM_POOL_SMALL_MAX) {
        ULONG cls = 0;
        while (MiPayloadCapacity(cls) < Size + MM_POOL_TRAILER)
            cls++;
        MiLock(Pool);
        h = Pool->FreeList[Type][cls];
        if (h == NULL) {
            MiUnlock(Pool);
            void *page = NULL;
            if (!NT_SUCCESS(Pool->Provider.GetPages(Pool->Provider.Context, 1, &page)) ||
                page == NULL)
                return NULL;
            MiLock(Pool);
            MiCarve(Pool, page, cls, (UCHAR)Type);
            h = Pool->FreeList[Type][cls];
        }
        POOL_HEADER *check = h;
        if (h->Magic != MiMagic(MI_POOL_MAGIC_FREE, h) || h->Class != cls || h->Type != Type)
            POOL_STOP(MM_POOL_STOP_BAD_HEADER, h, Tag, 0);
        Pool->FreeList[Type][cls] = *(void **)(void *)MiPayload(h);
        h->Magic                  = MiMagic(MI_POOL_MAGIC_ALLOC, h);
        h->Tag                    = Tag;
        h->Size                   = Size;
        MiAccountAlloc(Pool, Tag, Size);
        MiUnlock(Pool);
        MiCheckFillFree(Pool, check);
    } else {
        ULONG64 total = (ULONG64)MM_POOL_HEADER_SIZE + Size + MM_POOL_TRAILER;
        SIZE_T  pages = (SIZE_T)((total + (PAGE_SIZE - 1u)) >> PAGE_SHIFT);
        void   *va    = NULL;
        if (!NT_SUCCESS(Pool->Provider.GetPages(Pool->Provider.Context, pages, &va)) || va == NULL)
            return NULL;
        h        = va;
        h->Magic = MiMagic(MI_POOL_MAGIC_ALLOC, h);
        h->Tag   = Tag;
        h->Type  = (UCHAR)Type;
        h->Class = MI_POOL_LARGE;
        h->Spare = 0;
        h->Size  = Size;
        h->Pages = pages;
        MiLock(Pool);
        Pool->LargePages += pages;
        MiAccountAlloc(Pool, Tag, Size);
        MiUnlock(Pool);
    }

    UCHAR *payload = MiPayload(h);
    if (Pool->Debug) {
        ULONG64 cap = (h->Class == MI_POOL_LARGE) ? h->Pages * PAGE_SIZE - MM_POOL_HEADER_SIZE
                                                  : MiPayloadCapacity(h->Class);
        memset(payload, MI_POOL_FILL_ALLOC, cap);
        ULONG64 canary = MiCanary(payload);
        for (unsigned i = 0; i < MM_POOL_TRAILER; i++)
            payload[Size + i] = (UCHAR)(canary >> (8u * i));
    }
    return payload;
}

void MiPoolFree(MM_POOL *Pool, void *Block, ULONG Tag)
{
    if (Block == NULL)
        return;
    if (((ULONG_PTR)Block & (MM_POOL_ALIGNMENT - 1u)) != 0)
        POOL_STOP(MM_POOL_STOP_BAD_POINTER, Block, Tag, 0);
    POOL_HEADER *h = (POOL_HEADER *)(void *)((UCHAR *)Block - MM_POOL_HEADER_SIZE);
    if (h->Magic == MiMagic(MI_POOL_MAGIC_FREE, h))
        POOL_STOP(MM_POOL_STOP_DOUBLE_FREE, Block, Tag, h->Tag);
    if (h->Magic != MiMagic(MI_POOL_MAGIC_ALLOC, h))
        POOL_STOP(MM_POOL_STOP_BAD_HEADER, Block, Tag, 0);
    if (h->Tag != Tag)
        POOL_STOP(MM_POOL_STOP_WRONG_TAG, Block, Tag, h->Tag);
    if (h->Type > (UCHAR)PagedPool ||
        (h->Class >= MM_POOL_CLASS_COUNT && h->Class != MI_POOL_LARGE))
        POOL_STOP(MM_POOL_STOP_BAD_HEADER, Block, Tag, 1);

    ULONG64 size = h->Size;
    ULONG64 cap  = (h->Class == MI_POOL_LARGE) ? h->Pages * PAGE_SIZE - MM_POOL_HEADER_SIZE
                                               : MiPayloadCapacity(h->Class);
    if (size == 0 || size + MM_POOL_TRAILER > cap)
        POOL_STOP(MM_POOL_STOP_BAD_HEADER, Block, Tag, 2);
    if (Pool->Debug) {
        const UCHAR *payload = Block;
        ULONG64      canary  = MiCanary(payload);
        for (unsigned i = 0; i < MM_POOL_TRAILER; i++)
            if (payload[size + i] != (UCHAR)(canary >> (8u * i)))
                POOL_STOP(MM_POOL_STOP_OVERRUN, Block, Tag, size);
    }

    UCHAR   type  = h->Type;
    UCHAR   cls   = h->Class;
    ULONG64 pages = h->Pages;
    h->Magic      = MiMagic(MI_POOL_MAGIC_FREE, h); /* From here a second free is caught. */
    if (cls != MI_POOL_LARGE)
        MiFillFree(Pool, h, NULL); /* The link is written below, under the lock. */

    MiLock(Pool);
    MM_POOL_TAG_INFO *t = MiTagSlot(Pool, Tag, false);
    if (t == NULL || t->BytesInUse < size || Pool->Live == 0) {
        MiUnlock(Pool);
        POOL_STOP(MM_POOL_STOP_ACCOUNTING, Block, Tag, size);
        return;
    }
    t->Frees++;
    t->BytesInUse -= size;
    Pool->Live--;
    if (cls == MI_POOL_LARGE) {
        Pool->LargePages -= pages;
        MiUnlock(Pool);
        Pool->Provider.PutPages(Pool->Provider.Context, h, (SIZE_T)pages);
        return;
    }
    *(void **)(void *)MiPayload(h) = Pool->FreeList[type][cls];
    Pool->FreeList[type][cls]      = h;
    MiUnlock(Pool);
}

ULONG MiPoolTagCount(const MM_POOL *Pool)
{
    return Pool->TagCount;
}

BOOLEAN MiPoolGetTag(const MM_POOL *Pool, ULONG Index, MM_POOL_TAG_INFO *Info)
{
    if (Index >= Pool->TagCount)
        return false;
    *Info = Pool->Tags[Index];
    return true;
}

BOOLEAN MiPoolFindTag(const MM_POOL *Pool, ULONG Tag, MM_POOL_TAG_INFO *Info)
{
    for (ULONG i = 0; i < Pool->TagCount; i++) {
        if (Pool->Tags[i].Tag == Tag) {
            *Info = Pool->Tags[i];
            return true;
        }
    }
    return false;
}

void MiPoolReleaseAll(MM_POOL *Pool)
{
    if (Pool->Live != 0)
        POOL_STOP(MM_POOL_STOP_LIVE_AT_RELEASE, Pool->Live, 0, 0);
    POOL_SLAB *s = Pool->Slabs;
    while (s != NULL) {
        POOL_SLAB *next = s->Next;
        Pool->Provider.PutPages(Pool->Provider.Context, s, 1);
        s = next;
    }
    Pool->Slabs     = NULL;
    Pool->SlabPages = 0;
    for (unsigned t = 0; t < MM_POOL_TYPES; t++)
        for (unsigned c = 0; c < MM_POOL_CLASS_COUNT; c++)
            Pool->FreeList[t][c] = NULL;
}
