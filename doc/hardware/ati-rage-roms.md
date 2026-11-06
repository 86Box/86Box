# ATI Rage Pro family video BIOSes

The add-in Rage Pro AGP/PCI, Rage XL PCI and Rage LT Pro AGP devices
select their video BIOS through **BIOS Revision**. Place the following
files from [the ROM repository's ragepro branch](https://github.com/Cacodemon345/roms/tree/ragepro)
in `roms/video/mach64/`:

| Device | Filename | BIOS memory type | Image size |
| --- | --- | --- | --- |
| Rage Pro AGP | `ragepro_agp_turbo_3.091.bin` | SDRAM | 32 KB |
| Rage Pro AGP | `ragepro_agp_113-49801-101_2000.bin` | SDRAM | 32 KB |
| Rage Pro AGP | `ragepro_agp_113-49801-101_1998.bin` | SDRAM | 32 KB |
| Rage Pro AGP | `ragepro_agp_113-40212-103.bin` | SGRAM | 32 KB |
| Rage Pro PCI | `ragepro_pci_113-41901-104.bin` | SGRAM | 32 KB |
| Rage XL PCI | `ragexl_pci_113-72301-101.bin` | SDRAM | 32 KB |
| Rage LT Pro AGP | `ragelt_agp_113-55702-100.bin` | SDRAM | 58 KB |

The BIOS configuration's `local` value stores CFG_MEM_TYPE (4 for SDRAM,
5 for SGRAM). The selected Rage Pro BIOS determines the memory strap;
the onboard device retains its SDRAM strap and uses the system BIOS.

The LT Pro file at ROM repository commit
`95f1af675dfebdef3bacdbd5dd95fb735d03cdf2` is 59,392 bytes. Its legacy
ROM header declares 0x74 blocks of 512 bytes, and those bytes sum to zero
modulo 256. Its PCI data structure declares a 64 KB image. The emulator
maps a 64 KB ROM window and fills the unused 6 KB with 0xff, preserving
both the original image and a zero checksum over the complete window.
No checksum repair is needed. The XL's 32 KB image likewise occupies the
lower half of a 64 KB ROM window.

All seven images were checked against that commit's Git blob hashes,
legacy option ROM signatures and checksums, and PCI vendor/device IDs.
This is image validation, not a guest boot compatibility test.
