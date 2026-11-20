# v1.40_wip/shell: the console shell (fs_wip/planning/SHELL-SPEC.md; sessions/SHELL/NOTES.md).
#
#   make host    the shell on the host, on the emulated file system (../fs/), compiled from there into build/
#   make test    the division tests, the matcher against Pike's recursive one, then every tests/NN-name.in, diffed with NN-name.out
#   make rv32    the link-only RV32 build (stub console and fs.h calls); fails on any undefined symbol, and checks
#                that the division helpers linked are div.o's
#   make size    rv32 without and with C (C=1), the sizes; fails over the code budget (.text + .rodata <= 32 KB)
#
# Each takes CC=gcc (the default) or CC=clang; the same CC picks the host compiler. Nothing in ../fs/ is changed.

ifeq ($(origin CC),default)
CC = gcc
endif

ifeq ($(CC),clang)
HOSTCC   = clang
HOSTCXX  = clang++
RVCC     = clang --target=riscv32-unknown-elf
RVLDFLAG = -fuse-ld=lld
RVSIZE   = llvm-size
RVNM     = llvm-nm
STRFLAGS =
else
HOSTCC   = gcc
HOSTCXX  = g++
RVCC     = riscv64-unknown-elf-gcc
RVLDFLAG = -Wl,--no-warn-rwx-segments
RVSIZE   = riscv64-unknown-elf-size
RVNM     = riscv64-unknown-elf-nm
STRFLAGS = -fno-tree-loop-distribute-patterns
endif

ARCH   = $(if $(filter 1,$(C)),rv32ic_zmmul,rv32i_zmmul)
B      = build/$(CC)
RB     = $(B)/$(ARCH)
SRC    = shell.c cmds.c fmt.c str.c div.c grep.c glob.c
HDRS   = shell.h console.h ../fs/fs.h
BUDGET = 32768

HOST_CFLAGS   = -std=c99 -ffreestanding -O2 -g -Wall -Wextra
HOST_CXXFLAGS = -std=c++17 -O2 -g -Wall -Wextra
RV_CFLAGS     = -std=c99 -march=$(ARCH) -mabi=ilp32 -Os -ffreestanding -nostdlib -ffunction-sections \
                -fdata-sections -fstack-usage -Wall -Wextra

all: host

# ---------------------------------------------------------------- the host build
HOST_OBJS = $(SRC:%.c=$(B)/host/%.o) $(B)/host/console_host.o $(B)/host/main.o $(B)/host/seed.o \
            $(B)/fs/fs_emu.o $(B)/fs/stand_ins.o

host: $(B)/shell_host

$(B)/shell_host: $(HOST_OBJS)
	$(HOSTCXX) $(HOST_OBJS) -o $@

$(B)/host/%.o: %.c $(HDRS)
	@mkdir -p $(@D)
	$(HOSTCC) $(HOST_CFLAGS) $(if $(filter str.c,$<),$(STRFLAGS)) -c $< -o $@
$(B)/host/console_host.o: host/console_host.c host/console_host.h console.h
	@mkdir -p $(@D)
	$(HOSTCC) -std=c99 -D_DEFAULT_SOURCE -O2 -g -Wall -Wextra -c $< -o $@
$(B)/host/%.o: host/%.cpp host/seed.h host/console_host.h ../fs/fs.h ../fs/fs_emu.h ../fs/stand_ins.h
	@mkdir -p $(@D)
	$(HOSTCXX) $(HOST_CXXFLAGS) -c $< -o $@
$(B)/fs/%.o: ../fs/%.cpp ../fs/fs.h ../fs/fs_emu.h ../fs/stand_ins.h
	@mkdir -p $(@D)
	$(HOSTCXX) $(HOST_CXXFLAGS) -c $< -o $@

$(B)/divtest: tests/divtest.c div.c $(HDRS)
	@mkdir -p $(@D)
	$(HOSTCC) -std=c99 -O2 -Wall -Wextra tests/divtest.c div.c -o $@

$(B)/retest: tests/retest.c grep.c $(HDRS)
	@mkdir -p $(@D)
	$(HOSTCC) -std=c99 -O2 -Wall -Wextra tests/retest.c -o $@

test: $(B)/shell_host $(B)/divtest $(B)/retest
	$(B)/divtest
	$(B)/retest
	sh tests/run.sh $(B)/shell_host $(B)/out

# ---------------------------------------------------------------- the RV32 build
RV_OBJS = $(SRC:%.c=$(RB)/%.o) $(RB)/stubs.o $(RB)/crt0.o

rv32: $(RB)/shell.elf

$(RB)/shell.elf: $(RV_OBJS) rv32/link.ld
	$(RVCC) $(RV_CFLAGS) $(RVLDFLAG) -static -T rv32/link.ld -Wl,--gc-sections -Wl,-Map=$(RB)/shell.map \
	    $(RV_OBJS) -o $@
	@for s in $$($(RVNM) $@ | awk '$$3 ~ /^__(u)?(div|mod)(si|di)3$$/ { print $$3 }'); do \
	    $(RVNM) $(RB)/div.o | grep -q " T $$s$$" || { echo "$$s is not div.o's"; rm -f $@; exit 1; }; \
	done; echo "$@: the division helpers linked are div.o's"

$(RB)/%.o: %.c $(HDRS)
	@mkdir -p $(@D)
	$(RVCC) $(RV_CFLAGS) $(if $(filter str.c,$<),$(STRFLAGS)) -c $< -o $@
$(RB)/stubs.o: rv32/stubs.c $(HDRS)
	@mkdir -p $(@D)
	$(RVCC) $(RV_CFLAGS) -c $< -o $@
$(RB)/crt0.o: rv32/crt0.S
	@mkdir -p $(@D)
	$(RVCC) -march=$(ARCH) -mabi=ilp32 -c $< -o $@

# the sizes of one build; fails over the budget
size1: $(RB)/shell.elf
	@echo "== $(CC), $(ARCH)"
	@$(RVSIZE) -A $< | awk '$$1 ~ /^\.(text|rodata|srodata|data|sdata|bss|sbss)/ { s[$$1] += $$2 } \
	    END { t = s[".text"]; r = s[".rodata"] + s[".srodata"]; d = s[".data"] + s[".sdata"]; b = s[".bss"] + s[".sbss"]; \
	          printf "  .text %d, .rodata %d (code %d of $(BUDGET)), .data %d, .bss %d\n", t, r, t + r, d, b; \
	          if (t + r > $(BUDGET)) { print "  OVER THE BUDGET"; exit 1 } }'
	@echo "  per object (text data bss, before --gc-sections):"
	@$(RVSIZE) $(SRC:%.c=$(RB)/%.o) | awk 'NR > 1 { printf "    %-28s %6d %6d %6d\n", $$6, $$1, $$2, $$3 }'
	@echo "  the deepest stack frames (bytes):"
	@cat $(RB)/*.su | sort -t'	' -k2 -n -r | head -5 | sed 's/^/    /'

size:
	@$(MAKE) --no-print-directory size1 C=
	@$(MAKE) --no-print-directory size1 C=1

clean:
	rm -rf build

.PHONY: all host test rv32 size size1 clean
