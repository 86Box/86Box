#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Boot an external CCD.SYS 1.07 driver and verify DOS CD-ROM file reads.

Uses the same disposable PB430 guest and media fixtures as teac_cda_smoke.
Test both creative_cd200 and funai_cd200f. CCD.SYS uses /T:2 for a Creative
port layout and /T:3 for the standard Panasonic layout. CRCCD.SYS 1.02 can
also be supplied, but supports only the Creative layout: use --modes creative.
No vendor binaries or firmware are distributed. See doc/hardware/cd200-cdrom.md.
"""
import sys

sys.dont_write_bytecode = True
import teac_cda_smoke as smoke

smoke.CARDS = {
    "creative": ("mkecd", "MKE/Panasonic interface (Creative)", "230", "/P:230 /T:2"),
    "panasonic": ("mkecd_normal", "MKE/Panasonic interface", "340", "/P:340 /T:3"),
}

if __name__ == "__main__":
    raise SystemExit(smoke.main(("creative_cd200", "funai_cd200f"), __doc__,
                               "CCD.SYS", "CD200", media_modes=("iso",)))
