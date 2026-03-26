cmake_minimum_required(VERSION 3.20)
project(MeowOS C ASM)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ----------------------------
# Cross-compiler configuration
# ----------------------------
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER x86_64-elf-gcc)
set(CMAKE_ASM_COMPILER x86_64-elf-gcc)

set(CMAKE_C_FLAGS "-m64 -DMEOW_KERNEL_64 -ffreestanding -fno-stack-protector -fno-pie -O2 -Wall -Wextra -mno-mmx -mno-sse -mno-sse2 -fno-tree-vectorize")
set(CMAKE_ASM_FLAGS "-m64")
set(CMAKE_EXE_LINKER_FLAGS "-m64 -T ${CMAKE_SOURCE_DIR}/linker.ld -nostdlib -nodefaultlibs -nostartfiles -Wl,--build-id=none")

# ----------------------------
# Include directories
# ----------------------------
include_directories(${CMAKE_SOURCE_DIR}/include)

# ----------------------------
# Kernel sources
# ----------------------------
file(GLOB_RECURSE KERNEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.s"
    "${CMAKE_SOURCE_DIR}/src/*.asm"
)

# Remove 32-bit interrupt sources
list(REMOVE_ITEM KERNEL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/kernel/cpu/gdt.c"
    "${CMAKE_SOURCE_DIR}/src/kernel/interrupts/isr.c"
    "${CMAKE_SOURCE_DIR}/src/kernel/interrupts/pic.c"
    "${CMAKE_SOURCE_DIR}/src/kernel/interrupts/interrupts.s"
)

# Add 64-bit sources
list(APPEND KERNEL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/longmode.c"
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/longmode_jump.s"
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/longmode_tables.s"
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/enter_user_mode.c"
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/enter_user_mode.s"
)

# ----------------------------
# Fonts -> C array
# ----------------------------
set(PSFU_GZ "${CMAKE_SOURCE_DIR}/fonts/drdos8x8.psfu.gz")
set(PSFU_C "${CMAKE_BINARY_DIR}/drdos8x8_psfu.c")

add_custom_command(
    OUTPUT ${PSFU_C}
    COMMAND gzip -dc ${PSFU_GZ} | xxd -i -n drdos8x8_psfu_data > ${PSFU_C}
    DEPENDS ${PSFU_GZ}
    VERBATIM
)

# ----------------------------
# Build kernel
# ----------------------------
add_executable(kernel.bin
    ${KERNEL_SOURCES}
    ${PSFU_C}
)

# ----------------------------
# FAT disk image
# ----------------------------
set(FAT_DISK_IMG "${CMAKE_BINARY_DIR}/fat32.img")
add_custom_command(
    OUTPUT ${FAT_DISK_IMG}
    COMMAND ${CMAKE_COMMAND} -E env sh -c "if [ ! -f '${FAT_DISK_IMG}' ]; then truncate -s 2G '${FAT_DISK_IMG}' && mkfs.fat -F 32 '${FAT_DISK_IMG}'; fi"
    VERBATIM
)
add_custom_target(fat_disk DEPENDS ${FAT_DISK_IMG})

# ----------------------------
# Toybox (optional copy)
# ----------------------------
set(TOYBOX_SRC "${CMAKE_SOURCE_DIR}/external/toybox/toybox")
add_custom_target(toybox_target
    COMMAND ${CMAKE_COMMAND} -E copy ${TOYBOX_SRC} ${CMAKE_BINARY_DIR}/toybox
    COMMENT "Copying toybox binary to build folder"
)

# ----------------------------
# Initramfs using mkramfs.sh
# ----------------------------
set(MKRAMFS_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/mkramfs.sh")
set(INITRAMFS_CPIO "${CMAKE_BINARY_DIR}/initramfs.cpio")
set(INITRAMFS_CPIO_GZ "${CMAKE_BINARY_DIR}/initramfs.cpio.gz")

add_custom_command(
    OUTPUT ${INITRAMFS_CPIO} ${INITRAMFS_CPIO_GZ}
    COMMAND chmod +x ${MKRAMFS_SCRIPT}
    COMMAND ${MKRAMFS_SCRIPT} ${CMAKE_BINARY_DIR}
    DEPENDS ${MKRAMFS_SCRIPT} toybox_target
    COMMENT "Generating initramfs.cpio and initramfs.cpio.gz"
    VERBATIM
)
add_custom_target(initramfs_image DEPENDS ${INITRAMFS_CPIO} ${INITRAMFS_CPIO_GZ})

# ----------------------------
# Build ISO
# ----------------------------
add_custom_target(iso ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/iso/boot/grub
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/grub.cfg ${CMAKE_BINARY_DIR}/iso/boot/grub/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/kernel.bin ${CMAKE_BINARY_DIR}/iso/boot/
    COMMAND ${CMAKE_COMMAND} -E copy ${INITRAMFS_CPIO_GZ} ${CMAKE_BINARY_DIR}/iso/boot/initramfs.cpio.gz
    COMMAND gzip -dkf ${CMAKE_BINARY_DIR}/iso/boot/initramfs.cpio.gz
    COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/meowos.iso ${CMAKE_BINARY_DIR}/iso
    DEPENDS kernel.bin initramfs_image
)

# ----------------------------
# Run in QEMU
# ----------------------------
add_custom_target(run
    COMMAND qemu-system-x86_64 -cdrom ${CMAKE_BINARY_DIR}/meowos.iso -serial stdio -d int,cpu_reset -no-reboot
    DEPENDS iso fat_disk
)

# ----------------------------
# Code formatting
# ----------------------------
add_custom_target(format
    COMMAND clang-format -i ${KERNEL_SOURCES}
)