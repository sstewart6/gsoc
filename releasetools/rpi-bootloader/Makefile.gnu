CFLAGS := -c -O0 -Wall -Wstrict-prototypes -nostdinc -fno-builtin -ffunction-sections -march=armv7ve

ifdef DEBUG
    CFLAGS += -g
endif

DIR := releasetools/rpi-bootloader
LDFLAGS := --static --gc-sections -nostdlib -T${DIR}/bootloader.ld
COMPILER_ROOT := arm-none-eabi-
CC := $(COMPILER_ROOT)gcc
LD := $(COMPILER_ROOT)ld
OBJCOPY := $(COMPILER_ROOT)objcopy
S_FILES := bootloader.S
CPIO_FILE := kernel.bin mod01_ds mod02_rs mod03_pm mod04_sched mod05_vfs \
	mod06_memory mod07_tty mod08_mib mod09_vm mod10_pfs mod11_mfs mod12_init 

COMP_SRC += ${S_FILES:%=${DIR}/%} ${C_FILES:%=${DIR}/%}

COMP_OBJ := $(patsubst %.S,%.o,$(filter %.S,$(COMP_SRC)))

$(MOD_DIR)/minix_rpi.bin: $(MOD_DIR)/kernel.bin $(MOD_DIR)/bootloader.bin
	cat $(MOD_DIR)/bootloader.bin > $(MOD_DIR)/minix_rpi.bin
	cd $(MOD_DIR) && for file in $(CPIO_FILE); do \
		echo $$file >> file_list.txt; \
	done
	cd $(MOD_DIR) && cat file_list.txt | cpio -o --format=newc >> $@
	-rm $(MOD_DIR)/file_list.txt

$(MOD_DIR)/kernel.bin: $(MOD_DIR)/kernel
	$(OBJCOPY) -O binary $< $@

$(MOD_DIR)/bootloader.bin: bootloader.elf
	$(OBJCOPY) -O binary $(DIR)/$< $@

bootloader.elf: $(COMP_OBJ)
	$(LD) -o $(DIR)/$@ $(LDFLAGS) $^ 

%.o: %.S
	$(CC) $(DEPFLAGS) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	-rm ${RELEASE_DIR}/rpi-bootloader/*.o
	-rm ${RELEASE_DIR}/rpi-bootloader/bootloader.elf
	-rm $(MOD_DIR)/file_list.txt
	-rm $(MOD_DIR)/bootloader.bin
	-rm $(MOD_DIR)/minix_rpi.bin
	-rm $(MOD_DIR)/kernel.bin
