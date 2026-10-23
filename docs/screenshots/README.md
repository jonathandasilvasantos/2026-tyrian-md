# Screenshot provenance

The gallery images are **actual 320×224 video frames from the Mega Drive ROM**
running in Genesis Plus GX through its libretro API. They are saved directly
from the emulator's video callback, without retouching, upscaling or compositing.
The screenshot tool does not reconstruct game graphics, patch the ROM or
modify emulated RAM. It navigates the ordinary menus with controller inputs.

- Emulator: [Genesis Plus GX](https://github.com/libretro/Genesis-Plus-GX).
- Core revision: `60bee4c724859710ce05672239e5b601ad01382e`.
- Core build: native macOS / Apple Silicon, `Makefile.libretro`, `platform=osx`.
- First image: `01-title.png`, the port's title screen.
- Following images: opening and later combat frames in episodes 1–4.
- The `episode-*-capture.json` files record the ROM SHA-256, core revision,
  output dimensions and exact display-frame number for each image.

The title is built locally from the original PC title picture/logo, with a
small MD label. Its source image is not bundled in the repository. Tyrian
artwork in these documentary screenshots remains copyrighted by its original
creators; these images are not an asset pack and the engine GPL does not grant
rights to reuse the depicted artwork. See [licensing.md](../licensing.md).

## Reproduce the captures

Build the normal ROM first, following the main README. Then, on macOS:

```sh
git clone --filter=blob:none https://github.com/libretro/Genesis-Plus-GX.git out/genesis-plus-gx-capture
git -C out/genesis-plus-gx-capture checkout --detach 60bee4c724859710ce05672239e5b601ad01382e
make -C out/genesis-plus-gx-capture -f Makefile.libretro platform=osx ARCH=arm64 -j4

for ep in 1 2 3 4; do
  python3 tools/capture_screenshots.py \
    --core out/genesis-plus-gx-capture/genesis_plus_gx_libretro.dylib \
    --rom out/rom.bin --episode "$ep"
done
```

The core and its compiled library stay in ignored `out/`; this repository does
not redistribute the emulator. Each run starts from a fresh emulated console,
selects the requested episode at Normal difficulty, skips introductory text,
launches from the shop, then fires while sweeping left/right. The opening and
combat captures show two different points in that run. The four metadata files
identify the exact source ROM used for the checked-in gallery.
