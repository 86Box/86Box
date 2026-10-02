/*
 * FreeDOS(98) PC-9801-27 geometry fallback.
 * Copyright (c) 2026. Distributed under GPL-2.0-or-later, like the kernel.
 * Include before LBA_Get_Drive_Parameters_nec98 in nec98/kernel/initdisk.c
 * and initdsk2.c of fdkernel tag test-20220120-cherrypick.
 */
STATIC BOOL PC9801_27_Parameters(UBYTE drive, iregs *regs)
{
  UWORD work, segment, offset, cylinders;
  UBYTE unit, type, heads;

  /* Preserve every BIOS error and every successful extended response.
   * The old ROM returns successful SENSE status without changing DX. */
  if ((regs->flags & 1) || regs->d.x || !is_daua_sasi(drive))
    return FALSE;
  unit = drive & 3;
  if (unit > 1 || !(peekb(0, 0x55d) & (1 << unit)))
    return FALSE;

  work = 0x5e8 + 4 * unit;
  offset = peekw(0, work);
  segment = peekw(0, work + 2);
  if (segment < 0xc000 || segment > 0xdf00 ||
      peekw(segment, 9) != 0xaa55 || peekb(segment, 11) != 9 ||
      peekw(segment, 0x18) != 0x07e9 || peekb(segment, 0x1a) != 7 ||
      peekw(segment, 0x75f) != 0x08d6)
    return FALSE;

  /* Use the initialized per-unit pointer into this ROM's drive table.
   * Only the -27's connected switch encodings are valid (5/10/20/40 MB). */
  for (type = 0; type < 8; type++)
  {
    if (type != 0 && type != 1 && type != 4 && type != 6)
      continue;
    if (offset == peekw(segment, 0xc7c + 2 * type))
      break;
  }
  if (type == 8 || offset > 0x1000 - 28)
    return FALSE;

  heads = peekb(segment, offset + 3) + 1;
  cylinders = ((UWORD)peekb(segment, offset + 4) << 8) |
               peekb(segment, offset + 5);
  cylinders++;
  if (!heads || heads > 8 || !cylinders || cylinders > 615)
    return FALSE;

  regs->b.x = 256;
  regs->c.x = cylinders;
  regs->d.b.h = heads;
  regs->d.b.l = 33;
  return TRUE;
}
