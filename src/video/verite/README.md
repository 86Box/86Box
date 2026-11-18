# Rendition Vérité V1000

Extract `PHX49.ROM` from `biosv1-1.ZIP` as `video/verite/phx49.rom` under
an 86Box ROM search path. Select **Rendition Vérité V1000** as the video
card on a PCI machine. The card emulates 4 MiB of VRAM.

The Phoenix ROM runs on the guest x86 CPU. The RISC and pixel engine
come from rust-dos's low-level Vérité implementation, including its
synthetic RISC-side ROM and microcode loader. Native display timing
uses the programmed CRTC totals and a fixed 60 Hz refresh rate.
