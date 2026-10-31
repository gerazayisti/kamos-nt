# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The Kamos-NT Authors
#
# Top-level build. Owner: A02.
# Every target stops at the first error and keeps the failing exit code.

SHELL       := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := all

SRCDIR    := $(abspath .)
BUILD     := build
CROSS     ?= $(HOME)/opt/cross/bin/x86_64-elf-
CC        := $(CROSS)gcc
LD        := $(CROSS)ld
NASM      ?= nasm
HOSTCC    ?= cc
QEMU      ?= qemu-system-x86_64
QEMU_CPU  ?= qemu64
QEMU_MEM  ?= 512M
TIMEOUT_S ?= 30

# Reproducible builds (ADR-0003).
SOURCE_DATE_EPOCH ?= $(shell git log -1 --format=%ct 2>/dev/null || echo 0)
export SOURCE_DATE_EPOCH

KCFLAGS := -std=c11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
           -mno-red-zone -mcmodel=kernel -mgeneral-regs-only -fno-omit-frame-pointer \
           -Wall -Wextra -Werror -Wdate-time -ffile-prefix-map=$(SRCDIR)=. -Isdk/include

KERNEL := $(BUILD)/kernel.elf
ISO    := $(BUILD)/kamos.iso
INITRD := $(BUILD)/initrd.tar

# Kernel sources (ADR-0003, ADR-0002). Only .c and .asm are built; there is no .S. $(sort) orders
# the lists by byte value, so the link order does not depend on the file system.
KSRC_DIRS ?= boot arch/x86_64 hal ntoskrnl
KSRC_C    := $(sort $(shell find $(KSRC_DIRS) -name '*.c' 2>/dev/null))
KSRC_ASM  := $(sort $(shell find $(KSRC_DIRS) -name '*.asm' 2>/dev/null))
KOPT      ?= -O2 -g
KASMFLAGS ?= -g -F dwarf
# 1 = link the libgcc built with -mno-red-zone (ADR-0003: only if the kernel needs it).
KERNEL_LIBGCC ?= 0

ifeq ($(strip $(KSRC_C) $(KSRC_ASM)),)
# Until A03 delivers Phase 1 there is no kernel source: build the placeholder.
KERNEL_KIND := placeholder
KERNEL_ASM  := tools/image/placeholder/entry.asm
KERNEL_LD   := tools/image/placeholder/placeholder.ld
KOBJS       := $(BUILD)/entry.o
KLIBS       :=
else
# The entry symbol is read from ENTRY() in the linker script (ADR-0002); it is not set here.
KERNEL_KIND := sources
KERNEL_LD   ?= arch/x86_64/boot/kernel.ld
ifeq ($(wildcard $(KERNEL_LD)),)
$(error kernel sources found but the linker script $(KERNEL_LD) does not exist)
endif
KOBJS := $(sort $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(KSRC_C) $(KSRC_ASM))))
ifeq ($(KERNEL_LIBGCC),1)
KLIBS := $(shell $(CC) $(KCFLAGS) -print-libgcc-file-name)
else
KLIBS :=
endif
endif

QEMU_BASE := $(QEMU) -machine q35 -cpu $(QEMU_CPU) -m $(QEMU_MEM) -cdrom $(ISO) -no-reboot

.PHONY: all toolchain-check headers-check iso run run-serial debug test clean package

all: $(KERNEL) headers-check

toolchain-check:
	@tools/check-toolchain.sh "$(CROSS)"

# Every public header must compile alone with the kernel flags.
headers-check: | $(BUILD)
	@for h in sdk/include/kamos/*.h; do \
	  printf '#include <kamos/%s>\n' "$$(basename $$h)" | \
	    $(CC) $(KCFLAGS) -x c -fsyntax-only - || { echo "FAIL $$h"; exit 1; }; \
	done; echo "headers-check: OK"

$(BUILD):
	mkdir -p $@

$(BUILD)/entry.o: $(KERNEL_ASM) | $(BUILD)
	$(NASM) -f elf64 -o $@ $<

$(BUILD)/obj/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(KCFLAGS) $(KOPT) -MMD -MP -c -o $@ $<

$(BUILD)/obj/%.asm.o: %.asm
	@mkdir -p $(@D)
	$(NASM) -f elf64 $(KASMFLAGS) -MD $(@:.o=.d) -MP -MT $@ -o $@ $<

$(KERNEL): $(KOBJS) $(KERNEL_LD)
	$(LD) -n -nostdlib --build-id=none -T $(KERNEL_LD) -o $@ $(KOBJS) $(KLIBS)
	grub-file --is-x86-multiboot2 $@
	@! readelf -d $@ 2>/dev/null | grep -q NEEDED
	@echo "kernel: $@ ($(KERNEL_KIND), Multiboot2 OK, no dynamic dependencies)"

-include $(KOBJS:.o=.d)

$(INITRD): | $(BUILD)
	tar --sort=name --mtime=@$(SOURCE_DATE_EPOCH) --owner=0 --group=0 --numeric-owner \
	    --format=ustar -cf $@ -T /dev/null

iso: $(ISO)
$(ISO): $(KERNEL) $(INITRD) boot/grub/grub.cfg
	rm -rf $(BUILD)/iso && mkdir -p $(BUILD)/iso/boot/grub
	cp $(KERNEL) $(INITRD) $(BUILD)/iso/boot/
	cp boot/grub/grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	find $(BUILD)/iso -exec touch -h -d @$(SOURCE_DATE_EPOCH) {} +
	grub-mkrescue -o $@ $(BUILD)/iso --modification-date=$$(date -u -d @$(SOURCE_DATE_EPOCH) +%Y%m%d%H%M%S00) \
	  -- -iso_nowtime =$(SOURCE_DATE_EPOCH) -alter_date_r b =$(SOURCE_DATE_EPOCH) / -- \
	  2>$(BUILD)/grub-mkrescue.log || \
	  { cat $(BUILD)/grub-mkrescue.log; exit 1; }
	@echo "iso: $@"

run: $(ISO)
	$(QEMU_BASE) -serial stdio

run-serial: $(ISO)
	timeout $(TIMEOUT_S) $(QEMU_BASE) -display none -serial file:$(BUILD)/serial.log || true
	cat $(BUILD)/serial.log

debug: $(ISO)
	$(QEMU_BASE) -serial stdio -s -S

# Boots headless and exits through isa-debug-exit (ADR-0010). 33 = all tests passed.
test: $(ISO)
	@tools/qemu/run-test.sh "$(ISO)" "$(BUILD)/serial.log" "$(TIMEOUT_S)" "$(QEMU_CPU)" "$(QEMU_MEM)"

package: $(ISO)
	cd $(BUILD) && sha256sum kamos.iso kernel.elf > SHA256SUMS && cat SHA256SUMS

clean:
	rm -rf $(BUILD)
