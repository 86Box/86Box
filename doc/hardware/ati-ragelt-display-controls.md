# ATI J5.30 advanced display controls: scaler corruption

On Windows 98 SE with J5.30.1-B02 (4.13.1.2655), Display Properties → Settings → Advanced → ATI Color showed striped black/white borders, broken RGB buttons, slider graphics and Defaults button. The color wheel and ordinary Windows controls rendered correctly. The isolated LT Pro VM reproduced it at 800x600x32.

The driver alternates CI8 palette-cache uploads with front-end scaler bitmap expansion. It writes the Rage Pro `SCALE_OFF_ACC` alias at block-0 offset 0x388 to set the next bitmap source and restart the accumulators. The emulator only stored the alias, so the scaler retained the palette staging address and fractional state. Implementing the observed alias effects removes the corruption; ordinary `SCALE_OFF` writes retain their existing behavior.

Validation on 2026-11-04:

- ATI Displays, Color, Options and Overlay pages render cleanly at 32 bpp.
- ATI Color also renders cleanly at 16 bpp.
- `Mach64.Palette` regression checks actual CI8 bitmap pixels after stale source/fraction state, using byte, word and dword writes. Without the fix it reports 33 failed checks; with the fix zero.
- All 11 Mach64 CTest entries pass. Temporary diagnostic logging was removed.
- The private VM's SLiRP mapping is host TCP 9871 → guest TCP 9869, avoiding the unrelated guest on host port 9869.

The register's address is listed in ATI's [LT Pro Register Reference Guide](https://www.bitsavers.org/components/ati/RAGE_PRO/RRG-G03300_Rev2.01x_3D_RAGE_LT_PRO_Register_Reference_Guide_199804.pdf). The alias side effects here are supported by the traced J5.30 sequence and the resulting rendered pixels; this does not establish untested reserved-bit behavior.
