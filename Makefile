BUILD := build
IMAGE := $(BUILD)/tristos.img
CXX ?= g++
LD ?= ld
OBJCOPY ?= objcopy
CXXFLAGS := -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -mno-red-zone -mno-sse -mno-mmx -mno-sse2 -Wall -Wextra -Werror -Ikernel
LDFLAGS := -nostdlib -T kernel/linker.ld
KERNEL_OBJECTS := $(BUILD)/main.o $(BUILD)/crypto.o $(BUILD)/fs.o $(BUILD)/net.o $(BUILD)/texe.o $(BUILD)/clock.o $(BUILD)/users.o $(BUILD)/settings.o $(BUILD)/storage.o $(BUILD)/devices.o $(BUILD)/media.o $(BUILD)/hardware.o $(BUILD)/drivers.o $(BUILD)/processes.o $(BUILD)/services.o

.PHONY: all clean run

all: $(IMAGE)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.bin: boot/boot.asm | $(BUILD)
	nasm -f bin $< -o $@

$(BUILD)/stage2.bin: boot/stage2.asm | $(BUILD)
	nasm -f bin $< -o $@

$(BUILD)/%.o: kernel/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(KERNEL_OBJECTS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJECTS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	python3 -c "from pathlib import Path; p=Path('$@'); data=p.read_bytes(); raise SystemExit('kernel exceeds 128 sectors') if len(data) > 65536 else p.write_bytes(data + bytes(65536-len(data)))"

$(IMAGE): $(BUILD)/boot.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	cat $^ > $@

run: $(IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE)

clean:
	rm -rf $(BUILD)
