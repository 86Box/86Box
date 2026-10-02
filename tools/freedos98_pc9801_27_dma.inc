; FreeDOS(98) compatibility for the old PC-9801-27 DMA setup.
; Copyright (c) 2026. GPL-2.0-or-later, like the kernel.
;
; The ROM rotates ES by four bits, leaving ES bits 0-3 in the unused upper
; nibble of its DMA page byte.  A memory manager can enable 24-bit DMA, making
; those bits a real address.  Express the same buffer with ES aligned to a
; 64 KiB physical boundary and BP as the within-bank offset instead.
; Called only immediately before the transfer INT 1Bh.  Preserve AX/BX/CX/DX
; and DS; return the equivalent ES:BP.  Do not change DMA control ports.
pc9801_27_dma_buffer:
        push ax
        push bx
        push dx
        push ds
        mov bl, al
        and bl, 7eh             ; SASI units 0/1, CHS or LBA DA/UA
        jnz .done
        xor bh, bh
        mov bl, al
        and bl, 1
        shl bx, 1
        shl bx, 1
        xor ax, ax
        mov ds, ax
        mov dx, [bx + 5eah]     ; initialized unit's ROM table segment
        cmp dx, 0c000h
        jb .done
        cmp dx, 0df00h
        ja .done
        mov ds, dx
        cmp word [9], 0aa55h
        jne .done
        cmp byte [11], 9
        jne .done
        cmp word [18h], 07e9h
        jne .done
        cmp byte [1ah], 7
        jne .done
        cmp word [75fh], 08d6h  ; this ROM's legacy AH=84h dispatch
        jne .done

        mov ax, es
        mov dx, ax
        and ax, 0f000h
        and dx, 0fffh
        shl dx, 1
        shl dx, 1
        shl dx, 1
        shl dx, 1
        add dx, bp
        jnc .set_buffer
        add ax, 1000h
        jc .done               ; do not wrap a buffer above physical 1 MiB
.set_buffer:
        mov es, ax
        mov bp, dx
.done:
        pop ds
        pop dx
        pop bx
        pop ax
        ret
