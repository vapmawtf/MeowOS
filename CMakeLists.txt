cmake_minimum_required(VERSION 3.20)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(MeowOS C ASM)

# ----------------------------
# Cross-compiler configuration
# ----------------------------
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER x86_64-elf-gcc)
set(CMAKE_ASM_COMPILER x86_64-elf-gcc)

set(CMAKE_C_FLAGS "-m64 -DMEOW_KERNEL_64 -ffreestanding -nostdlib -nostartfiles -fno-stack-protector -fno-pie -O2 -Wall -Wextra -mno-mmx -mno-sse -mno-sse2 -fno-tree-vectorize")
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

# Remove sources that are not needed in pure x86_64 mode
list(REMOVE_ITEM KERNEL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/kernel/arch/compat64.c"
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
# (stamp file used so CMake knows when this has been done)
# ----------------------------
set(FAT_DISK_IMG "${CMAKE_BINARY_DIR}/fat32.img")
set(FAT_DISK_STAMP "${CMAKE_BINARY_DIR}/fat32.stamp")

add_custom_command(
    OUTPUT ${FAT_DISK_STAMP}
    COMMAND sh -c "if [ ! -f '${FAT_DISK_IMG}' ]; then truncate -s 64M '${FAT_DISK_IMG}' && mkfs.fat -F 32 '${FAT_DISK_IMG}'; fi"
    COMMAND ${CMAKE_COMMAND} -E touch ${FAT_DISK_STAMP}
    COMMENT "Creating FAT32 disk image"
    VERBATIM
)
add_custom_target(fat_disk DEPENDS ${FAT_DISK_STAMP})

# ----------------------------
# Toybox: verify it exists and is an x86-64 ELF, then copy
# Fix: use add_custom_command with OUTPUT (a file) so other
# add_custom_commands can DEPENDS on it — custom targets can't
# be used in DEPENDS of add_custom_command.
# ----------------------------
set(TOYBOX_SRC "${CMAKE_SOURCE_DIR}/external/toybox/toybox")
set(TOYBOX_DST "${CMAKE_BINARY_DIR}/toybox")

add_custom_command(
    OUTPUT ${TOYBOX_DST}
    COMMAND sh -c
        "if [ ! -f '${TOYBOX_SRC}' ]; then \
             echo 'ERROR: ${TOYBOX_SRC} not found. Run: git submodule update --init && cd external/toybox && make defconfig && CFLAGS=--static make' >&2; \
             exit 1; \
         fi"
    COMMAND sh -c
        "if ! file '${TOYBOX_SRC}' | grep -q 'x86-64'; then \
             echo 'ERROR: ${TOYBOX_SRC} is not an x86-64 ELF. Rebuild toybox for x86-64.' >&2; \
             exit 1; \
         fi"
    COMMAND ${CMAKE_COMMAND} -E copy ${TOYBOX_SRC} ${TOYBOX_DST}
    DEPENDS ${TOYBOX_SRC}
    COMMENT "Validating and copying toybox binary"
    VERBATIM
)

# ----------------------------
# Initramfs using mkramfs.sh
# Fix: DEPENDS on the actual output file ${TOYBOX_DST}, not a
# custom target — this is the correct way to chain custom commands.
# Only produce the raw .cpio (no .gz needed; kernel reads raw CPIO).
# ----------------------------
set(MKRAMFS_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/mkramfs.sh")
set(INITRAMFS_CPIO "${CMAKE_BINARY_DIR}/initramfs.cpio")

add_custom_command(
    OUTPUT ${INITRAMFS_CPIO}
    COMMAND chmod +x ${MKRAMFS_SCRIPT}
    COMMAND ${MKRAMFS_SCRIPT} ${CMAKE_BINARY_DIR}
    DEPENDS ${MKRAMFS_SCRIPT} ${TOYBOX_DST}
    COMMENT "Generating initramfs.cpio"
    VERBATIM
)
add_custom_target(initramfs_image DEPENDS ${INITRAMFS_CPIO})

# ----------------------------
# Build ISO
# Fix: copy only the raw .cpio (no gz), don't bloat the ISO.
# grub.cfg already references /boot/initramfs.cpio.
# ----------------------------
set(ISO_DIR "${CMAKE_BINARY_DIR}/iso")
set(MEOWOS_ISO "${CMAKE_BINARY_DIR}/meowos.iso")

add_custom_target(iso ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_DIR}/boot/grub
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/grub.cfg ${ISO_DIR}/boot/grub/grub.cfg
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/kernel.bin ${ISO_DIR}/boot/kernel.bin
    COMMAND ${CMAKE_COMMAND} -E copy ${INITRAMFS_CPIO} ${ISO_DIR}/boot/initramfs.cpio
    COMMAND grub-mkrescue -o ${MEOWOS_ISO} ${ISO_DIR}
    DEPENDS kernel.bin initramfs_image
    COMMENT "Building MeowOS ISO"
)

# ----------------------------
# Run in QEMU
# Fix: actually attach the FAT disk that fat_disk creates.
# Added -m 256M so the userland stack/heap has room.
# ----------------------------
add_custom_target(run
    COMMAND qemu-system-x86_64
        -cdrom ${MEOWOS_ISO}
        -m 256M
        -serial stdio
        -d int,cpu_reset
        -no-reboot
    DEPENDS iso fat_disk
    COMMENT "Running MeowOS in QEMU"
)

# ----------------------------
# Code formatting
# ----------------------------
add_custom_target(format
    COMMAND clang-format -i ${KERNEL_SOURCES}
)