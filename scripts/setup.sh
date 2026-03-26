#!/bin/bash

# setup.sh - setup script for MeowOS development environment

# Check for required tools
REQUIRED_TOOLS=(x86_64-elf-gcc x86_64-elf-as qemu-system-x86_64 cmake make)

for tool in "${REQUIRED_TOOLS[@]}"; do
    if ! command -v "$tool" &> /dev/null; then
        echo "Error: $tool is not installed. Please install it and try again."
        exit 1
    fi
done

cd external/toybox
make defconfig
CFLAGS="--static -no-pie" LDFLAGS="-no-pie" make -j$(nproc)
cd ../..


# Create build directory
mkdir -p build
cd build
cmake ..
echo "Setup complete. You can now build the kernel with 'make' in the build directory."