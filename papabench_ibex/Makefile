# PapaBench on the standard Ibex reference system (Secure-Ibex, Verilator).
#
#   make PROG=fbw            build build/fbw/fbw.elf
#   make PROG=autopilot      build build/autopilot/autopilot.elf
#   make PROG=fbw run        build and run on Verilator (results printed from
#                            build/fbw/reference_system.log)
#   make all-progs           build both
#   make sim                 build the Verilator simulator into build/sim/ from
#                            the Secure-Ibex sources + hw/patches (done by 'run'
#                            if missing)
#   make PROG=fbw vmem       also produce a .vmem image (not needed by Verilator)
#   make hwtest              smoke test of the SoC patches (TimerC IRQ 20,
#                            20-bit PWM) on the simulator, with a waveform
#
# Each program runs its own upstream scheduler; its time base is the Ibex
# machine timer (0x80000000, IRQ 7), one tick every TICK_CYCLES cycles.
#
# Variables:
#   TICKS        scheduler ticks to run before printing results (default 61,
#                ~1 s of flight); the Autopilot adds its 30 startup ticks
#   TICK_CYCLES  timer period in cycles (default 819200 = AVR Timer2 period,
#                16.384 ms, at 50 MHz)
#   SIM          path to Vreference_system (default: the one 'make sim' builds
#                from the submodule; set it to use another build)
#   FUSESOC      FuseSoC executable (default 'fusesoc'; its venv must be active,
#                the Secure-Ibex tool check runs 'pip3 show edalize')
#   OPT          optimisation level (default -Os)
#
# Nothing is written under bench/ or Secure-Ibex/: sources are read from
# there, objects go to build/<prog>/, the simulator to build/sim/. SoC hardware
# changes are patches (hw/patches/) applied to a copy in build/sim/hw/.

.DEFAULT_GOAL := all

PROG        ?= fbw
TICKS       ?= 61
TICK_CYCLES ?= 819200
OPT         ?= -Os

HERE        := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT        := $(abspath $(HERE)/..)
PB          := $(ROOT)/bench/parallel/PapaBench
PB_AIR      := $(PB)/sw/airborne
IBEX_SW     := $(ROOT)/Secure-Ibex/sw/Standard_Only_SW/Standard_Tests
IBEX_COMMON := $(IBEX_SW)/common
IBEX_CRT    := $(IBEX_SW)/hello_test/crt0.S
# Our layout: the whole 128 KiB RAM (the stock 56 KiB link.ld is too small)
LINKER_SCRIPT ?= $(HERE)/link.ld
SIM_DEFAULT := $(HERE)/build/sim/sim_ibex-verilator/Vreference_system
SIM ?= $(SIM_DEFAULT)
FUSESOC ?= fusesoc

BUILD := $(HERE)/build/$(PROG)
ELF   := $(BUILD)/$(PROG).elf

CC      := riscv32-unknown-elf-gcc
NM      := riscv32-unknown-elf-nm
OBJDUMP := riscv32-unknown-elf-objdump
OBJCOPY := riscv32-unknown-elf-objcopy
SIZE    := riscv32-unknown-elf-size

# ---------------------------------------------------------------- flags ---

ARCH_FLAGS := -march=rv32im -mabi=ilp32 -mcmodel=medany
BASE_CFLAGS := $(ARCH_FLAGS) $(OPT) -g -static -ffreestanding -nostdlib \
  -nostartfiles -ffunction-sections -fdata-sections -Wall

# PapaBench sources: AVR device header, 2000s-era C semantics
# (-fcommon: tentative definitions of ck_a/ck_b in two files;
#  -fgnu89-inline: non-static 'inline' functions in autopilot/main.c)
# PAPABENCH_TICK_BIT = TOV2 (6 on both devices)
# PAPABENCH_SPDR_ADDR = SPDR = _SFR_IO8(0x0F) on both devices
PB_CFLAGS := $(BASE_CFLAGS) -fcommon -fgnu89-inline -Wno-unknown-pragmas \
  -DPAPABENCH_TICK_BIT=6 -DPAPABENCH_SPDR_ADDR=0x2F \
  -I$(HERE)/include -I$(PB)/sw/include -I$(PB)/sw/var/include

# Ibex side: Secure-Ibex common library + harness
IBEX_CFLAGS := $(BASE_CFLAGS) -I$(IBEX_COMMON) -I$(HERE)/harness \
  -DPAPABENCH_TICKS=$(TICKS) -DPAPABENCH_TICK_CYCLES=$(TICK_CYCLES)

LDFLAGS := $(ARCH_FLAGS) -static -nostdlib -nostartfiles -Wl,--gc-sections \
  -T $(LINKER_SCRIPT) -Wl,-Map=$(BUILD)/$(PROG).map
LIBS := -lgcc

# ------------------------------------------------------ per-program sets ---

ifeq ($(PROG),fbw)
# TIFR = _SFR_IO8(0x38) on ATmega8
PB_CFLAGS += -D__AVR_ATmega8__ -DPAPABENCH_TIFR_ADDR=0x58 \
  -I$(PB_AIR)/fly_by_wire
PB_CFLAGS += -I$(PB)/arch/include/avr -I$(PB)/arch/include/avr/arch
PB_SRCS := $(addprefix $(PB_AIR)/fly_by_wire/, \
  adc_fbw.c main.c ppm.c servo.c spi.c uart.c)
PATCHED :=
MAIN_OBJ := main.o
GLUE := $(HERE)/harness/fbw_glue.c
else ifeq ($(PROG),autopilot)
# TIFR = _SFR_IO8(0x36) on ATmega128.
# autopilot headers first: fly_by_wire has spi.h/uart.h/timer.h with the same
# names; fly_by_wire is only needed for link_autopilot.h
PB_CFLAGS += -D__AVR_ATmega128__ -DPAPABENCH_TIFR_ADDR=0x56 -DUBX \
  -I$(PB_AIR)/autopilot -I$(PB_AIR)/fly_by_wire
PB_CFLAGS += -I$(PB)/arch/include/avr -I$(PB)/arch/include/avr/arch
# not built: ad7714.c and gps_sirf.c (unused, do not compile);
# main.c is built from a patched copy
PB_SRCS := $(addprefix $(PB_AIR)/autopilot/, \
  adc.c estimator.c gps_ubx.c if_calib.c infrared.c link_fbw.c mainloop.c \
  modem.c nav.c pid.c spi.c uart.c) $(PB)/sw/lib/c/math.c
PATCHED := $(BUILD)/patched/main.c
MAIN_OBJ := mainloop.o
GLUE := $(HERE)/harness/autopilot_glue.c

# Upstream bug fix applied to a copy; bench/ is never modified
$(BUILD)/patched/main.c: $(PB_AIR)/autopilot/main.c \
  $(HERE)/patches/autopilot_main_modeupdate.patch
	@mkdir -p $(dir $@)
	cp $< $@
	patch -s $@ < $(word 2,$^)
else
$(error PROG must be fbw or autopilot)
endif

IBEX_SRCS := $(IBEX_COMMON)/reference_system_common.c \
  $(IBEX_COMMON)/timer.c $(IBEX_COMMON)/uart.c

PB_NAMES     := $(notdir $(PB_SRCS) $(PATCHED))
PLAIN_OBJS   := $(patsubst %.c,$(BUILD)/plain/%.o,$(PB_NAMES))
PB_OBJS      := $(patsubst %.c,$(BUILD)/pb/%.o,$(PB_NAMES))
GLUE_OBJ     := $(BUILD)/glue/$(notdir $(GLUE:.c=.o))
# AVR peripheral models (harness/periph.h): generic part + this program's
PERIPH_SRCS  := $(HERE)/harness/periph.c $(HERE)/harness/$(PROG)_periph.c
PERIPH_OBJS  := $(patsubst %.c,$(BUILD)/glue/%.o,$(notdir $(PERIPH_SRCS)))
IBEX_OBJS    := $(patsubst %.c,$(BUILD)/ibex/%.o,$(notdir $(IBEX_SRCS)))
HARNESS_OBJS := $(addprefix $(BUILD)/harness/,harness.o runtime.o calib.o)
CRT_OBJ      := $(BUILD)/ibex/crt0.o
OBJS := $(PB_OBJS) $(GLUE_OBJ) $(PERIPH_OBJS) $(IBEX_OBJS) $(HARNESS_OBJS) \
  $(CRT_OBJ)

# Upstream main() is kept but renamed; the harness provides main() and calls it
$(BUILD)/plain/$(MAIN_OBJ) $(BUILD)/pb/$(MAIN_OBJ): \
  PB_CFLAGS += -Dmain=papabench_upstream_main

# ------------------------------------------------------ instrumentation ---
#
# Only the first/last function of each task gets -finstrument-functions
# hooks. TASKS_TXT: those names, parsed from the PAPABENCH_TASK( ... ) lines
# of the glue file. EXCLUDE_TXT: every other function defined in the PapaBench
# objects (static and inline ones included), obtained with nm from a plain
# build. GCC matches the exclude list by substring, so an entry contained in a
# task name would silently drop that task's hooks: the build stops instead.

TASKS_TXT   := $(BUILD)/instrumented.txt
FUNCS_TXT   := $(BUILD)/functions.txt
EXCLUDE_TXT := $(BUILD)/exclude.txt

$(TASKS_TXT): $(GLUE)
	@mkdir -p $(dir $@)
	sed -n 's/^[[:space:]]*PAPABENCH_TASK( *"[^"]*", *\([A-Za-z_0-9]*\), *\([A-Za-z_0-9]*\) *).*/\1\n\2/p' \
	  $< | sort -u > $@

$(EXCLUDE_TXT): $(PLAIN_OBJS) $(TASKS_TXT)
	$(NM) --defined-only -P $(PLAIN_OBJS) | \
	  awk 'NF >= 2 && $$2 ~ /^[Tt]$$/ && $$1 !~ /^\./ { print $$1 }' | \
	  sort -u > $(FUNCS_TXT)
	@missing=$$(grep -vxF -f $(FUNCS_TXT) $(TASKS_TXT)); \
	  if [ -n "$$missing" ]; then \
	    echo "ERROR: task functions not defined in PapaBench objects:" $$missing; \
	    exit 1; fi
	grep -vxF -f $(TASKS_TXT) $(FUNCS_TXT) > $@
	@bad=$$(awk 'NR == FNR { t[ $$0 ]; next } \
	  { for ( n in t ) if ( index( n, $$0 ) ) print $$0 " (in " n ")" }' \
	  $(TASKS_TXT) $@); \
	  if [ -n "$$bad" ]; then \
	    echo "ERROR: exclude entries are substrings of task names:" $$bad; \
	    exit 1; fi

INSTR_FLAGS = -finstrument-functions \
  -finstrument-functions-exclude-function-list=$$(paste -sd, $(EXCLUDE_TXT))

vpath %.c $(sort $(dir $(PB_SRCS)))

# ------------------------------------------------------------------ rules ---

.PHONY: all all-progs sim vmem run disassemble clean distclean

all: $(ELF)
	@$(SIZE) $(ELF)

vmem: $(BUILD)/$(PROG).vmem

all-progs:
	$(MAKE) PROG=fbw
	$(MAKE) PROG=autopilot

$(ELF): $(OBJS) $(LINKER_SCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)

$(BUILD)/$(PROG).bin: $(ELF)
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(PROG).vmem: $(BUILD)/$(PROG).bin
	srec_cat $< -binary -offset 0x0000 -fill 0x00 -within $< -binary \
	  -range-padding 4 -byte-swap 4 -o $@ -vmem

disassemble: $(BUILD)/$(PROG).dis
$(BUILD)/$(PROG).dis: $(ELF)
	$(OBJDUMP) -fhSD $< > $@

# PapaBench, plain (only for nm): keep static/inline functions so they are
# listed and excluded from instrumentation
$(BUILD)/plain/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PB_CFLAGS) -fkeep-inline-functions -fkeep-static-functions \
	  -w -c $< -o $@

# PapaBench, instrumented (linked)
$(BUILD)/pb/%.o: %.c $(EXCLUDE_TXT)
	@mkdir -p $(dir $@)
	$(CC) $(PB_CFLAGS) $(INSTR_FLAGS) -MMD -c $< -o $@

ifeq ($(PROG),autopilot)
$(BUILD)/plain/main.o: $(PATCHED)
	@mkdir -p $(dir $@)
	$(CC) $(PB_CFLAGS) -fkeep-inline-functions -fkeep-static-functions \
	  -w -c $< -o $@

$(BUILD)/pb/main.o: $(PATCHED) $(EXCLUDE_TXT)
	@mkdir -p $(dir $@)
	$(CC) $(PB_CFLAGS) $(INSTR_FLAGS) -MMD -c $< -o $@
endif

$(GLUE_OBJ): $(GLUE)
	@mkdir -p $(dir $@)
	$(CC) $(PB_CFLAGS) -I$(HERE)/harness -MMD -c $< -o $@

$(BUILD)/glue/%.o: $(HERE)/harness/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PB_CFLAGS) -I$(HERE)/harness -MMD -c $< -o $@

$(BUILD)/ibex/%.o: $(IBEX_COMMON)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(IBEX_CFLAGS) -MMD -c $< -o $@

$(CRT_OBJ): $(IBEX_CRT)
	@mkdir -p $(dir $@)
	$(CC) $(IBEX_CFLAGS) -MMD -c $< -o $@

$(BUILD)/harness/runtime.o: $(HERE)/harness/runtime.c
	@mkdir -p $(dir $@)
	$(CC) $(IBEX_CFLAGS) -fno-builtin -fno-tree-loop-distribute-patterns \
	  -MMD -c $< -o $@

# Same hooks as the tasks, to measure their overhead
$(BUILD)/harness/calib.o: $(HERE)/harness/calib.c
	@mkdir -p $(dir $@)
	$(CC) $(IBEX_CFLAGS) -finstrument-functions -MMD -c $< -o $@

# harness.o depends on TICKS/TICK_CYCLES: rebuild it every time
$(BUILD)/harness/harness.o: $(HERE)/harness/harness.c FORCE
	@mkdir -p $(dir $@)
	$(CC) $(IBEX_CFLAGS) -MMD -c $< -o $@

FORCE: ;
.PHONY: FORCE

# Standard-Ibex Verilator model (FuseSoC target sim_ibex) with the PapaBench
# SoC patches. SOC_FILES are copied from the submodule into HW_STAGE, the
# patches in hw/patches/ are applied there in name order, our environment
# (hw/rtl/) and its stimulus tables (generated by hw/stimulus/
# gen_stimulus.py) are added under rtl/papabench/, and our two cores
# (hw/*.core: papabench:soc:reference_system[_core]) are built from it; the
# rest (Ibex, bus, RAM, timer, DPI) comes unmodified from the submodule's
# cores. --build-root keeps every output out of Secure-Ibex/. A patch that no
# longer applies (submodule updated) stops the build. Rebuilt only by
# 'make sim' or when missing.
HW_STAGE  := $(HERE)/build/sim/hw
SOC_FILES := $(addprefix rtl/system/, reference_system_core.sv debounce.sv \
  gpio.sv pwm.sv pwm_wrapper.sv uart.sv spi_host.sv spi_top.sv) \
  rtl/top_simulation/reference_system.sv \
  $(addprefix dv/verilator/, reference_system.cc reference_system.h \
  reference_system_main.cc reference_system_verilator_lint.vlt)
HW_PATCHES := $(sort $(wildcard $(HERE)/hw/patches/*.patch))

sim:
	rm -rf $(HW_STAGE)
	mkdir -p $(HW_STAGE)
	cd $(ROOT)/Secure-Ibex && cp --parents $(SOC_FILES) $(HW_STAGE)/
	cp $(HERE)/hw/*.core $(HW_STAGE)/
	mkdir -p $(HW_STAGE)/rtl/papabench
	cp $(HERE)/hw/rtl/*.sv $(HW_STAGE)/rtl/papabench/
	python3 $(HERE)/hw/stimulus/gen_stimulus.py \
	  $(HW_STAGE)/rtl/papabench/papabench_stim.svh
	for p in $(HW_PATCHES); do \
	  echo "patch $$(basename $$p)"; \
	  patch -s -p1 -N -d $(HW_STAGE) < $$p || exit 1; done
	$(FUSESOC) --cores-root=$(ROOT)/Secure-Ibex --cores-root=$(HW_STAGE) \
	  run --target=sim_ibex --setup --build --build-root=$(HERE)/build/sim \
	  papabench:soc:reference_system --verilator_options=-Wno-fatal

$(SIM_DEFAULT):
	$(MAKE) sim

# Smoke test of the SoC patches (hw/test/hw_smoke.c). Runs with -t: the PWM
# cannot be read back, so hw/test/check_pwm.sh measures its output on the
# waveform (fst2vcd). Independent of PROG.
HWT := $(HERE)/build/hwtest
HWT_SRCS := $(HERE)/hw/test/hw_smoke.c \
  $(IBEX_COMMON)/reference_system_common.c $(IBEX_COMMON)/timer.c \
  $(IBEX_COMMON)/pwm.c $(IBEX_CRT)

.PHONY: hwtest
hwtest: $(HWT)/hw_smoke.elf $(SIM)
	cd $(HWT) && $(SIM) --meminit=ram,$< -t > sim.log 2>&1; \
	  status=$$?; cat reference_system.log; [ $$status -eq 0 ] || exit $$status
	grep -q "^hw_smoke: PASS" $(HWT)/reference_system.log
	$(HERE)/hw/test/check_pwm.sh $(HWT)/sim.fst

$(HWT)/hw_smoke.elf: $(HWT_SRCS) $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	$(CC) $(IBEX_CFLAGS) $(ARCH_FLAGS) -static -nostdlib -nostartfiles \
	  -Wl,--gc-sections -T $(LINKER_SCRIPT) $(HWT_SRCS) -o $@ -lgcc

# The simulator writes in the current directory: sim.log (stdout),
# reference_system.log (SimCtrl output = results), uart0.log, pcount CSV
run: all $(SIM)
	cd $(BUILD) && $(SIM) --meminit=ram,$(ELF) > sim.log 2>&1; \
	  status=$$?; cat reference_system.log; exit $$status

clean:
	rm -rf $(BUILD)

distclean:
	rm -rf $(HERE)/build

-include $(OBJS:.o=.d)
