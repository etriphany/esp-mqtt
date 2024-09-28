# Makefile for ESP8266 projects
# https://makefiletutorial.com/


#=======================================================
# Output directories (RELATIVE PATH)

BUILD_BASE	= build
FW_BASE		= firmware

#=======================================================
# Target application name
TARGET		= app

#=======================================================
# SDK directories and tools (ABSOLUTE PATH)

XTENSA_BASE	?= /home/user/esp-open-sdk/xtensa-lx106-elf/bin
SDK_BASE	?= /home/user/ESP8266_NONOS_SDK-3.0.4
ESPTOOL		?= esptool.py
GEN_APPBIN	:= ${SDK_BASE}/tools/gen_appbin.py
FOTA_BOOT	:= ${SDK_BASE}/bin/boot_v1.7.bin

#=======================================================
# SDK directories and tools (RELATIVE PATH)

SDK_LIBDIR	= lib
SDK_LDDIR	= ld
SDK_INCDIR	= include include/json third_party/include

#=======================================================
# Port connected to ESP (ABSOLUTE PATH)

ESP_PORT	?= /dev/ttyUSB0

# which modules (subdirectories) of the project to include in compiling
MODULES 		= user \
				driver \
				modules/esp-mqtt \
				modules/utils

#=======================================================
# Directory for includes (RELATIVE PATH)

XTRA_INCDIR	= include

#=======================================================
# Libraries used in this project, mainly provided by the SDK
#
# Proprietary
#   	Espressif SDK: (libc.a, libcc.a, libhal.a, libpp.a, libmain.a, libphy.a)
#  		Xtensa Tool Chain: (libm.a)
# Open NOT shared on SDK:
#		FreeBSD WiFi stack: (libnet80211.a)
#		WPA Supplicant: (libcrypto.a, libwpa.a)
# Open shared on SDK (thirdy-party directory):
#		LwIP: (liblwip.a):
#		Mbed TLS: (libmbedtls.a)
#		SICS Json: (libjson.a)

LIBS		= c \
			gcc \
			hal \
			pp \
			main \
			phy \
			net80211 \
			wpa \
			crypto \
			lwip \
			mbedtls \
			json \
			m

#=======================================================
# Compiler flags using during compilation of source files

CFLAGS		= -Os \
			-g \
			-O2 \
			-Wpointer-arith \
			-Wundef \
			-Werror \
			-Wl,-EL \
			-fno-inline-functions \
			-nostdlib \
			-mlongcalls \
			-mtext-section-literals \
			-D__ets__ \
			-DICACHE_FLASH \
			-DLWIP_OPEN_SRC

#=======================================================
# Linker flags used to generate the main object file

LDFLAGS		= -nostdlib \
			-Wl,--no-check-sections \
			-u call_user_start \
			-Wl,-static

#=======================================================
# Linker scripts used for the above linkier step

# Linker for NON-FOTA user.bin
LD_SCRIPT_APP_0		= eagle.app.v6.ld
# Linker for FOTA user1.bin (ESP8266 12e SIZE_MAP = 4)
LD_SCRIPT_APP_1		= eagle.app.v6.new.1024.app1.ld
# Linker for FOTA user2.bin (ESP8266 12e SIZE_MAP = 4)
LD_SCRIPT_APP_2		= eagle.app.v6.new.1024.app2.ld

#=======================================================
# Names (adress) used to generate/upload files to flash

# NON-FOTA app.bin or FOTA boot.bin
FW_MAIN_ADDR	= 0x00000
# NON-FOTA irom0text.bin
FW_APP_0_ADDR	= 0x10000
# FOTA user1.bin (4KB)
FW_APP_1_ADDR	= 0x01000
# FOTA user2.bin (512KB + 4KB)
FW_APP_2_ADDR	= 0x81000

#=======================================================
# Select which tools to use as compiler, librarian and linker

CC			:= $(XTENSA_BASE)/xtensa-lx106-elf-gcc
AR			:= $(XTENSA_BASE)/xtensa-lx106-elf-ar
LD			:= $(XTENSA_BASE)/xtensa-lx106-elf-gcc
OBJCOPY		:= $(XTENSA_BASE)/xtensa-lx106-elf-objcopy
OBJDUMP		:= $(XTENSA_BASE)/xtensa-lx106-elf-objdump

##########################################################
# No user configurable options below here

SRC_DIR		:= $(MODULES)
BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(MODULES))

SDK_LIBDIR	:= $(addprefix $(SDK_BASE)/,$(SDK_LIBDIR))
SDK_INCDIR	:= $(addprefix -I$(SDK_BASE)/,$(SDK_INCDIR))

SRC			:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ			:= $(patsubst %.c,$(BUILD_BASE)/%.o,$(SRC))
LIBS		:= $(addprefix -l,$(LIBS))
APP_AR		:= $(addprefix $(BUILD_BASE)/,$(TARGET)_app.a)
APP_OUT		:= $(addprefix $(BUILD_BASE)/,$(TARGET).out)

# Point ld script to selected APP (0,1 or 2)
LD_SCRIPT	:= $(addprefix -T$(SDK_BASE)/$(SDK_LDDIR)/,$(LD_SCRIPT_APP_$(PARAM_APP)))

INCDIR			:= $(addprefix -I,$(SRC_DIR))
XTRA_INCDIR		:= $(addprefix -I,$(XTRA_INCDIR))
MODULE_INCDIR	:= $(addsuffix /include,$(INCDIR))

FW_BOOT		:= $(addprefix $(FW_BASE)/,$(FW_MAIN_ADDR).bin)
# Point app firmware to selected APP (0,1 or 2)
FW_APP		:= $(addprefix $(FW_BASE)/,$(FW_APP_$(PARAM_APP)_ADDR).bin)

# Boot mode
ifeq ($(PARAM_APP), 0)
	BOOT_MODE := 0
else
	BOOT_MODE := 2
endif

# Verbose control
V ?= $(VERBOSE)
ifeq ("$(V)","1")
	Q :=
	vecho := @true
else
	Q := @
	vecho := @echo
endif

# ???
vpath %.c $(SRC_DIR)

# Macro to compile source file
define compile-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(INCDIR) $(MODULE_INCDIR) $(XTRA_INCDIR) $(SDK_INCDIR) $(CFLAGS) -c $$< -o $$@
endef

# Required or gen_appbin.py will fail
export COMPILE=gcc

# Makefile targets
.PHONY: all checkdirs image flash clean trace reborn

all: checkdirs $(APP_OUT) $(FW_BOOT) $(FW_APP)

$(FW_BASE)/%.bin: $(APP_OUT) | $(FW_BASE)
	$(vecho) "FW $(FW_BASE)/"
	$(Q) $(ESPTOOL) elf2image -o $(FW_BASE)/ $(APP_OUT)

$(APP_OUT): $(APP_AR)
	$(vecho) "LD $@"
	$(Q) $(LD) -L$(SDK_LIBDIR) $(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@

$(APP_AR): $(OBJ)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $^

checkdirs: $(BUILD_DIR) $(FW_BASE)

$(BUILD_DIR):
	$(Q) mkdir -p $@

$(FW_BASE):
	$(Q) mkdir -p $@

image: $(FW_BOOT) $(FW_APP)
	$(vecho) "Splitting app.out into sub-components"
	@$(OBJDUMP) -x -s $(APP_OUT) > $(FW_BASE)/app.dump
	@$(OBJDUMP) -S $(APP_OUT) > $(FW_BASE)/app.S
	@$(OBJCOPY) --only-section .text -O binary $(APP_OUT) $(FW_BASE)/eagle.app.v6.text.bin
	@$(OBJCOPY) --only-section .data -O binary $(APP_OUT) $(FW_BASE)/eagle.app.v6.data.bin
	@$(OBJCOPY) --only-section .rodata -O binary $(APP_OUT) $(FW_BASE)/eagle.app.v6.rodata.bin
	@$(OBJCOPY) --only-section .irom0.text -O binary $(APP_OUT) $(FW_BASE)/eagle.app.v6.irom0text.bin
	$(vecho) "Build app bin"
	$(Q) cp -f $(GEN_APPBIN) $(FW_BASE)
	$(Q) cp -f $(APP_OUT) $(FW_BASE)/eagle.app.out
	$(Q) (cd $(FW_BASE); python gen_appbin.py eagle.app.out ${BOOT_MODE} 0 0 4 0)

flash: $(FW_BOOT) $(FW_APP)
	@if [ $(PARAM_APP) -eq 0 ] ; then \
		echo "Flash NON-FOTA firmware" ; \
		$(ESPTOOL) --chip esp8266 --port $(ESP_PORT) write_flash $(FW_MAIN_ADDR) $(FW_BASE)/eagle.app.flash.bin $(FW_APP_$(PARAM_APP)_ADDR) $(FW_BASE)/eagle.app.v6.irom0text.bin -fs 4MB; \
	else \
		echo "Flash FOTA firmware" ; \
		$(ESPTOOL) --chip esp8266 --port $(ESP_PORT) write_flash $(FW_MAIN_ADDR) $(FOTA_BOOT) $(FW_APP_$(PARAM_APP)_ADDR) $(FW_BASE)/eagle.app.flash.bin -fs 4MB; \
	fi

clean:
	$(Q) rm -rf $(FW_BASE) $(BUILD_BASE)

trace:
	$(ESPTOOL) --chip esp8266 --port $(ESP_PORT) chip_id
	tail -f $(ESP_PORT)

reborn:
	$(ESPTOOL) --chip esp8266 --port $(ESP_PORT) erase_flash
	$(ESPTOOL) --chip esp8266 --port $(ESP_PORT) write_flash 0x3FB000 $(SDK_BASE)/bin/blank.bin 0x3FC000 $(SDK_BASE)/bin/esp_init_data_default_v08.bin 0x3FE000 $(SDK_BASE)/bin/blank.bin -fs 4MB

$(foreach bdir,$(BUILD_DIR),$(eval $(call compile-objects,$(bdir))))
