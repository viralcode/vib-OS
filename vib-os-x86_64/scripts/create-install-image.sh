#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -ne 1 ]]; then
  echo "Usage: $0 NEW_IMAGE_FILE" >&2
  exit 2
fi

requested="$1"
output="$(realpath -m -- "$requested")"
case "$output" in
  /dev/*) echo "Refusing to write an installer image under /dev." >&2; exit 2 ;;
esac
if [[ -e "$output" || -L "$output" ]]; then
  echo "Refusing to overwrite existing path: $output" >&2
  exit 2
fi
parent="$(dirname "$output")"
if [[ ! -d "$parent" ]]; then
  echo "Output directory does not exist: $parent" >&2
  exit 2
fi

for command in sgdisk mkfs.fat mmd mcopy; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Missing required tool: $command" >&2
    exit 2
  fi
done
kernel="$ROOT_DIR/build/kernel.elf"
limine_efi="$ROOT_DIR/limine/BOOTX64.EFI"
config="$ROOT_DIR/limine.conf"
for file in "$kernel" "$limine_efi" "$config"; do
  if [[ ! -f "$file" ]]; then
    echo "Required build artifact is missing: $file (build the kernel first)" >&2
    exit 2
  fi
done

# The output is a fresh regular file. No loop devices, mounts, sudo, or /dev paths.
image_size_mib=128
total_sectors=$((image_size_mib * 1024 * 1024 / 512))
partition_first=2048
partition_sectors=$((total_sectors - partition_first - 33))
tmp="$(mktemp "${output}.tmp.XXXXXX")"
partition_tmp="$(mktemp "${output}.fat.XXXXXX")"
cleanup() { rm -f -- "$tmp" "$partition_tmp"; }
trap cleanup EXIT

truncate -s "$((image_size_mib * 1024 * 1024))" "$tmp"
sgdisk --clear --new="1:${partition_first}:0" --typecode=1:EF00 \
  --change-name=1:VIBOS-ESP "$tmp" >/dev/null
truncate -s "$((partition_sectors * 512))" "$partition_tmp"
mkfs.fat -F 32 -s 1 -n VIBOS "$partition_tmp" >/dev/null
volume="$partition_tmp"
mmd -i "$volume" ::/EFI
mmd -i "$volume" ::/EFI/BOOT
mmd -i "$volume" ::/boot
mcopy -i "$volume" "$limine_efi" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$volume" "$config" ::/limine.conf
mcopy -i "$volume" "$config" ::/EFI/BOOT/limine.conf
mcopy -i "$volume" "$kernel" ::/boot/kernel.elf
dd if="$partition_tmp" of="$tmp" bs=512 seek="$partition_first" \
  conv=notrunc status=none

readme="$(mktemp "${output}.README.XXXXXX")"
trap 'rm -f -- "$tmp" "$partition_tmp" "$readme"' EXIT
cat >"$readme" <<'EOF'
Vib-OS x86_64 UEFI install image

This is a disposable 128 MiB GPT disk image with one FAT32 EFI System
Partition. It contains the Limine UEFI loader and the Vib-OS kernel.
The current kernel mounts FAT32 read-only; this image is not a writable
system installer and does not modify firmware NVRAM.
EOF
mcopy -i "$volume" "$readme" ::/README.TXT
rm -f -- "$readme"
sgdisk --verify "$tmp" >/dev/null

# Link creation is atomic and fails if another process created the destination.
ln -T -- "$tmp" "$output"
rm -f -- "$tmp"
trap - EXIT
printf 'Created new UEFI disk image: %s\n' "$output"
printf 'Size: %s MiB; GPT ESP at LBA %s; FAT32\n' "$image_size_mib" "$partition_first"
