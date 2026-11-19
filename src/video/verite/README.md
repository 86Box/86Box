# Rendition Vérité V1000

Extract `PHX49.ROM` from `biosv1-1.ZIP` as `video/verite/phx49.rom` under
an 86Box ROM search path. Select **Rendition Vérité V1000** as the video
card on a PCI machine. The card emulates 4 MiB of VRAM.

The Phoenix ROM runs on the guest x86 CPU. The RISC and pixel engine
come from rust-dos's low-level Vérité implementation, including its
synthetic RISC-side ROM and microcode loader. Native display timing
uses the programmed CRTC totals and the pixel clock latched through the
V1000's serial PLL port.

The emulated monitor exposes EDID through the VGA connector's DDC1 and DDC2
paths. Native scanout supports Bt485 palette addressing, interlaced fields,
and frame-sequential stereo from the two framebuffer bases.
The DRAM controller accounts for refresh and slow precharge in the RISC
cycle budget using approximate EDO timing.

On x86-64 hosts, the RISC runs cached native blocks for integer and paired
arithmetic, branches, and common local VRAM accesses. The cache checks
instruction bytes before entering a block so microcode changes take effect.
FIFO, device and pixel-engine operations use the interpreter, which also
runs the RISC on other hosts or when executable memory is unavailable.
