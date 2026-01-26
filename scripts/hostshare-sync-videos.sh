#!/bin/bash
set -euo pipefail

HOSTSHARE_DIR="${1:-hostshare}"
VIDEOS_DIR="${HOSTSHARE_DIR}/Videos"
OUT_DIR="${VIDEOS_DIR}/_mjpg"
PLAYLIST="${VIDEOS_DIR}/videos.txt"

if ! command -v ffprobe >/dev/null 2>&1; then
  echo "[HOSTSHARE] ffprobe not found (install ffmpeg)." >&2
  exit 2
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "[HOSTSHARE] ffmpeg not found (install ffmpeg)." >&2
  exit 2
fi

mkdir -p "${VIDEOS_DIR}"
mkdir -p "${OUT_DIR}"

echo "[HOSTSHARE] Scanning ${VIDEOS_DIR} for .avi..."

tmp_playlist="$(mktemp)"
trap 'rm -f "${tmp_playlist}"' EXIT

{
  echo "# Auto-generated playlist for vib-OS (virtio-9p hostshare)"
  echo "#"
  echo "# Drop AVI files into hostshare/Videos/ and (re)run make run-gui/run-gpu."
  echo "# vib-OS currently plays MJPEG-in-AVI; non-MJPEG AVIs will be converted"
  echo "# to MJPEG into: hostshare/Videos/_mjpg/"
  echo "#"
} > "${tmp_playlist}"

shopt -s nullglob nocaseglob
found_any=0
for src in "${VIDEOS_DIR}"/*.avi; do
  found_any=1
  base="$(basename "${src}")"
  # Skip previously converted outputs accidentally copied back.
  if [[ "${base}" == *_mjpg.avi ]]; then
    continue
  fi

  codec="$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name -of default=nw=1:nk=1 "${src}" | head -n 1 || true)"
  if [[ "${codec}" == "mjpeg" ]]; then
    echo "Videos/${base}" >> "${tmp_playlist}"
    continue
  fi

  stem="${base%.*}"
  out="${OUT_DIR}/${stem}_mjpg.avi"
  if [[ ! -f "${out}" || "${src}" -nt "${out}" ]]; then
    echo "[HOSTSHARE] Converting ${base} (${codec}) -> $(basename "${out}")"
    ffmpeg -hide_banner -loglevel error -y -i "${src}" \
      -vf "scale=1024:-2,fps=15" -c:v mjpeg -q:v 5 -an "${out}"
  fi
  echo "Videos/_mjpg/$(basename "${out}")" >> "${tmp_playlist}"
done
shopt -u nocaseglob nullglob

if [[ "${found_any}" == "0" ]]; then
  echo "# (no .avi files found in Videos/)" >> "${tmp_playlist}"
fi

mv "${tmp_playlist}" "${PLAYLIST}"
echo "[HOSTSHARE] Wrote playlist: ${PLAYLIST}"
