/* bench68k — headless 68000 runner/profiler for out/rom.bin.
 *
 * Adapted from ~/projects/sgdk/tools/bench68k.  Runs the ROM on the Musashi
 * 68000 core with a minimal Genesis memory map (ROM, RAM, VDP status stub,
 * pads, Z80 bus stub), raises VINT every NTSC frame (127,856 cycles) and
 * reports CPU load (cycles outside the VDP wait loops), the deepest stack and
 * a per-function cycle profile from the ELF symbols.
 *
 * Usage: bench68k rom.bin syms.txt ticks [replay.txt]
 *   ticks     : run until game_tick has been called this many times
 *   replay.txt: lines "<tick> <pad hex>" (pad held from that tick on), the
 *               same format and bits as platform/host (SGDK BUTTON_*).
 *               The pad is written into game_tick's argument on entry, so
 *               the simulation sees exactly the host's inputs whatever the
 *               boot timing. Level transitions/restarts are held during
 *               the replay, matching the host even after death.
 * Output ends with "ticks N hash XXXXXXXX" (the ROM's frame_hash), directly
 * comparable with the host build's "frames N hash XXXXXXXX".
 *
 * Aborts (exit 3) on a CPU exception handler, SYS_die, a CPU read outside the logical 4 MB
 * cartridge space or mapped read past physical ROM, a stack deeper than SGDK's 0xA00 reserve, or no
 * game_tick for B68K_HANG frames (default 600).
 * Environment: B68K_EPISODE=n selects episode n (default ROM menu default),
 * B68K_OPTIONS=left,right equips sidekicks for replay coverage,
 * B68K_PAL=1 selects the 50 Hz/312-line timing model,
 * B68K_LEVEL=n plays level n directly (ROM debug_level),
 * B68K_FUNC=name per-instruction cycle histogram of one function,
 * B68K_TOP=n functions listed (default 20), B68K_FRAMES=1
 * per-frame busy cycles, B68K_PROF=a,b profile only frames a..b-1.
 * Musashi cycle counts; VDP DMA/FIFO stalls and the Z80 are not modelled.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "m68k.h"

static unsigned frame_cycles = 127856u, scanlines = 262u, pal_mode;
#define FRAME_CYC frame_cycles
#define VBLANK_LINE 224u
#define CYC_PER_LINE 488u
#define MAX_SYMS 20000

static uint8_t *rom; static size_t rom_size;
static int mapper_enabled;
static uint8_t cart_banks[8] = {0,1,2,3,4,5,6,7};
static uint8_t vdp_regs[24];
static unsigned vdp_command_hi, vdp_command_pending;
static unsigned dma_transfers;
static uint8_t ram[0x10000];
static uint8_t zram[0x2000];
static unsigned frame_no;
static uint64_t frame_base, total_base;
static uint8_t th[2] = {0x40, 0x40};

typedef struct { uint32_t addr; char name[64]; uint64_t cyc, ins; } Sym;
static Sym *syms; static int nsyms;
static uint32_t idle_lo[16], idle_hi[16]; static int nidle;
static uint32_t prev_pc; static uint64_t prev_cyc, frame_idle;
static uint32_t hash_on_addr, level_addr, episode_addr; static int force_level, force_episode;
static unsigned force_options, option_left, option_right;
static uint32_t loadout_addr, left_option_addr, right_option_addr, bombs_addr;
static unsigned force_bombs, force_checkpoint;
static uint32_t checkpoint_addr;
static uint32_t tick_addr, hash_addr, crash_lo, crash_hi, die_addr, hold_level_addr, main_addr;
static uint64_t ticks, want; static unsigned last_tick_frame;
static uint32_t hf_lo, hf_hi; static uint64_t *hf_cyc;
static int prof_on = 1;
/* main-loop iterations: work from leaving the vblank wait to entering
 * SYS_doVBlankProcess, split by whether a game tick ran */
static uint32_t vbp_lo, vbp_hi; static uint64_t it_start, idle_total, idle_at_start; static int it_ticked;
static uint64_t it_n[2], it_sum[2], it_max[2], it_over[2]; static unsigned prof_a, prof_b = ~0u;
static uint32_t min_sp = 0xFFFFFFFF, min_sp_pc; static unsigned min_sp_frame;

typedef struct { unsigned tick, pad; } Cmd;
static Cmd cmds[4096]; static int ncmd, ci;
static unsigned pad;

static uint64_t now_cycles(void) { return total_base + (uint64_t)m68k_cycles_run(); }

static int sym_index(uint32_t pc) {
    int lo = 0, hi = nsyms - 1, r = -1;
    while (lo <= hi) { int mid = (lo + hi) / 2; if (syms[mid].addr <= pc) { r = mid; lo = mid + 1; } else hi = mid - 1; }
    return r;
}
static const char *sym_name(uint32_t pc) { int s = sym_index(pc); return s >= 0 ? syms[s].name : "?"; }
static unsigned rd8(unsigned a);
static void wr8(unsigned a, unsigned v);
static uint32_t rd32(unsigned a) { return (uint32_t)rd8(a) << 24 | (uint32_t)rd8(a + 1) << 16 | (uint32_t)rd8(a + 2) << 8 | rd8(a + 3); }

static void crash_report(const char *why, uint32_t pc) {
    unsigned sp = m68k_get_reg(NULL, M68K_REG_SP) & 0xFFFFFF, a; int n = 0;
    printf("CRASH frame %u: %s (pc %06x %s)\n", frame_no, why, pc, sym_name(pc));
    printf("  deepest stack so far: sp %06x (%u bytes) at %s, frame %u\n", min_sp, 0x1000000u - min_sp, sym_name(min_sp_pc), min_sp_frame);
    if (!strcmp(why, "_Bus_Error") || !strcmp(why, "_Address_Error"))
        printf("  access %06x, faulting pc %06x %s\n", rd32(sp + 2) & 0xFFFFFF, rd32(sp + 10) & 0xFFFFFF, sym_name(rd32(sp + 10) & 0xFFFFFF));
    printf("  sp %06x; return addresses on the stack:\n", sp);
    for (a = sp; a < 0x1000000 && n < 24; a += 2) {
        uint32_t v = rd32(a);
        if (v > 0x200 && v < 0x400000 && !(v & 1) && sym_index(v) >= 0 && v != syms[sym_index(v)].addr) { printf("    [%06x] %06x %s\n", a, v, sym_name(v)); n++; }
    }
    fflush(stdout); exit(3);
}
static int is_idle(uint32_t pc) {
    int i; for (i = 0; i < nidle; i++) if (pc >= idle_lo[i] && pc < idle_hi[i]) return 1;
    return 0;
}

void bench_instr_hook(unsigned int pc) {
    uint64_t c = now_cycles(), d = c - prev_cyc;
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP) & 0xFFFFFF;
    if (sp >= 0xFF0000 && sp < min_sp) { min_sp = sp; min_sp_pc = pc; min_sp_frame = frame_no;
        /* SGDK's heap ends at MEMORY_HIGH = 0xE1000000 - STACK_SIZE (0xA00) */
        if (sp < 0xFFF600) crash_report("stack overflow into the heap (SGDK STACK_SIZE 0xA00)", pc); }
    if (is_idle(prev_pc)) { frame_idle += d; idle_total += d; }
    else if (prof_on) { int s = sym_index(prev_pc); if (s >= 0) { syms[s].cyc += d; syms[s].ins++; }
        if (hf_cyc && prev_pc >= hf_lo && prev_pc < hf_hi) hf_cyc[(prev_pc - hf_lo) >> 1] += d; }
    if (pc == vbp_lo) {   /* entry of SYS_doVBlankProcess: one main-loop iteration done */
        if (it_start && ticks) {
            uint64_t w = (c - it_start) - (idle_total - idle_at_start);
            it_n[it_ticked]++; it_sum[it_ticked] += w; if (w > it_max[it_ticked]) it_max[it_ticked] = w;
            if (w > FRAME_CYC - 8000) it_over[it_ticked]++;
        }
        it_start = c; idle_at_start = idle_total; it_ticked = 0;
    }
    if (pc == main_addr) {
        if (bombs_addr) wr8(bombs_addr, force_bombs);
        if (checkpoint_addr) wr8(checkpoint_addr, force_checkpoint);
        if (force_options && loadout_addr && left_option_addr && right_option_addr) {
            wr8(loadout_addr, 1); wr8(left_option_addr, option_left); wr8(right_option_addr, option_right);
        }
        if (force_episode && episode_addr) wr8(episode_addr, (unsigned)force_episode);
        if (force_level && level_addr) wr8(level_addr, (unsigned)force_level);
    }
    if (pc == tick_addr) {
        it_ticked = 1;
        if (hold_level_addr) wr8(hold_level_addr, 1);
        /* game_tick(u16 pad): int-promoted stack argument, low word at sp+6 */
        while (ci < ncmd && cmds[ci].tick <= ticks) pad = cmds[ci++].pad;
        /* hashing is off in normal runs; switch it on before the last tick
         * so frame_hash is fresh and the profile is not polluted */
        if (hash_on_addr) wr8(hash_on_addr, ticks + 1 >= want ? 1 : 0);
        wr8(sp + 6, (pad >> 8) & 0xFF); wr8(sp + 7, pad & 0xFF);
        ticks++; last_tick_frame = frame_no;
        /* entry of tick want+1: frame_hash covers exactly want ticks */
        if (ticks > want) m68k_end_timeslice();
    }
    if ((crash_lo && pc >= crash_lo && pc < crash_hi) || (die_addr && pc == die_addr)) crash_report(sym_name(pc), prev_pc);
    prev_pc = pc; prev_cyc = c;
}

static unsigned vcount(void) { return (unsigned)(((now_cycles() - frame_base) / CYC_PER_LINE) % scanlines); }

static unsigned pad_read(int port) {
    unsigned m = port ? 0 : pad;
    if (th[port] & 0x40)   /* TH=1: ? 1 C B R L D U (active low) */
        return 0x40 | (~m & 0x3F);
    /* TH=0: ? 0 S A 0 0 D U */
    return ((m & 0x01) ? 0 : 1) | ((m & 0x02) ? 0 : 2) | ((m & 0x40) ? 0 : 0x10) | ((m & 0x80) ? 0 : 0x20);
}

static unsigned rd8(unsigned a) {
    a &= 0xFFFFFF;
    /* 0x400000-0x7FFFFF is the expansion area: open bus on a cartridge. */
    if (a >= 0x400000 && a < 0x800000) crash_report("read past the 4 MB cartridge space", m68k_get_reg(NULL, M68K_REG_PPC));
    if (a < 0x400000) {
        unsigned physical = mapper_enabled ? ((unsigned)cart_banks[a >> 19] << 19) | (a & 0x7FFFF) : a;
        if (mapper_enabled && physical >= rom_size) crash_report("mapper read past physical ROM", m68k_get_reg(NULL, M68K_REG_PPC));
        return physical < rom_size ? rom[physical] : 0xFF;
    }
    if (a >= 0xE00000) return ram[a & 0xFFFF];
    if (a >= 0xA00000 && a < 0xA10000) { if ((a & 0x7FFF) < 0x2000) return zram[a & 0x1FFF] | ((a & 0x1FFF) == 0x102 ? 0x80 : 0); return 0; }
    if (a >= 0xA10000 && a < 0xA10020) {
        switch (a & 0x1F) { case 0x01: return pal_mode ? 0xE0 : 0xA0; case 0x03: return pad_read(0); case 0x05: return pad_read(1); default: return 0; }
    }
    if (a == 0xA11100 || a == 0xA11101) return 0;   /* Z80 bus granted */
    if (a >= 0xC00000 && a < 0xC00020) {
        unsigned r = a & 0x1F;
        if (r == 4 || r == 6) return 0x34;
        if (r == 5 || r == 7) return 0x02 | pal_mode | (vcount() >= VBLANK_LINE ? 0x8 : 0);
        if (r == 8) return vcount() & 0xFF;
    }
    return 0;
}
unsigned int m68k_read_memory_8(unsigned int a) { return rd8(a); }
unsigned int m68k_read_memory_16(unsigned int a) {
    a &= 0xFFFFFF;
    if (a >= 0xC00000 && a < 0xC00020) {
        unsigned r = a & 0x1F, v = vcount();
        if (r == 4 || r == 6) return 0x3600 | pal_mode | (v >= VBLANK_LINE ? 0x8 : 0);   /* FIFO empty, no DMA */
        if (r == 8) return (v & 0xFF) << 8;
        return 0;
    }
    return (rd8(a) << 8) | rd8(a + 1);
}
unsigned int m68k_read_memory_32(unsigned int a) { return (m68k_read_memory_16(a) << 16) | m68k_read_memory_16(a + 2); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }

static void wr8(unsigned a, unsigned v) {
    a &= 0xFFFFFF;
    if (mapper_enabled && a >= 0xA130F3 && a <= 0xA130FF && (a & 1)) {
        cart_banks[(a - 0xA130F1) >> 1] = v & 0x3F;
        return;
    }
    if (a >= 0xE00000) { ram[a & 0xFFFF] = (uint8_t)v; return; }
    if (a >= 0xA00000 && a < 0xA10000) { if ((a & 0x7FFF) < 0x2000) zram[a & 0x1FFF] = (uint8_t)v; return; }
    if (a == 0xA10003) { th[0] = (uint8_t)v; return; }
    if (a == 0xA10005) { th[1] = (uint8_t)v; return; }
}
void m68k_write_memory_8(unsigned int a, unsigned int v) { wr8(a, v); }
static void vdp_control(unsigned v) {
    if ((v & 0xC000) == 0x8000) {
        unsigned reg = (v >> 8) & 31;
        if (reg < 24) vdp_regs[reg] = v;
        vdp_command_pending = 0;
    } else if (!vdp_command_pending) {
        vdp_command_hi = v;
        vdp_command_pending = 1;
    } else {
        vdp_command_pending = 0;
        if ((v & 0x80) && (vdp_regs[1] & 0x10) && !(vdp_regs[23] & 0x80)) {
            unsigned length = (vdp_regs[20] << 8) | vdp_regs[19];
            unsigned source = ((vdp_regs[23] << 16) | (vdp_regs[22] << 8) | vdp_regs[21]) << 1;
            if (!length) length = 65536;
            if ((source & 0x1FFFF) + length * 2 > 0x20000)
                crash_report("VDP DMA source crosses 128 KB boundary", m68k_get_reg(NULL, M68K_REG_PPC));
            (void)rd8(source);
            (void)rd8(source + length * 2 - 1);
            dma_transfers++;
        }
    }
}
void m68k_write_memory_16(unsigned int a, unsigned int v) {
    if ((a & 0xFFFFFC) == 0xC00004) { vdp_control(v); return; }
    wr8(a, v >> 8); wr8(a + 1, v & 0xFF);
}
void m68k_write_memory_32(unsigned int a, unsigned int v) { m68k_write_memory_16(a, v >> 16); m68k_write_memory_16(a + 2, v & 0xFFFF); }

static void load_syms(const char *path) {
    static const char *idle[] = { "VDP_waitVInt", "VDP_waitVBlank", "VDP_waitVActive", "VDP_waitVSync",
                                  "waitMs", "waitSubTick", "waitTick", "SYS_isInVInt", "addFrameLoad" };
    char line[256]; FILE *fp = fopen(path, "r"); int i, k;
    if (!fp) { perror(path); exit(1); }
    syms = calloc(MAX_SYMS, sizeof(Sym));
    while (fgets(line, sizeof line, fp) && nsyms < MAX_SYMS) {
        unsigned addr; char t, name[64];
        if (sscanf(line, "%x %c %63s", &addr, &t, name) != 3) continue;
        if (!strcmp(name, "main")) main_addr = addr;
        if (!strcmp(name, "frame_hash")) hash_addr = addr;
        if (!strcmp(name, "debug_hold_level")) hold_level_addr = addr;
        if (!strcmp(name, "hash_enabled")) hash_on_addr = addr;
        if (!strcmp(name, "debug_level")) level_addr = addr;
        if (!strcmp(name, "debug_episode")) episode_addr = addr;
        if (!strcmp(name, "debug_checkpoint")) checkpoint_addr = addr;
        if (!strcmp(name, "debug_bombs")) bombs_addr = addr;
        if (!strcmp(name, "debug_loadout")) loadout_addr = addr;
        if (!strcmp(name, "debug_left_option")) left_option_addr = addr;
        if (!strcmp(name, "debug_right_option")) right_option_addr = addr;
        if ((t == 'T' || t == 't') && addr < 0x800000) { syms[nsyms].addr = addr; strcpy(syms[nsyms].name, name); nsyms++; }
    }
    fclose(fp);
    for (i = 0; i < nsyms - 1; i++) {
        const char *n = syms[i].name;
        if (!strcmp(n, "game_tick")) tick_addr = syms[i].addr;
        if (!strcmp(n, "_Bus_Error")) crash_lo = syms[i].addr;
        if (!strcmp(n, "_INT")) crash_hi = syms[i].addr;
        if (!strcmp(n, "SYS_die")) die_addr = syms[i].addr;
        if (!strcmp(n, "SYS_doVBlankProcess")) { vbp_lo = syms[i].addr; vbp_hi = syms[i + 1].addr; }
        for (k = 0; k < (int)(sizeof idle / sizeof *idle); k++)
            if (!strcmp(n, idle[k]) && nidle < 16) { idle_lo[nidle] = syms[i].addr; idle_hi[nidle] = syms[i + 1].addr; nidle++; }
    }
    if (!tick_addr || !hash_addr) { fprintf(stderr, "game_tick / frame_hash not in %s\n", path); exit(1); }
}

static int mapper_self_test(void) {
    unsigned bank, i;
    rom_size = 0x800000;
    rom = malloc(rom_size);
    if (!rom) return 1;
    mapper_enabled = 1;
    for (i = 0; i < rom_size; i++) rom[i] = (i >> 19) ^ (i & 0xFF);
    for (bank = 0; bank < 16; bank++) {
        wr8(0xA130FD, bank);
        wr8(0xA130FF, 15 - bank);
        if (rd8(0x300000) != bank || rd8(0x37FFFF) != (bank ^ 0xFF) ||
            rd8(0x380000) != 15 - bank || rd8(0x3FFFFF) != ((15 - bank) ^ 0xFF) ||
            rd8(0) != 0 || rd8(0x280000) != 5) {
            fprintf(stderr, "FAIL mapper bank %u\n", bank);
            free(rom); return 1;
        }
    }
    wr8(0xA130FD, 6);
    for (bank = 0; bank < 16; bank++) {
        wr8(0xA130FF, bank);
        if (rd8(0x380000) != bank || rd8(0x300000) != 6 || rd8(0x37FFFF) != (6 ^ 0xFF)) {
            fprintf(stderr, "FAIL fixed 3.5 MB region\n");
            free(rom); return 1;
        }
    }
    free(rom);
    printf("   PASS  SSF2 mapper: all 16 banks, boundaries and fixed region\n");
    return 0;
}

int main(int argc, char **argv) {
    FILE *fp; char line[256]; int i;
    uint64_t sum_busy = 0, max_busy = 0, ticks_before = 0, busy_t = 0, busy_n = 0, max_t = 0, max_n = 0;
    unsigned nb = 0, over = 0, hang, over_t = 0, n_t = 0, n_n = 0;
    if (argc == 2 && !strcmp(argv[1], "--self-test-mapper")) return mapper_self_test();
    if (argc < 4) { fprintf(stderr, "usage: bench68k rom.bin syms.txt ticks [replay.txt]\n"); return 2; }
    fp = fopen(argv[1], "rb"); if (!fp) { perror("rom"); return 1; }
    fseek(fp, 0, SEEK_END); rom_size = (size_t)ftell(fp); fseek(fp, 0, SEEK_SET);
    rom = malloc(rom_size); if (fread(rom, 1, rom_size, fp) != rom_size) return 1; fclose(fp);
    mapper_enabled = rom_size >= 0x108 && !memcmp(rom + 0x100, "SEGA SSF", 8);
    load_syms(argv[2]);
    want = (uint64_t)atoll(argv[3]);
    if (argc > 4) {
        fp = fopen(argv[4], "r"); if (!fp) { perror("replay"); return 1; }
        while (fgets(line, sizeof line, fp) && ncmd < 4096)
            if (line[0] != '#' && sscanf(line, "%u %x", &cmds[ncmd].tick, &cmds[ncmd].pad) == 2) ncmd++;
        fclose(fp);
    }
    if (getenv("B68K_FUNC")) for (i = 0; i < nsyms - 1; i++) if (!strcmp(syms[i].name, getenv("B68K_FUNC"))) {
        hf_lo = syms[i].addr; hf_hi = syms[i + 1].addr; hf_cyc = calloc((hf_hi - hf_lo) / 2 + 1, 8); }
    force_checkpoint = getenv("B68K_CHECKPOINT") && atoi(getenv("B68K_CHECKPOINT"));
    if (getenv("B68K_BOMBS")) {
        force_bombs = atoi(getenv("B68K_BOMBS"));
        if (force_bombs > 10) { fprintf(stderr, "B68K_BOMBS must be 0..10\n"); return 2; }
    }
    if (getenv("B68K_OPTIONS")) {
        if (sscanf(getenv("B68K_OPTIONS"), "%u,%u", &option_left, &option_right) != 2 || option_left > 30 || option_right > 30) {
            fprintf(stderr, "B68K_OPTIONS requires two IDs 0..30\n"); return 2;
        }
        force_options = 1;
    }
    if (getenv("B68K_EPISODE")) force_episode = atoi(getenv("B68K_EPISODE"));
    if (getenv("B68K_LEVEL")) force_level = atoi(getenv("B68K_LEVEL"));
    if (getenv("B68K_PROF")) sscanf(getenv("B68K_PROF"), "%u,%u", &prof_a, &prof_b);
    hang = getenv("B68K_HANG") ? (unsigned)atoi(getenv("B68K_HANG")) : 600;

    if (getenv("B68K_PAL") && atoi(getenv("B68K_PAL"))) {
        pal_mode = 1; scanlines = 312; frame_cycles = scanlines * CYC_PER_LINE;
    }
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_pulse_reset();
    for (frame_no = 0; ticks <= want; frame_no++) {
        uint64_t busy;
        if (frame_no - last_tick_frame > hang) crash_report("hang (no game_tick)", m68k_get_reg(NULL, M68K_REG_PC));
        if (force_episode && episode_addr) wr8(episode_addr, (unsigned)force_episode);
        if (force_level && level_addr) wr8(level_addr, (unsigned)force_level);
        /* menus before the first level read the pad directly: tap START */
        if (!ticks) pad = frame_no % 30 < 3 ? 0x80 : 0;
        frame_base = total_base; frame_idle = 0;
        ticks_before = ticks;
        prof_on = frame_no >= prof_a && frame_no < prof_b;
        m68k_set_irq(6);
        total_base += (uint64_t)m68k_execute(200);   /* VINT level-held only briefly */
        m68k_set_irq(0);
        while (total_base - frame_base < FRAME_CYC && ticks <= want)
            total_base += (uint64_t)m68k_execute((int)(FRAME_CYC - (total_base - frame_base)));
        if (ticks > want) break;
        busy = FRAME_CYC - (frame_idle > FRAME_CYC ? FRAME_CYC : frame_idle);
        if (ticks) { sum_busy += busy; nb++; if (busy > max_busy) max_busy = busy; if (!frame_idle) over++;
            if (ticks != ticks_before) { busy_t += busy; n_t++; if (busy > max_t) max_t = busy; if (!frame_idle) over_t++; }
            else { busy_n += busy; n_n++; if (busy > max_n) max_n = busy; } }
        if (getenv("B68K_FRAMES")) printf("frame %u busy %llu (%.1f%%)\n", frame_no, (unsigned long long)busy, 100.0 * busy / FRAME_CYC);
    }
    printf("frames %u (in game loop %u): avg busy %.0f cycles (%.1f%%), max %llu (%.1f%%), frames without idle: %u\n",
           frame_no, nb, nb ? (double)sum_busy / nb : 0.0, nb ? 100.0 * sum_busy / nb / FRAME_CYC : 0.0,
           (unsigned long long)max_busy, 100.0 * max_busy / FRAME_CYC, over);
    printf("tick frames %u: avg %.0f max %llu over %u | other frames %u: avg %.0f max %llu\n", n_t, n_t ? (double)busy_t / n_t : 0.0,
           (unsigned long long)max_t, over_t, n_n, n_n ? (double)busy_n / n_n : 0.0, (unsigned long long)max_n);
    printf("loop work per iteration (minus waits): tick %llu avg %.0f max %llu near-over %llu | no tick %llu avg %.0f max %llu near-over %llu\n",
           (unsigned long long)it_n[1], it_n[1] ? (double)it_sum[1] / it_n[1] : 0.0, (unsigned long long)it_max[1], (unsigned long long)it_over[1],
           (unsigned long long)it_n[0], it_n[0] ? (double)it_sum[0] / it_n[0] : 0.0, (unsigned long long)it_max[0], (unsigned long long)it_over[0]);
    printf("deepest stack: sp %06x (%u bytes used; SGDK reserves 0xA00) at %s, frame %u\n",
           min_sp, 0x1000000u - min_sp, sym_name(min_sp_pc), min_sp_frame);
    {
        uint64_t tot = 0; int k, top = getenv("B68K_TOP") ? atoi(getenv("B68K_TOP")) : 20;
        for (i = 0; i < nsyms; i++) tot += syms[i].cyc;
        for (k = 0; k < top; k++) {
            int best = -1; for (i = 0; i < nsyms; i++) if (syms[i].cyc && (best < 0 || syms[i].cyc > syms[best].cyc)) best = i;
            if (best < 0) break;
            printf("  %5.1f%%  %10llu cyc/frame %6.1f cyc/instr  %s\n", 100.0 * syms[best].cyc / (tot ? tot : 1),
                   (unsigned long long)(syms[best].cyc / (frame_no ? frame_no : 1)), (double)syms[best].cyc / syms[best].ins, syms[best].name);
            syms[best].cyc = 0;
        }
    }
    if (hf_cyc) { uint64_t t = 0; uint32_t a; for (a = hf_lo; a < hf_hi; a += 2) t += hf_cyc[(a - hf_lo) >> 1];
        for (a = hf_lo; a < hf_hi; a += 2) if (hf_cyc[(a - hf_lo) >> 1] * 100 > t)
            printf("  pc %06x %5.1f%%\n", a, 100.0 * hf_cyc[(a - hf_lo) >> 1] / t); }
    printf("validated DMA transfers: %u\n", dma_transfers);
    printf("ticks %llu hash %08x\n", (unsigned long long)want, rd32(hash_addr));
    return 0;
}
