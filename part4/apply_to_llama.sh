#!/usr/bin/env bash
# Apply the GGML hook and copy the hardware-only Part 4 sources.
set -euo pipefail

part4_dir="$(cd "$(dirname "$0")" && pwd)"
llama_dir="$(cd "${1:?Usage: bash part4/apply_to_llama.sh /path/to/llama.cpp}" && pwd)"
if [ "$part4_dir" = "$llama_dir/part4" ]; then
    echo "Run the script from the lab repository's part4 directory, outside llama.cpp." >&2
    exit 1
fi

patch_file="$part4_dir/integration/llama-cpu.patch"
if git -C "$llama_dir" apply --check "$patch_file" 2>/dev/null; then
    git -C "$llama_dir" apply "$patch_file"
elif git -C "$llama_dir" apply --reverse --check "$patch_file" 2>/dev/null; then
    echo "GGML integration patch is already applied."
else
    echo "Cannot apply the patch cleanly; existing GGML edits require review." >&2
    exit 1
fi

for file in \
    driver/Makefile driver/dot_accel_drv.c \
    hal/dot_hal.c hal/dot_hal.h \
    include/dot_accel_regs.h include/dot_accel_ioctl.h \
    integration/dot-ggml.c integration/dot-ggml.h integration/ggml.cmake; do
    mkdir -p "$llama_dir/part4/$(dirname "$file")"
    cp "$part4_dir/$file" "$llama_dir/part4/$file"
done
echo "Part 4 integrated into $llama_dir."
