cmake_minimum_required(VERSION 3.20)
project(MeowOS C ASM)

# cross compiler
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_ASM_COMPILER gcc)

set(CMAKE_C_FLAGS "-m64 -DMEOW_KERNEL_64 -ffreestanding -fno-stack-protector -fno-pie -O2 -Wall -Wextra -mno-mmx -mno-sse -mno-sse2 -fno-tree-vectorize")
set(CMAKE_ASM_FLAGS "-m64")
set(CMAKE_EXE_LINKER_FLAGS "-m64 -no-pie -T ${CMAKE_SOURCE_DIR}/linker.ld -nostdlib")

add_custom_target(format
    COMMAND clang-format -i ${SOURCES}
)

# build kernel sources from the entire src tree
file(GLOB_RECURSE KERNEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.asm"
    "${CMAKE_SOURCE_DIR}/src/*.s"
)

# 32-bit interrupt and ring transition code is not directly compatible with the
# current 64-bit build. Use 64-bit compatibility stubs instead.
list(REMOVE_ITEM KERNEL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/kernel/cpu/gdt.c"
    "${CMAKE_SOURCE_DIR}/src/kernel/interrupts/idt.c"
    "${CMAKE_SOURCE_DIR}/src/kernel/interrupts/isr.c"
    "${CMAKE_SOURCE_DIR}/src/kernel/interrupts/pic.c"
    "${CMAKE_SOURCE_DIR}/src/kernel/interrupts/interrupts.s"
)

# Add long mode switcher and tables
list(APPEND KERNEL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/longmode.c"
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/longmode_trampoline.s"
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/longmode_tables.s"
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/longmode_jump.s"
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/enter_user_mode.c"
    "${CMAKE_SOURCE_DIR}/src/arch/x86_64/enter_user_mode.s"
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
set(MEOWBOX_SRC "${CMAKE_SOURCE_DIR}/tools/meowbox/meowbox.c")
set(MEOWBOX_BIN "${CMAKE_BINARY_DIR}/meowbox.elf")
set(MKINITRAMFS_SRC "${CMAKE_SOURCE_DIR}/tools/mkinitramfs.c")
set(MKINITRAMFS_BIN "${CMAKE_BINARY_DIR}/mkinitramfs")
set(INITRAMFS_CPIO "${CMAKE_BINARY_DIR}/initramfs.cpio")
set(INITRAMFS_CPIO_GZ "${CMAKE_BINARY_DIR}/initramfs.cpio.gz")

add_custom_command(
    OUTPUT ${FAT_DISK_IMG}
    COMMAND ${CMAKE_COMMAND} -E env sh -c "if [ ! -f '${FAT_DISK_IMG}' ]; then truncate -s 2G '${FAT_DISK_IMG}' && mkfs.fat -F 32 '${FAT_DISK_IMG}'; fi"
    VERBATIM
)

add_custom_target(fat_disk
    DEPENDS ${FAT_DISK_IMG}
)



add_custom_command(
    OUTPUT ${MEOWBOX_BIN}
    COMMAND ${CMAKE_C_COMPILER} -m64 -ffreestanding -fno-stack-protector -fno-pie -no-pie -nostdlib -Wl,--build-id=none,-melf_x86_64,-Ttext,0x0000000000100000,-e,_start ${MEOWBOX_SRC} -o ${MEOWBOX_BIN}
    DEPENDS ${MEOWBOX_SRC}
    VERBATIM
)

add_custom_target(meowbox_tool
    DEPENDS ${MEOWBOX_BIN}
)

add_custom_command(
    OUTPUT ${MKINITRAMFS_BIN}
    COMMAND ${CMAKE_C_COMPILER} ${MKINITRAMFS_SRC} -o ${MKINITRAMFS_BIN}
    DEPENDS ${MKINITRAMFS_SRC}
    VERBATIM
)

add_custom_target(mkinitramfs_tool
    DEPENDS ${MKINITRAMFS_BIN}
)

add_custom_command(
    OUTPUT ${INITRAMFS_CPIO_GZ}
    COMMAND ${MKINITRAMFS_BIN} ${INITRAMFS_CPIO} ${MEOWBOX_BIN}
    COMMAND ${CMAKE_COMMAND} -E env sh -c "gzip -c '${INITRAMFS_CPIO}' > '${INITRAMFS_CPIO_GZ}'"
    DEPENDS ${MEOWBOX_BIN} ${MKINITRAMFS_BIN}
    VERBATIM
)

add_custom_target(initramfs_image
    DEPENDS ${INITRAMFS_CPIO_GZ}
)

# ISO
add_custom_target(iso ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/iso/boot/grub
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/grub.cfg ${CMAKE_BINARY_DIR}/iso/boot/grub/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/kernel.bin ${CMAKE_BINARY_DIR}/iso/boot/
    COMMAND ${CMAKE_COMMAND} -E copy ${INITRAMFS_CPIO_GZ} ${CMAKE_BINARY_DIR}/iso/boot/initramfs.cpio.gz
    COMMAND ${CMAKE_COMMAND} -E env sh -c "gzip -dkf '${CMAKE_BINARY_DIR}/iso/boot/initramfs.cpio.gz'"
    COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/meowos.iso ${CMAKE_BINARY_DIR}/iso
    DEPENDS kernel.bin initramfs_image
)

# run in QEMU
add_custom_target(run
    COMMAND qemu-system-x86_64
        -cdrom ${CMAKE_BINARY_DIR}/meowos.iso -serial stdio
    DEPENDS iso fat_disk
)