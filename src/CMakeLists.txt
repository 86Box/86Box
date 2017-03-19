cmake_minimum_required(VERSION 2.8.8)
project(86box)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/CMakeModules)

ENABLE_LANGUAGE(RC)

set(CMAKE_RC_COMPILE_OBJECT "${CMAKE_RC_COMPILER} -O coff -I${CMAKE_CURRENT_SOURCE_DIR} <SOURCE> <OBJECT>")

set(SRCS
386.c 386_dynarec.c 386_dynarec_ops.c 808x.c acer386sx.c acerm3a.c ali1429.c amstrad.c buslogic.c cdrom.c cdrom-ioctl.c cdrom-iso.c
cdrom-null.c codegen.c codegen_ops.c codegen_timing_486.c codegen_timing_686.c codegen_timing_pentium.c codegen_timing_winchip.c compaq.c config.c cpu.c dac.c
device.c disc.c disc_86f.c disc_fdi.c disc_imd.c disc_img.c disc_random.c disc_td0.c dma.c fdc.c fdc37c665.c fdc37c932fr.c fdd.c fdi2raw.c gameport.c hdd.c headland.c i430hx.c i430lx.c i430fx.c
i430nx.c i430vx.c i440fx.c ide.c intel.c intel_flash.c io.c jim.c joystick_ch_flightstick_pro.c joystick_standard.c joystick_sw_pad.c joystick_tm_fcs.c keyboard.c keyboard_amstrad.c keyboard_at.c
keyboard_olim24.c keyboard_pcjr.c keyboard_xt.c lpt.c mcr.c mem.c memregs.c mfm_at.c model.c mouse.c mouse_ps2.c
mouse_serial.c ne2000.c neat.c nethandler.c nmi.c nvr.c olivetti_m24.c opti495.c pc.c pc87306.c pci.c pic.c piix.c pit.c ppi.c ps1.c ps2.c rom.c rtc.c
scat.c scsi.c serial.c sis496.c sis85c471.c sio.c sound.c sound_ad1848.c sound_adlib.c sound_adlibgold.c sound_cms.c
sound_dbopl.cc sound_emu8k.c sound_gus.c sound_mpu401_uart.c sound_opl.c sound_pas16.c sound_ps1.c sound_pssj.c sound_resid.cc
sound_sb.c sound_sb_dsp.c sound_sn76489.c sound_speaker.c sound_ssi2001.c sound_wss.c sound_ym7128.c
soundopenal.c tandy_eeprom.c tandy_rom.c timer.c um8669f.c usb.c vid_ati_eeprom.c vid_ati_mach64.c vid_ati18800.c
vid_ati28800.c vid_ati68860_ramdac.c vid_bt485_ramdac.c vid_cga.c vid_cl_gd.c vid_cl_gd_blit.c vid_cl_ramdac.c vid_colorplus.c vid_ega.c vid_et4000.c
vid_et4000w32.c vid_hercules.c vid_herculesplus.c vid_icd2061.c vid_ics2595.c vid_incolor.c vid_mda.c vid_nv_riva128.c
vid_olivetti_m24.c vid_oti067.c vid_paradise.c vid_pc1512.c vid_pc1640.c vid_pc200.c
vid_pcjr.c vid_ps1_svga.c vid_s3.c vid_s3_virge.c vid_sdac_ramdac.c vid_stg_ramdac.c vid_svga.c
vid_svga_render.c vid_tandy.c vid_tandysl.c vid_tgui9440.c vid_tkd8001_ramdac.c vid_tvga.c vid_unk_ramdac.c
vid_vga.c vid_wy700.c vid_voodoo.c video.c w83877f.c wd76c10.c win.c win-config.c win-d3d.cc win-d3d-fs.cc win-ddraw.cc
win-ddraw-fs.cc win-ddraw-screenshot.cc win-deviceconfig.c win-hdconf.c win-joystick.cc win-joystickconfig.c win-keyboard.cc win-midi.c win-mouse.cc
win-status.c win-video.c x86seg.c x87.c xtide.c pc.rc
dosbox/dbopl.cpp dosbox/nukedopl.cpp dosbox/vid_cga_comp.c
lzf/lzf_c.c lzf/lzf_d.c
resid-fp/convolve.cc resid-fp/convolve-sse.cc resid-fp/envelope.cc resid-fp/extfilt.cc resid-fp/filter.cc resid-fp/pot.cc resid-fp/sid.cc resid-fp/voice.cc resid-fp/wave6581__ST.cc resid-fp/wave6581_P_T.cc resid-fp/wave6581_PS_.cc resid-fp/wave6581_PST.cc resid-fp/wave8580__ST.cc resid-fp/wave8580_P_T.cc resid-fp/wave8580_PS_.cc resid-fp/wave8580_PST.cc resid-fp/wave.cc
slirp/bootp.c slirp/ip_icmp.c slirp/misc.c slirp/socket.c slirp/tcp_timer.c slirp/cksum.c slirp/ip_input.c slirp/queue.c slirp/tcp_input.c slirp/tftp.c slirp/debug.c slirp/ip_output.c slirp/sbuf.c slirp/tcp_output.c slirp/udp.c slirp/if.c slirp/mbuf.c slirp/slirp.c slirp/tcp_subr.c
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_ARCH_64 1)
else()
	set(_ARCH_32 1)
endif()

include(FindOpenAL REQUIRED)
include(FindDirectInput REQUIRED)
include(FindDirectDraw REQUIRED)

if(_ARCH_32)
set(SRCS ${SRCS}
codegen_x86.c
)
else()
set(SRCS ${SRCS}
codegen_x86-64.c
)
endif()

add_definitions(-msse2 -mstackrealign -mwindows -g)

add_executable(86box ${SRCS})

target_link_libraries(86box winmm openal.dll openal ddraw dinput8 dxguid d3d9 d3dx9 wsock32 iphlpapi stdc++ psapi wpcapdelay comdlg32 gdi32)