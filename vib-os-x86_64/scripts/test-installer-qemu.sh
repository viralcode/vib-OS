#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

find_firmware() {
  local first="$1" second="$2" third="$3"
  for path in "$first" "$second" "$third"; do
    if [[ -n "$path" && -f "$path" ]]; then printf '%s\n' "$path"; return 0; fi
  done
  return 1
}
code="$(find_firmware "${OVMF_CODE:-}" /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd)" || {
  echo "OVMF_CODE firmware not found; set OVMF_CODE." >&2; exit 2;
}
vars="$(find_firmware "${OVMF_VARS:-}" /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/OVMF/OVMF_VARS.fd)" || {
  echo "OVMF_VARS firmware not found; set OVMF_VARS." >&2; exit 2;
}
for command in qemu-system-x86_64 timeout; do
  command -v "$command" >/dev/null 2>&1 || { echo "Missing required tool: $command" >&2; exit 2; }
done

tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
image="$tmp/vibos-test.img"
log="$tmp/serial.log"
bash "$ROOT_DIR/scripts/create-install-image.sh" "$image"
cp -- "$vars" "$tmp/OVMF_VARS.fd"

set +e
timeout 60s qemu-system-x86_64 \
  -machine q35,accel=tcg -cpu qemu64 -m 512M -smp 1 \
  -display none -vga std -serial "file:$log" -monitor none \
  -no-reboot -no-shutdown \
  -drive "if=pflash,format=raw,readonly=on,file=$code" \
  -drive "if=pflash,format=raw,file=$tmp/OVMF_VARS.fd" \
  -device ich9-ahci,id=ahci \
  -drive "if=none,id=osdisk,file=$image,format=raw" \
  -device ide-hd,drive=osdisk,bus=ahci.0
qemu_status=$?
set -e

if ! grep -q 'STORAGE_SELFTEST: FAT32 VFS kernel read OK' "$log" 2>/dev/null ||
   ! grep -q 'STORAGE_SELFTEST: FAT32 VFS read-only guard OK' "$log" 2>/dev/null; then
  echo "QEMU test failed (status $qemu_status): AHCI/FAT32/VFS readback or read-only guard failed." >&2
  [[ -f "$log" ]] && tail -100 "$log" >&2
  exit 1
fi
printf 'PASS: booted the disposable UEFI image, read through AHCI -> FAT32 -> VFS, and verified the read-only guard.\n'
if [[ -f "$log" ]]; then grep -E 'AHCI:|FAT32:|STORAGE_SELFTEST:' "$log" || true; fi
