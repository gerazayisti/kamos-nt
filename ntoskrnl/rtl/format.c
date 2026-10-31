/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 The Kamos-NT Authors */
/* Bounded printf-style formatter. Pure: no global state, no allocation, no library call. */

#include "rtl.h"

typedef struct _RTLP_OUT {
    char  *Buf;
    size_t Cap;
    size_t Len; /* Characters produced, including those that did not fit. */
} RTLP_OUT;

static void Put(RTLP_OUT *Out, char C)
{
    if (Out->Cap != 0 && Out->Len < Out->Cap - 1) {
        Out->Buf[Out->Len] = C;
    }
    Out->Len++;
}

static void PutRepeat(RTLP_OUT *Out, char C, size_t Count)
{
    while (Count-- != 0) {
        Put(Out, C);
    }
}

/* Prefix (sign or 0x) then digits, padded to Width. */
static void PutNumber(RTLP_OUT *Out, uint64_t Value, unsigned Base, int Upper, const char *Prefix,
                      size_t Width, int Left, int Zero)
{
    char        digits[24];
    size_t      n         = 0;
    size_t      prefixLen = 0;
    size_t      total;
    const char *alphabet = Upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do {
        digits[n++] = alphabet[Value % Base];
        Value /= Base;
    } while (Value != 0);
    while (Prefix[prefixLen] != '\0') {
        prefixLen++;
    }
    total = prefixLen + n;
    if (!Left && !Zero && Width > total) {
        PutRepeat(Out, ' ', Width - total);
    }
    for (size_t i = 0; i < prefixLen; i++) {
        Put(Out, Prefix[i]);
    }
    if (!Left && Zero && Width > total) {
        PutRepeat(Out, '0', Width - total);
    }
    while (n != 0) {
        Put(Out, digits[--n]);
    }
    if (Left && Width > total) {
        PutRepeat(Out, ' ', Width - total);
    }
}

size_t RtlFormatV(char *Buffer, size_t Capacity, const char *Format, va_list Args)
{
    RTLP_OUT out = {Buffer, Capacity, 0};

    for (; *Format != '\0'; Format++) {
        int         left  = 0;
        int         zero  = 0;
        int         alt   = 0;
        int         is64  = 0;
        size_t      width = 0;
        const char *start = Format;

        if (*Format != '%') {
            Put(&out, *Format);
            continue;
        }
        Format++;
        for (;; Format++) {
            if (*Format == '-') {
                left = 1;
            } else if (*Format == '0') {
                zero = 1;
            } else if (*Format == '#') {
                alt = 1;
            } else {
                break;
            }
        }
        if (*Format == '*') {
            int w = va_arg(Args, int);
            if (w < 0) {
                left = 1;
                w    = -w;
            }
            width = (size_t)w;
            Format++;
        } else {
            while (*Format >= '0' && *Format <= '9') {
                width = width * 10 + (size_t)(*Format - '0');
                Format++;
            }
        }
        if (*Format == 'l') {
            is64 = 1;
            Format++;
            if (*Format == 'l') {
                Format++;
            }
        } else if (*Format == 'z') {
            is64 = 1;
            Format++;
        }

        switch (*Format) {
        case 'c':
            if (!left && width > 1) {
                PutRepeat(&out, ' ', width - 1);
            }
            Put(&out, (char)va_arg(Args, int));
            if (left && width > 1) {
                PutRepeat(&out, ' ', width - 1);
            }
            break;
        case 's': {
            const char *s = va_arg(Args, const char *);
            size_t      n = 0;
            if (s == NULL) {
                s = "(null)";
            }
            while (s[n] != '\0') {
                n++;
            }
            if (!left && width > n) {
                PutRepeat(&out, ' ', width - n);
            }
            for (size_t i = 0; i < n; i++) {
                Put(&out, s[i]);
            }
            if (left && width > n) {
                PutRepeat(&out, ' ', width - n);
            }
            break;
        }
        case 'd':
        case 'i': {
            int64_t  v = is64 ? (int64_t)va_arg(Args, long) : (int64_t)va_arg(Args, int);
            uint64_t u = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
            PutNumber(&out, u, 10, 0, v < 0 ? "-" : "", width, left, zero);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            uint64_t u =
                is64 ? (uint64_t)va_arg(Args, unsigned long) : (uint64_t)va_arg(Args, unsigned int);
            unsigned    base   = (*Format == 'u') ? 10u : 16u;
            const char *prefix = "";
            if (alt && base == 16 && u != 0) {
                prefix = (*Format == 'X') ? "0X" : "0x";
            }
            PutNumber(&out, u, base, *Format == 'X', prefix, width, left, zero);
            break;
        }
        case 'p':
            PutNumber(&out, (uint64_t)(uintptr_t)va_arg(Args, void *), 16, 0, "0x", 18, 0, 1);
            break;
        case '%':
            Put(&out, '%');
            break;
        default:
            /* Unknown or truncated conversion: copy it as written. */
            for (const char *c = start; c <= Format && *c != '\0'; c++) {
                Put(&out, *c);
            }
            if (*Format == '\0') {
                Format--;
            }
            break;
        }
    }
    if (Capacity != 0) {
        Buffer[out.Len < Capacity ? out.Len : Capacity - 1] = '\0';
    }
    return out.Len;
}

size_t RtlFormat(char *Buffer, size_t Capacity, const char *Format, ...)
{
    va_list ap;
    size_t  n;

    va_start(ap, Format);
    n = RtlFormatV(Buffer, Capacity, Format, ap);
    va_end(ap);
    return n;
}
