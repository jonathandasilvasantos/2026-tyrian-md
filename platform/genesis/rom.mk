# Keep the shipped SGDK toolchain read-only. Place DMA-only graphics in a
# separate physical ROM section; code, tables and audio stay below 3.5 MB.
include $(GDK)/makefile.gen
out/rom.out: CC += -Wl,--section-start=.far_assets=0x380000
# Recompile only this library translation unit, without changing SGDK.
out/src/gen/sgdk_maths.o: CFLAGS = $(FLAGS) -ffunction-sections -fdata-sections
