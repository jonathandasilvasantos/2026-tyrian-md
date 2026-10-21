# Keep the shipped SGDK toolchain read-only. Place DMA-only graphics in a
# separate physical ROM section; code, tables and audio stay below 3.5 MB.
include $(GDK)/makefile.gen
out/rom.out: CC += -Wl,--section-start=.far_assets=0x380000
