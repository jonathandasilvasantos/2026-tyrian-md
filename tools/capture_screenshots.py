#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Capture actual ROM video through an externally built Genesis Plus GX core.

No game artwork is rendered or reconstructed by this script. It saves the
emulator's video callback and navigates the normal menus with pad inputs.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
BATCH = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL = C.CFUNCTYPE(None)
INPUT = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)


class Variable(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]


class Game(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p), ("size", C.c_size_t), ("meta", C.c_char_p)]


class Emulator:
    def __init__(self, core, rom, directory):
        self.lib = C.CDLL(str(core.resolve()))
        self.directory = str(directory.resolve()).encode()
        directory.mkdir(parents=True, exist_ok=True)
        self.variables = {}
        self.pixel_format = 0
        self.pad = set()
        self.frame = None
        self.frames = 0
        self.captures = []
        self.callbacks = [ENV(self.environment), VIDEO(self.video), AUDIO(lambda l, r: None),
                          BATCH(lambda data, n: n), POLL(lambda: None), INPUT(self.input)]
        for name, callback in zip(("environment", "video_refresh", "audio_sample", "audio_sample_batch",
                                   "input_poll", "input_state"), self.callbacks):
            function = getattr(self.lib, "retro_set_" + name)
            function.argtypes = [type(callback)]
            function(callback)
        self.lib.retro_init()
        self.lib.retro_load_game.argtypes = [C.POINTER(Game)]
        self.lib.retro_load_game.restype = C.c_bool
        self.rom = C.create_string_buffer(rom.read_bytes())
        game = Game(str(rom.resolve()).encode(), C.cast(self.rom, C.c_void_p), len(self.rom) - 1, None)
        if not self.lib.retro_load_game(C.byref(game)):
            raise RuntimeError("Emulator rejected ROM")
        self.lib.retro_set_controller_port_device(0, 1)

    def environment(self, command, data):
        if command in (9, 31):
            C.cast(data, C.POINTER(C.c_char_p))[0] = self.directory
            return True
        if command == 10:
            self.pixel_format = C.cast(data, C.POINTER(C.c_uint))[0]
            return self.pixel_format in (0, 1, 2)
        if command == 15:
            var = C.cast(data, C.POINTER(Variable)).contents
            var.value = self.variables.get(var.key)
            return var.value is not None
        if command == 16:
            variables = C.cast(data, C.POINTER(Variable))
            i = 0
            while variables[i].key:
                self.variables[variables[i].key] = variables[i].value.split(b";", 1)[-1].strip().split(b"|")[0]
                i += 1
            return True
        if command == 17:
            C.cast(data, C.POINTER(C.c_bool))[0] = False
            return True
        if command == 52:
            C.cast(data, C.POINTER(C.c_uint))[0] = 0  # legacy option format
            return True
        return command in (18, 35, 37)

    def input(self, port, device, index, button):
        return int(port == 0 and button in self.pad)

    def video(self, data, width, height, pitch):
        if not data or data == C.c_void_p(-1).value:
            return
        raw = np.frombuffer(C.string_at(data, pitch * height), np.uint8).reshape(height, pitch)
        if self.pixel_format == 1:
            words = raw[:, :width * 4].copy().view(np.uint32).reshape(height, width)
            rgb = np.stack(((words >> 16) & 255, (words >> 8) & 255, words & 255), axis=2)
        else:
            words = raw[:, :width * 2].copy().view(np.uint16).reshape(height, width)
            green_bits = 6 if self.pixel_format == 2 else 5
            red = (words >> (green_bits + 5)) & 31
            green = (words >> 5) & ((1 << green_bits) - 1)
            blue = words & 31
            rgb = np.stack((red * 255 // 31, green * 255 // ((1 << green_bits) - 1), blue * 255 // 31), axis=2)
        self.frame = rgb.astype(np.uint8)

    def run(self, frames, *buttons):
        self.pad = set(buttons)
        for _ in range(frames):
            self.lib.retro_run()
            self.frames += 1

    def press(self, button):
        self.run(3, button)
        self.run(15)

    def save(self, path):
        if self.frame is None:
            raise RuntimeError("No video callback received")
        Image.fromarray(self.frame).save(path)
        self.captures.append({"file": path.name, "frame": self.frames,
                              "width": self.frame.shape[1], "height": self.frame.shape[0]})
        print(f"Captured {path}: {self.frame.shape[1]}x{self.frame.shape[0]}, frame {self.frames}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", required=True, type=Path)
    parser.add_argument("--rom", type=Path, default=Path("out/rom.bin"))
    parser.add_argument("--output", type=Path, default=Path("docs/screenshots"))
    parser.add_argument("--episode", type=int, choices=range(1, 5), default=1)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    emu = Emulator(args.core, args.rom, Path("out/capture-state"))
    emu.run(120)
    if args.episode == 1:
        emu.save(args.output / "01-title.png")
    emu.press(3)  # START: title
    emu.press(0)  # B: new game
    for _ in range(args.episode - 1):
        emu.press(5)  # DOWN: episode selection
    emu.press(0)
    emu.press(0)  # normal difficulty
    # C skips story text until the next scene reset. The shop starts on Launch.
    emu.press(8)  # Mega Drive C (libretro A)
    emu.run(45)
    emu.press(0)  # Launch
    emu.run(180, 0)
    emu.save(args.output / f"episode-{args.episode}-opening.png")
    for i in range(8):
        emu.run(60, 0, 7 if i % 2 == 0 else 6)
    emu.save(args.output / f"episode-{args.episode}-combat.png")
    (args.output / f"episode-{args.episode}-capture.json").write_text(json.dumps({
        "emulator": "Genesis Plus GX (libretro)", "rom_sha256": __import__('hashlib').sha256(args.rom.read_bytes()).hexdigest(),
        "core_revision": subprocess.check_output(["git", "-C", str(args.core.resolve().parent), "rev-parse", "HEAD"], text=True).strip(),
        "episode": args.episode, "frames": emu.frames, "pixel_format": emu.pixel_format,
        "captures": emu.captures,
        "method": "normal menu input and direct emulator video callback; no ROM or RAM patches"
    }, indent=2) + "\n")
    emu.lib.retro_unload_game()
    emu.lib.retro_deinit()


if __name__ == "__main__":
    main()
