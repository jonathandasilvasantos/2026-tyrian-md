# Tyrian → Sega Genesis port — build (see agents.md)
#
#   make setup-sgdk   one-time: check m68k-elf-gcc, patch ./sgdk for GCC 15
#   make gen          tyrian-data/ -> src/gen_data/ (tools/gen.py)
#   make rom          out/rom.bin (SGDK), fails above 8 MB, pads + checksum
#   make rom-demo     ROM that plays itself (AUTOPLAY), for emulator captures
#   make host         out/tyrian_host (src/game on the host)
#   make test         tier 0: converter + host unit tests, ROM build, 68000 boot/parity
#   make bench-68k    CPU load, deepest stack, profile (BENCH_TICKS, REPLAY)
#   make bench-levels frames per level (ideal 3429 for 2000 ticks), all levels
#   make clean

GDK      ?= $(CURDIR)/sgdk
ROM      := out/rom.bin
CC       := cc
NPROC    := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
# Optional compile-time flags; release weapons follow the equipped loadout.
GAME_FLAGS ?=
HOST_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Isrc $(GAME_FLAGS)
ROM_EXTRA_FLAGS := -fno-strict-aliasing -Wno-unterminated-string-initialization $(GAME_FLAGS)
JAVA_HEADLESS_TOOL_OPTIONS := -Djava.awt.headless=true

GAME_SRCS := $(wildcard src/game/*.c) src/gen_data/items.c src/gen_data/level.c src/gen_data/script.c src/gen_data/episode_tables.c src/gen_data/motion.c src/gen_data/shot_shapes.c src/gen_data/difficulty.c src/gen_data/cubes.c
GAME_HDRS := $(wildcard src/game/*.h)

EPISODE     ?= 1
BENCH_TICKS ?= 600
REPLAY      ?= test/replays/boot.txt
MUSASHI_REV := 313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd

.PHONY: all music setup-sgdk gen rom rom-demo host unit-test py-test test bench-68k parity parity-episodes parity-sidekicks parity-checkpoint parity-specials parity-combos parity-story parity-cubes parity-pal clean

all: rom

# ── SGDK setup ───────────────────────────────────────────────────────────────
# ./sgdk is SGDK 2.00 (git tag v2.00).  Its prebuilt libmd.a carries LTO IR
# from an older GCC that GCC 15 rejects: strip the LTO sections (regular
# code stays) and drop the LTO flags from makefile.gen.  Idempotent.
setup-sgdk:
	@which m68k-elf-gcc >/dev/null || { echo "ERROR: m68k-elf-gcc missing: brew install m68k-elf-gcc"; exit 1; }
	@test -f "$(GDK)/makefile.gen" || { echo "ERROR: SGDK not found at $(GDK)"; exit 1; }
	@if [ ! -f "$(GDK)/lib/.lto_stripped" ]; then \
	    rm -rf out/sgdk_strip && mkdir -p out/sgdk_strip && cd out/sgdk_strip && \
	    m68k-elf-ar x "$(GDK)/lib/libmd.a" && \
	    for f in *.o; do m68k-elf-objcopy --remove-section='.gnu.lto_*' --remove-section='.gnu.debuglto_*' "$$f"; done && \
	    rm -f "$(GDK)/lib/libmd.a" && m68k-elf-ar rcs "$(GDK)/lib/libmd.a" *.o && \
	    cd "$(CURDIR)" && rm -rf out/sgdk_strip && touch "$(GDK)/lib/.lto_stripped" && echo "  libmd.a: LTO stripped"; \
	fi
	@perl -i -pe 's/ -fuse-linker-plugin//g; s/ -flto//g; s/ -Wl,-flto//g' "$(GDK)/makefile.gen"
	@perl -0 -i -pe 's/\t\$$\(CP\) \$$\*\.d out\/\$$\*\.d\n\t\$$\(RM\) \$$\*\.d\n/\t\@test -f \$$*.d && \$$(CP) \$$*.d out\/\$$*.d && \$$(RM) \$$*.d || true\n/' "$(GDK)/makefile.gen"
	@grep -q '^\.PRECIOUS: %\.rs' "$(GDK)/makefile.gen" || echo '.PRECIOUS: %.rs' >> "$(GDK)/makefile.gen"
	@echo "  SGDK ready at $(GDK)"

# ── Generated data ──────────────────────────────────────────────────────────
gen:
	@test -f tyrian-data/palette.dat || ./open-tyrian/get_data.sh tyrian-data
	@test -f out/music/song_41.vgm || $(MAKE) --no-print-directory music
	@mkdir -p res/music && cp -p out/music/song_*.vgm res/music/
	@python3 tools/gen.py src/gen_data --episode $(EPISODE)

# ── ROM ──────────────────────────────────────────────────────────────────────
rom: gen
	@test -f "$(GDK)/lib/.lto_stripped" || { echo "ERROR: run make setup-sgdk first"; exit 1; }
	@mkdir -p out
	@# makefile.gen tracks no header dependencies: rebuild all objects when
	@# any header changed since the last ROM build.
	@if [ ! -f out/.rom_stamp ] || [ -n "$$(find src res -newer out/.rom_stamp \( -name '*.h' -o -name '*.res' \) 2>/dev/null | head -1)" ]; then \
	    rm -rf out/src; fi
	@# rescomp writes res/resources.h, which src/ includes: makefile.gen has
	@# no such dependency, so run rescomp alone before the parallel build.
	@# rescomp output depends on every file the .res names (WAV, VGM...)
	@if [ -f res/resources.rs ] && [ -n "$$(find res -newer res/resources.rs -type f ! -name 'resources.*' 2>/dev/null | head -1)" ]; then \
	    rm -f res/resources.rs out/res/resources.o; fi
	@if [ -f res/resources.res ]; then GDK=$(GDK) JAVA_TOOL_OPTIONS="$(JAVA_HEADLESS_TOOL_OPTIONS)" $(MAKE) --no-print-directory -f platform/genesis/rom.mk res/resources.rs; fi
	GDK=$(GDK) JAVA_TOOL_OPTIONS="$(JAVA_HEADLESS_TOOL_OPTIONS)" EXTRA_FLAGS="$(ROM_EXTRA_FLAGS)" \
	    $(MAKE) --no-print-directory -j$(NPROC) -f platform/genesis/rom.mk release
	@python3 tools/check_cart.py out/rom.out
	@touch out/.rom_stamp
	@# Physical cartridge budget is 8 MB; only 4 MB is CPU-visible.
	@# Graphics use the mapper; code/tables/audio stay below 0x380000.
	@python3 -c "import sys; f='$(ROM)'; d=open(f,'rb').read(); t=8*1024*1024; \
	    sys.exit(f'ROM is {len(d)} bytes: over the 8 MB cartridge space') if len(d) > t else None; \
	    open(f,'wb').write(d + b'\xff'*(t-len(d))); \
	    u=len(d.rstrip(b'\xff').rstrip(b'\x00')); print(f'   ROM: {u} bytes used of 8 MB ({100*u//t}%)')"
	@python3 tools/fix_checksum.py $(ROM)

rom-demo:
	@rm -rf out/src
	@$(MAKE) --no-print-directory rom ROM_EXTRA_FLAGS="$(ROM_EXTRA_FLAGS) -DAUTOPLAY"
	@cp $(ROM) out/rom-demo.bin && rm -rf out/src out/.rom_stamp && echo "   demo: out/rom-demo.bin (run make rom again for the normal ROM)"

# ── Host build ───────────────────────────────────────────────────────────────
host: gen
	@$(MAKE) --no-print-directory out/tyrian_host

out/tyrian_host: platform/host/main.c $(GAME_SRCS) $(GAME_HDRS) src/gen_data/items.h Makefile
	@mkdir -p out
	$(CC) $(HOST_CFLAGS) platform/host/main.c $(GAME_SRCS) -o $@

out/test_game: test/test_game.c $(GAME_SRCS) $(GAME_HDRS) Makefile
	@mkdir -p out
	$(CC) $(HOST_CFLAGS) test/test_game.c $(GAME_SRCS) -o $@

out/test_stars: test/test_stars.c src/game/stars.c src/game/stars.h src/game/rng.c src/game/special.h Makefile
	@mkdir -p out
	$(CC) $(HOST_CFLAGS) test/test_stars.c src/game/stars.c src/game/rng.c -o $@

out/test_filter: test/test_filter.c src/game/filter.c src/game/filter.h Makefile
	@mkdir -p out
	$(CC) $(HOST_CFLAGS) test/test_filter.c src/game/filter.c -o $@

out/test_password: test/test_password.c $(GAME_SRCS) $(GAME_HDRS) Makefile
	@mkdir -p out
	$(CC) $(HOST_CFLAGS) test/test_password.c $(GAME_SRCS) -o $@

unit-test: gen
	@$(MAKE) --no-print-directory out/test_game out/test_password out/test_filter out/test_stars
	@./out/test_game
	@./out/test_password
	@./out/test_filter
	@./out/test_stars

# ── 68000 runner (Musashi) ───────────────────────────────────────────────────
out/musashi/m68kops.c:
	@rm -rf out/musashi && git clone -q https://github.com/kstenerud/Musashi.git out/musashi
	@cd out/musashi && git checkout -q $(MUSASHI_REV) && $(CC) -O2 m68kmake.c -o m68kmake && ./m68kmake . m68k_in.c >/dev/null

out/bench68k: tools/bench68k/bench68k.c out/musashi/m68kops.c
	@printf 'void bench_instr_hook(unsigned int pc);\n' > out/musashi/bench_hook.h
	$(CC) -O2 -Iout/musashi -DM68K_INSTRUCTION_HOOK=M68K_OPT_SPECIFY_HANDLER -DM68K_EMULATE_ADDRESS_ERROR=M68K_OPT_ON \
	    '-DM68K_INSTRUCTION_CALLBACK(pc)=bench_instr_hook(pc)' -include out/musashi/bench_hook.h \
	    tools/bench68k/bench68k.c out/musashi/m68kcpu.c out/musashi/m68kops.c \
	    out/musashi/m68kdasm.c out/musashi/softfloat/softfloat.c -o $@

out/rom.syms: $(ROM)
	@m68k-elf-nm -n out/rom.out > $@

bench-68k: out/bench68k out/rom.syms
	@B68K_EPISODE=$(EPISODE) ./out/bench68k $(ROM) out/rom.syms $(BENCH_TICKS) $(REPLAY)

# Every level, 2000 ticks of test/replays/sweep.txt: frames used (ideal
# 3429) and frames over budget.  Manual (minutes), agents.md §7.
bench-levels: out/bench68k out/rom.syms
	@for l in $$(seq 1 18); do printf "level %2d: " $$l; \
	    B68K_LEVEL=$$l ./out/bench68k $(ROM) out/rom.syms 2000 test/replays/sweep.txt | head -1; done

# ROM and host must reach the same state hash on the same replay (§5).
parity: out/bench68k out/rom.syms out/tyrian_host
	@r=$$(B68K_EPISODE=$(EPISODE) ./out/bench68k $(ROM) out/rom.syms $(BENCH_TICKS) $(REPLAY) | tail -1 | awk '{print $$4}'); \
	h=$$(./out/tyrian_host $(BENCH_TICKS) $(REPLAY) $(EPISODE) | awk '{print $$4}'); \
	if [ "$$r" = "$$h" ]; then echo "   PASS  parity ($(BENCH_TICKS) ticks, hash $$h)"; \
	else echo "   FAIL  parity: rom $$r host $$h"; exit 1; fi; \
	if [ "$(BENCH_TICKS):$(REPLAY)" = "600:test/replays/boot.txt" ]; then \
	    e=$$(awk -v ep=$(EPISODE) '$$1 == ep {print $$2}' test/replays/boot.hashes); \
	    if [ "$$h" = "$$e" ]; then echo "   PASS  golden replay (episode $(EPISODE))"; \
	    else echo "   FAIL  golden replay: episode $(EPISODE), expected $$e got $$h"; exit 1; fi; \
	fi

# ── Music tools ─────────────────────────────────────────────────────────────
# lds_dump: OpenTyrian's LDS player + OPL emulator on the host, logs OPL2
# writes per tick (tools/music_vgm.py builds it on demand too).
LDS_DUMP_SRCS := tools/lds_dump/lds_dump.c open-tyrian/src/opl.c open-tyrian/src/memreader.c
out/lds_dump: $(LDS_DUMP_SRCS) open-tyrian/src/lds_play.c
	@mkdir -p out
	$(CC) -O2 -std=c99 $$(sdl2-config --cflags) -Iopen-tyrian/src $(LDS_DUMP_SRCS) -lm -o $@

music: out/lds_dump
	@python3 tools/music_vgm.py out/music

# ── Tier 0 ───────────────────────────────────────────────────────────────────
py-test:
	@python3 -m pytest -q test/tools

parity-episodes: out/bench68k out/rom.syms out/tyrian_host
	@./out/bench68k --self-test-mapper
	@for ep in 1 2 3 4; do $(MAKE) --no-print-directory parity EPISODE=$$ep || exit $$?; done

parity-sidekicks: out/bench68k out/rom.syms out/tyrian_host
	@python3 tools/check_replays.py test/replays/sidekicks.hashes test/replays/sidekicks.txt
	@python3 tools/check_replays.py test/replays/bombs.hashes test/replays/sidekicks.txt

parity-combos: out/bench68k out/rom.syms out/tyrian_host
	@python3 tools/check_replays.py test/replays/combos.hashes test/replays/combos.txt

parity-specials: out/bench68k out/rom.syms out/tyrian_host
	@python3 tools/check_replays.py test/replays/specials.hashes test/replays/sidekicks.txt

# Selected presentation cases: maximum tile usage, warning siren, PCX/red text.
# Their following opening replay must preserve the existing simulation golden.
parity-story: out/bench68k out/rom.syms out/tyrian_host
	@B68K_STORY=3,7 $(MAKE) --no-print-directory parity EPISODE=1
	@B68K_STORY=0,5 $(MAKE) --no-print-directory parity EPISODE=1
	@B68K_STORY=5,0 $(MAKE) --no-print-directory parity EPISODE=4

parity-cubes: out/bench68k out/rom.syms out/tyrian_host
	@B68K_CUBES=1 $(MAKE) --no-print-directory parity-episodes

parity-checkpoint:
	@B68K_CHECKPOINT=1 $(MAKE) --no-print-directory parity-episodes

parity-pal: out/bench68k out/rom.syms out/tyrian_host
	@echo "   PAL replay checks"
	@B68K_PAL=1 $(MAKE) --no-print-directory parity-episodes parity-sidekicks parity-checkpoint parity-specials parity-combos parity-story parity-cubes

test: rom
	@$(MAKE) --no-print-directory py-test out/test_game out/test_password out/test_filter out/test_stars parity-episodes parity-sidekicks parity-checkpoint parity-specials parity-combos parity-story parity-cubes parity-pal
	@./out/test_game
	@./out/test_password
	@./out/test_filter
	@./out/test_stars

clean:
	@rm -rf out
	@echo "Cleaned."
