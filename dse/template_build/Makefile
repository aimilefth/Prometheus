#
# Project: GEMM on VCK190 base DFX platform
#

SHELL := /bin/bash

############################## Help Section ##############################
.PHONY: help all_full clean cleanall test run build host xclbin emconfig hls all

help::
	@echo "Makefile Usage:"
	@echo "  make all TARGET=<sw_emu/hw_emu/hw> HOST_ARCH=<aarch64/x86> [DEVICE=<platform.xpfm>] [EDGE_COMMON_SW=<versal common image path>]"
	@echo ""
	@echo "  make hls TARGET=<sw_emu/hw_emu/hw> [DEVICE=<platform.xpfm>]"
	@echo "      Run HLS (compile only) and produce kernel .xo(s) only."
	@echo ""
	@echo "  make build TARGET=<sw_emu/hw_emu/hw> HOST_ARCH=<aarch64/x86> [DEVICE=<platform.xpfm>]"
	@echo "      Build kernel xclbin."
	@echo ""
	@echo "  make host HOST_ARCH=<aarch64/x86> [EDGE_COMMON_SW=<versal common image path>]"
	@echo "      Build host executable."
	@echo ""
	@echo "  make run TARGET=<sw_emu/hw_emu/hw> HOST_ARCH=<aarch64/x86>"
	@echo "      Run emulation (x86) or launch emulator (aarch64) depending on TARGET."
	@echo ""
	@echo "  make sd_card TARGET=<sw_emu/hw_emu/hw> HOST_ARCH=<aarch64/x86> [EDGE_COMMON_SW=<versal common image path>]"
	@echo "      Create package (for aarch64) including rootfs/Image, host exe, run_app.sh, and data folders."
	@echo ""
	@echo "  make clean / make cleanall"

############################## Project Variables ##############################
TARGET     ?= hw
HOST_ARCH  ?= aarch64

# Default platform (as requested)
PLATFORM_REPO_PATHS ?= /opt/Xilinx/Vitis/2023.2/base_platforms
DEVICE ?= $(PLATFORM_REPO_PATHS)/xilinx_vck190_base_dfx_202320_1/xilinx_vck190_base_dfx_202320_1.xpfm

EDGE_COMMON_SW_PATH ?= /opt/petalinux_files
EDGE_COMMON_SW ?= ${EDGE_COMMON_SW_PATH}/xilinx-versal-common-v2023.2

# PetaLinux SDK sysroot used for cross-compiling the host (has XRT headers)
SYSROOT_PATH ?= /opt/petalinux/2023.2
SYSROOT      ?= $(SYSROOT_PATH)/sysroots/cortexa72-cortexa53-xilinx-linux


# Kernel / Host naming
KERNEL_NAME ?= kernel_nlp

KERNEL_DIR  ?= src
HOST_DIR    ?= benchmark_host

KERNEL_CFG  ?= hls.cfg
LINK_CFG    ?= system.cfg

# Sources
KERNEL_SRCS ?= $(KERNEL_DIR)/$(KERNEL_NAME).cpp
HOST_SRCS   ?= $(HOST_DIR)/tb_gemm.cpp $(HOST_DIR)/host_gemm_fpga.cpp

# Output names
EXECUTABLE  ?= ./$(KERNEL_NAME)_host.exe

# Friendly name from device for folder names
device2xsa = $(strip $(patsubst %.xpfm,%,$(shell basename $(DEVICE))))
XSA := $(call device2xsa,$(DEVICE))

TEMP_DIR  := ./_x.$(TARGET).$(XSA)
BUILD_DIR := ./build_dir.$(TARGET).$(XSA)

KERNEL_XO := $(TEMP_DIR)/$(KERNEL_NAME).xo
# Detect DFX platform (rp.xsa exists next to the .xpfm)
DFX_RP_XSA := $(wildcard $(dir $(DEVICE))/hw/*rp.xsa)

ifneq ($(wildcard $(DFX_RP_XSA)),)
  # DFX platforms require link output to be .xsa
  LINK_OUTPUT := $(BUILD_DIR)/$(KERNEL_NAME).xsa
else
  # Non-DFX platforms typically link to an intermediate .xclbin
  LINK_OUTPUT := $(BUILD_DIR)/$(KERNEL_NAME).link.xclbin
endif

FINAL_XCLBIN := $(BUILD_DIR)/$(KERNEL_NAME).xclbin

BINARY_CONTAINERS := $(FINAL_XCLBIN)

# Packaging / run scripts (SoC flow)
RUN_APP_SCRIPT = ./run_app.sh
PACKAGE_OUT    = ./package.$(TARGET)
LAUNCH_EMULATOR = $(PACKAGE_OUT)/launch_$(TARGET).sh

# Default runtime args for host (override if your host expects more)
CMD_ARGS ?= $(FINAL_XCLBIN)

############################## Toolchain ##############################
VPP ?= v++
ECHO := @echo
CP := cp -rf
RM := rm -f
RMDIR := rm -rf

############################## Kernel Build Flags ##############################
VPP_FLAGS += -t $(TARGET) --platform $(DEVICE) --save-temps --optimize 2 --report_level estimate

# Add debug for emulation
ifneq ($(TARGET), hw)
VPP_FLAGS += -g
endif

KERNEL_INC_FLAGS += -I$(KERNEL_DIR)

############################## Host Build Flags ##############################
CXXFLAGS += -Wall -O0 -g -std=c++17 -fmessage-length=0
INC_FLAGS += -I./ -I./$(KERNEL_DIR) -I./$(HOST_DIR)

# x86 vs aarch64 host toolchain setup
ifeq ($(HOST_ARCH), x86)

CXX ?= g++

check-xrt:
ifndef XILINX_XRT
	$(error XILINX_XRT variable is not set. Please set it and rerun.)
endif

# Typical x86 XRT include/lib
INC_FLAGS += -I$(XILINX_XRT)/include
LDFLAGS   += -L$(XILINX_XRT)/lib -lOpenCL -lxrt_coreutil -pthread -lrt -ldl

else  # aarch64

check-xrt:
ifndef XILINX_VITIS
	$(error XILINX_VITIS variable is not set. Please set it and rerun.)
endif
ifndef EDGE_COMMON_SW
	$(error EDGE_COMMON_SW variable is not set (e.g. /opt/petalinux_files/xilinx-versal-common-v2023.2).)
endif
ifndef SYSROOT
	$(error SYSROOT is not set. Expected: /opt/petalinux/2023.2/sysroots/cortexa72-cortexa53-xilinx-linux)
endif

# rootfs/image still come from versal-common
ROOTFS  := $(EDGE_COMMON_SW)/rootfs.ext4
IMAGE   := $(EDGE_COMMON_SW)/Image

# IMPORTANT: force cross compiler (do NOT use ?=)
CXX := $(XILINX_VITIS)/gnu/aarch64/lin/aarch64-linux/bin/aarch64-linux-gnu-g++

# Match the old working host compile behavior
CXXFLAGS += -std=c++17 -Wno-int-to-pointer-cast --sysroot=$(SYSROOT)

# Make sure xrt/xrt_device.h can be found
INC_FLAGS += -I$(SYSROOT)/usr/include \
            -I$(SYSROOT)/usr/include/xrt \
            -I$(XILINX_VITIS)/aietools/include \
            -I$(XILINX_VITIS)/include

# Match the old working link line (important if your host uses ADF/XRT APIs)
LDFLAGS += -ladf_api_xrt -lgcc -lc -lxrt_coreutil -lxilinxopencl \
           -lpthread -lrt -ldl -lcrypt -lstdc++ \
           -L$(SYSROOT)/usr/lib --sysroot=$(SYSROOT) \
           -L$(XILINX_VITIS)/aietools/lib/aarch64.o

endif

############################## Essential Checks ##############################
check-vitis:
ifndef XILINX_VITIS
	$(error XILINX_VITIS variable is not set. Please set it and rerun.)
endif

check-devices:
ifndef DEVICE
	$(error DEVICE not set. Please set DEVICE=<platform.xpfm> and rerun.)
endif

############################## Targets ##############################
all_full: check-devices $(EXECUTABLE) $(BINARY_CONTAINERS) emconfig

all:
	@$(MAKE) all_full; \
	RET=$$?; \
	$(MAKE) report; \
	exit $$RET

all_no_hls:
	@$(MAKE) NO_HLS=1 all_full; \
	RET=$$?; \
	$(MAKE) NO_HLS=1 report; \
	exit $$RET

host: $(EXECUTABLE)

build: check-vitis $(BINARY_CONTAINERS)

xclbin: build

# HLS-only: compile kernel(s) to .xo(s), no link, no package, no host build
hls: check-vitis check-devices $(KERNEL_XO)
	@echo "INFO: HLS compile complete -> $(KERNEL_XO)"

############################## Directory Rules ##############################
$(TEMP_DIR):
	mkdir -p $(TEMP_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

############################## Kernel Rules ##############################
# Rebuild kernel if headers change
KERNEL_DEPS := $(wildcard $(KERNEL_DIR)/*.h) $(wildcard $(KERNEL_DIR)/*.hpp)

ifeq ($(NO_HLS),1)
$(KERNEL_XO):
	@test -f $@ || { \
		echo "ERROR: Prebuilt kernel object not found: $@"; \
		echo "Run 'make hls' or 'make all' first."; \
		exit 1; \
	}
else
$(KERNEL_XO): $(KERNEL_SRCS) $(KERNEL_DEPS) | $(TEMP_DIR)
	$(VPP) $(VPP_FLAGS) -c -k $(KERNEL_NAME) --config $(KERNEL_CFG) --temp_dir $(TEMP_DIR) $(KERNEL_INC_FLAGS) -o $@ $(KERNEL_SRCS)
endif

$(LINK_OUTPUT): $(KERNEL_XO) | $(BUILD_DIR)
	$(VPP) $(VPP_FLAGS) -l --config $(LINK_CFG) --temp_dir $(TEMP_DIR) $(VPP_LDFLAGS) -o $@ $^

# Keep a stable “final” xclbin name in BUILD_DIR
# For x86: optionally run v++ -p to create a packaged xclbin (still useful for emu)
# For aarch64: we still create FINAL_XCLBIN, but packaging happens in sd_card
$(FINAL_XCLBIN): $(LINK_OUTPUT) | $(BUILD_DIR)
ifeq ($(HOST_ARCH), x86)
	$(VPP) -p $< -t $(TARGET) --platform $(DEVICE) --package.out_dir $(PACKAGE_OUT) -o $@
else
	# For embedded, keep a plain xclbin in BUILD_DIR for convenience
	# (the real SD-card package is produced by the sd_card target below)
	$(VPP) -p $< -t $(TARGET) --platform $(DEVICE) --package.out_dir $(PACKAGE_OUT) -o $@
endif


############################## Host Rules ##############################
HOST_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(HOST_SRCS))

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INC_FLAGS) -c -o $@ $<

$(EXECUTABLE): $(HOST_OBJS) | check-xrt
	$(CXX) -o $@ $^ $(LDFLAGS)

############################## Emulation Config ##############################
EMCONFIG_DIR = $(TEMP_DIR)
emconfig: $(EMCONFIG_DIR)/emconfig.json

$(EMCONFIG_DIR)/emconfig.json: | $(TEMP_DIR)
	emconfigutil --platform $(DEVICE) --od $(EMCONFIG_DIR)

############################## Run / Test ##############################
run: all_full
ifeq ($(TARGET),$(filter $(TARGET),sw_emu hw_emu))
ifeq ($(HOST_ARCH), x86)
	$(CP) $(EMCONFIG_DIR)/emconfig.json .
	XCL_EMULATION_MODE=$(TARGET) $(EXECUTABLE) $(CMD_ARGS)
else
	$(LAUNCH_EMULATOR) -run-app $(RUN_APP_SCRIPT) | tee run_app.log; exit $${PIPESTATUS[0]}
endif
else
ifeq ($(HOST_ARCH), x86)
	$(EXECUTABLE) $(CMD_ARGS)
else
	@echo "INFO: For hw on board, copy package.$(TARGET) contents to SD card and run on target."
endif
endif

test: $(EXECUTABLE)
ifeq ($(TARGET),$(filter $(TARGET),sw_emu hw_emu))
ifeq ($(HOST_ARCH), x86)
	XCL_EMULATION_MODE=$(TARGET) $(EXECUTABLE) $(CMD_ARGS)
else
	$(LAUNCH_EMULATOR) -run-app $(RUN_APP_SCRIPT) | tee run_app.log; exit $${PIPESTATUS[0]}
endif
else
ifeq ($(HOST_ARCH), x86)
	$(EXECUTABLE) $(CMD_ARGS)
else
	@echo "INFO: Please run on target board (hw) or use hw_emu/sw_emu."
endif
endif

############################## SD Card / Packaging ##############################
# Auto-include data directories if they exist
SD_DIRS :=
ifneq ($(wildcard data),)
SD_DIRS += --package.sd_dir data
endif

sd_card: $(BINARY_CONTAINERS) | $(EXECUTABLE) gen_run_app
ifneq ($(HOST_ARCH), x86)
	$(VPP) -p $(LINK_OUTPUT) -t $(TARGET) --platform $(DEVICE) \
		--package.out_dir $(PACKAGE_OUT) \
		--package.rootfs $(ROOTFS) \
		--package.kernel_image $(IMAGE) \
		--package.boot_mode=sd \
		$(SD_DIRS) \
		--package.sd_file xrt.ini \
		--package.sd_file $(RUN_APP_SCRIPT) \
		--package.sd_file $(EXECUTABLE) \
		-o $(KERNEL_NAME).xclbin
else
	@echo "INFO: HOST_ARCH=x86 => skipping embedded sd_card packaging."
endif

gen_run_app:
ifneq ($(HOST_ARCH), x86)
	$(RM) $(RUN_APP_SCRIPT)
	$(ECHO) 'export LD_LIBRARY_PATH=/mnt:/tmp:$$LD_LIBRARY_PATH' >> $(RUN_APP_SCRIPT)
	$(ECHO) 'export PATH=$$PATH:/sbin:$$PATH' >> $(RUN_APP_SCRIPT)
	$(ECHO) 'export XILINX_XRT=/usr' >> $(RUN_APP_SCRIPT)
ifeq ($(TARGET),$(filter $(TARGET),sw_emu hw_emu))
	$(ECHO) 'export XCL_EMULATION_MODE=$(TARGET)' >> $(RUN_APP_SCRIPT)
endif
	$(ECHO) '$(notdir $(EXECUTABLE)) $(KERNEL_NAME).xclbin' >> $(RUN_APP_SCRIPT)
	$(ECHO) 'return_code=$$?' >> $(RUN_APP_SCRIPT)
	$(ECHO) 'if [ $$return_code -ne 0 ]; then' >> $(RUN_APP_SCRIPT)
	$(ECHO) '  echo "ERROR: host run failed, RC=$$return_code"' >> $(RUN_APP_SCRIPT)
	$(ECHO) 'fi' >> $(RUN_APP_SCRIPT)
	$(ECHO) 'echo "INFO: host run completed."' >> $(RUN_APP_SCRIPT)
	chmod +x $(RUN_APP_SCRIPT)
endif

############################## Cleaning ##############################
clean:
	-$(RMDIR) $(EXECUTABLE) $(HOST_OBJS)
	-$(RMDIR) profile_* TempConfig system_estimate.xtxt *.rpt *.csv
	-$(RMDIR) *v++* .Xil emconfig.json dltmp* xmltmp* *.log *.jou *.wcfg *.wdb
	-$(RMDIR) $(TEMP_DIR) $(BUILD_DIR)

cleanall: clean
	-$(RMDIR) build_dir* package.* _x* emulation qemu-memory-* sd_card* *.xclbin.run_summary
	-$(RMDIR) run_app.sh run_app.log *.link.xclbin .ipcache

############################## Report Generation Target ##############################
.PHONY: report
report:
	@echo "========================================================="
	@echo " Backing up requested reports and outputs..."
	@echo "========================================================="
	@mkdir -p .report_backup/$(TEMP_DIR)/link/int
	@mkdir -p .report_backup/$(TEMP_DIR)/$(KERNEL_NAME)/$(KERNEL_NAME)/$(KERNEL_NAME)/solution/syn/report

	@# 2) Backup kernel_nlp.xclbin (both root and build_dir versions) and kernel_nlp_host.exe
	@cp -a $(KERNEL_NAME).xclbin .report_backup/ 2>/dev/null || true
	@mkdir -p .report_backup/$(BUILD_DIR)
	@cp -a $(FINAL_XCLBIN) .report_backup/$(BUILD_DIR)/ 2>/dev/null || true
	@cp -a $(EXECUTABLE) .report_backup/ 2>/dev/null || true

	@# ---> MINIMAL ADDITIONS START HERE: Save intermediate files to satisfy 'make'
	@cp -a $(BUILD_DIR)/$(HOST_DIR) .report_backup/$(BUILD_DIR)/ 2>/dev/null || true
	@cp -a $(LINK_OUTPUT) .report_backup/$(BUILD_DIR)/ 2>/dev/null || true
	@cp -a $(KERNEL_XO) .report_backup/$(TEMP_DIR)/ 2>/dev/null || true
	@cp -a $(EMCONFIG_DIR)/emconfig.json .report_backup/$(TEMP_DIR)/ 2>/dev/null || true
	@# ---> MINIMAL ADDITIONS END HERE

	@# 3) Backup v++_kernel_nlp*.log outputs
	@cp -a v++_$(KERNEL_NAME)*.log .report_backup/ 2>/dev/null || true

	@# 4a & 4b) Backup the entire reports and logs directory in _x...
	@cp -a -r $(TEMP_DIR)/reports .report_backup/$(TEMP_DIR)/ 2>/dev/null || true
	@cp -a -r $(TEMP_DIR)/logs .report_backup/$(TEMP_DIR)/ 2>/dev/null || true

	@# 4c) Backup kernel_nlp.xclbin.info
	@cp -a $(TEMP_DIR)/link/int/$(KERNEL_NAME).xclbin.info .report_backup/$(TEMP_DIR)/link/int/ 2>/dev/null || true

	@# 4d) Backup csynth_design_size.rpt and csynth.rpt
	@cp -a $(TEMP_DIR)/$(KERNEL_NAME)/$(KERNEL_NAME)/$(KERNEL_NAME)/solution/syn/report/csynth_design_size.rpt .report_backup/$(TEMP_DIR)/$(KERNEL_NAME)/$(KERNEL_NAME)/$(KERNEL_NAME)/solution/syn/report/ 2>/dev/null || true
	@cp -a $(TEMP_DIR)/$(KERNEL_NAME)/$(KERNEL_NAME)/$(KERNEL_NAME)/solution/syn/report/csynth.rpt .report_backup/$(TEMP_DIR)/$(KERNEL_NAME)/$(KERNEL_NAME)/$(KERNEL_NAME)/solution/syn/report/ 2>/dev/null || true

	@echo "========================================================="
	@echo " Running cleanall to remove other generated files..."
	@echo "========================================================="
	@$(MAKE) cleanall

	@echo "========================================================="
	@echo " Restoring preserved files..."
	@echo "========================================================="
	@# Restore outputs
	@cp -a .report_backup/$(KERNEL_NAME).xclbin ./ 2>/dev/null || true
	@mkdir -p $(BUILD_DIR)
	@cp -a .report_backup/$(BUILD_DIR)/* $(BUILD_DIR)/ 2>/dev/null || true
	@cp -a .report_backup/$(notdir $(EXECUTABLE)) ./ 2>/dev/null || true
	@cp -a .report_backup/v++_$(KERNEL_NAME)*.log ./ 2>/dev/null || true

	@# Restore reports and nested paths
	@mkdir -p $(TEMP_DIR)
	@cp -a -r .report_backup/$(TEMP_DIR)/* $(TEMP_DIR)/ 2>/dev/null || true

	@# Cleanup the temp backup
	@rm -rf .report_backup
	@echo "========================================================="
	@echo " Report generation and cleanup complete."
	@echo "========================================================="