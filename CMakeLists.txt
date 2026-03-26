cmake_minimum_required(VERSION 3.20)
project(MeowOS C ASM)

# cross compiler
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_ASM_COMPILER gcc)

set(CMAKE_C_FLAGS "-m32 -ffreestanding -fno-stack-protector -fno-pie -O2 -Wall -Wextra -mno-mmx -mno-sse -mno-sse2 -fno-tree-vectorize")
set(CMAKE_ASM_FLAGS "-m32")
set(CMAKE_EXE_LINKER_FLAGS "-m32 -no-pie -T ${CMAKE_SOURCE_DIR}/linker.ld -nostdlib")

# build kernel sources from the entire src tree
file(GLOB_RECURSE KERNEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.asm"
    "${CMAKE_SOURCE_DIR}/src/*.s"
)

set(PSFU_GZ "${CMAKE_SOURCE_DIR}/fonts/drdos8x8.psfu.gz")
set(PSFU_C "${CMAKE_BINARY_DIR}/drdos8x8_psfu.c")

add_custom_command(
    OUTPUT ${PSFU_C}
    COMMAND ${CMAKE_COMMAND} -E env sh -c "gzip -dc '${PSFU_GZ}' | xxd -i -n drdos8x8_psfu_data > '${PSFU_C}'"
    DEPENDS ${PSFU_GZ}
    VERBATIM
)

add_executable(kernel.bin
    ${KERNEL_SOURCES}
    ${PSFU_C}
)

include_directories(
    "include/"
)

set(FAT_DISK_IMG "${CMAKE_BINARY_DIR}/fat32.img")

add_custom_command(
    OUTPUT ${FAT_DISK_IMG}
    COMMAND ${CMAKE_COMMAND} -E env sh -c "if [ ! -f '${FAT_DISK_IMG}' ]; then truncate -s 2G '${FAT_DISK_IMG}' && mkfs.fat -F 32 '${FAT_DISK_IMG}'; fi"
    VERBATIM
)

add_custom_target(fat_disk
    DEPENDS ${FAT_DISK_IMG}
)

# ISO
add_custom_target(iso ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/iso/boot/grub
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/grub.cfg ${CMAKE_BINARY_DIR}/iso/boot/grub/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/kernel.bin ${CMAKE_BINARY_DIR}/iso/boot/
    COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/myos.iso ${CMAKE_BINARY_DIR}/iso
    DEPENDS kernel.bin
)

# run in QEMU
add_custom_target(run
    COMMAND qemu-system-x86_64
        -drive if=none,id=ahcidisk,file=${FAT_DISK_IMG},format=raw
        -cdrom ${CMAKE_BINARY_DIR}/myos.iso
        -device ahci,id=ahci0
        -device ide-hd,drive=ahcidisk,bus=ahci0.0
    DEPENDS iso fat_disk
)