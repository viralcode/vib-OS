Host Share (QEMU)

This folder is shared into vib-OS when using `make run-gui` or `make run-gpu`.

In Vib-OS, browse the host share in the File Manager at:
  /Host/
or directly to the videos folder at:
  /Videos/Host/

Put MJPEG-encoded AVI files into:
  hostshare/Videos/

On boot, vib-OS will import entries listed in:
  hostshare/Videos/videos.txt
into the in-OS folder:
  /Videos/
If an entry points at a converted file in `Videos/_mjpg/`, vib-OS will import it
under the original name (drops the `_mjpg` suffix) so it looks like a normal
`*.avi` inside `/Videos/`.

If you’re putting regular AVI files (like H.264) into `hostshare/Videos/`, use
`make run-gui` / `make run-gpu` from this repo: it runs
`scripts/hostshare-sync-videos.sh` which auto-converts them to MJPEG AVI into:
  hostshare/Videos/_mjpg/
and regenerates `hostshare/Videos/videos.txt`.

The video player will still try (in order):
  - /Host/Videos/videos.txt   (optional playlist)
  - /Host/Videos/demo.avi
  - /Videos/demo.avi          (built-in fallback)

Playlist format:
  - Create `hostshare/Videos/videos.txt`
  - One path per line, relative to the share root, e.g.:
      Videos/demo.avi
      Videos/clip2.avi
  - Lines starting with `#` are ignored
