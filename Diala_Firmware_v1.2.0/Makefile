# Diala main controller firmware
#   make            target build  -> build/diala_fw.elf/.hex/.bin (+ size, map)
#   make test       host simulator: build and run all scenario tests
#   make check      pin map + cycle table cross-checks, cppcheck
#   make all-checks everything above; must finish with no warnings or failures
APP  := $(wildcard src/app/*.c)
TGT  := $(wildcard src/bsp/stm32/*.c)
SIM  := $(wildcard src/sim/*.c)
WARN := -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wformat=2 -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wno-missing-field-initializers

CC_ARM := arm-none-eabi-gcc
ARMFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Os -g3 -std=c11 \
            -ffunction-sections -fdata-sections -DSTM32F407xx -ffile-prefix-map=$(CURDIR)/= \
            -Ivendor/cmsis_device_f4/Include -Ivendor/CMSIS_5/CMSIS/Core/Include
LDFLAGS := -Tlinker/stm32f407ve.ld -nostartfiles --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=build/diala_fw.map -Wl,--print-memory-usage

.PHONY: all test check all-checks clean
all: build/diala_fw.hex

build/diala_fw.elf: $(APP) $(TGT) $(wildcard src/*/*.h src/bsp/stm32/*.h) linker/stm32f407ve.ld tools/patch_crc.py
	@mkdir -p build
	$(CC_ARM) $(ARMFLAGS) $(WARN) $(APP) $(TGT) $(LDFLAGS) -o build/diala_fw.raw.elf
	python3 tools/patch_crc.py build/diala_fw.raw.elf $@
	@rm -f build/diala_fw.raw.elf
	arm-none-eabi-size $@

build/diala_fw.hex: build/diala_fw.elf
	arm-none-eabi-objcopy -O ihex $< $@
	arm-none-eabi-objcopy -O binary --gap-fill 0xFF $< build/diala_fw.bin

build/sim: $(APP) $(SIM) $(wildcard src/*/*.h)
	@mkdir -p build
	gcc -std=c11 -O1 -g $(WARN) -fsanitize=address,undefined -fno-sanitize-recover=all $(APP) $(SIM) -o $@

test: build/sim
	./build/sim

check:
	python3 tools/check_pinmap.py
	python3 tools/check_cycle_table.py
	python3 tools/check_lcd_text.py
	cppcheck --quiet --enable=warning,performance,portability --error-exitcode=1 --std=c11 \
	   --suppress=missingIncludeSystem -DSTM32F407xx src/app src/sim

all-checks: all test check

clean:
	rm -rf build
