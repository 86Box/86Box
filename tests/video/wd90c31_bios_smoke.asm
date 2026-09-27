; Synthetic AT system BIOS for running the supplied option ROMs, not a PC BIOS replacement.
bits 16
cpu 286
org 0
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    cld
    mov di, 0
    mov cx, 256
.ivt:
    mov ax, interrupt
    stosw
    mov ax, 0xf000
    stosw
    loop .ivt
    mov byte [0x410], 0x21
    mov word [0x413], 640
    mov word [0x463], 0x3d4
    mov ax, 0x86
    out 0x80, al
    call 0xc000:3
    cli
    call test_text_scroll
    mov ax, 0x13
    int 0x10
    mov ax, 0xa000
    mov es, ax
    mov word [es:0], 0x1234
    cmp word [es:0], 0x1234
    jne fail_vram
%ifdef TEST_MODE
    mov ax, TEST_MODE
    int 0x10
    mov ax, 0xa000
    mov es, ax
    xor di, di
    mov cx, 32
.fill:
%if TEST_BPP = 8
    mov al, 1
    stosb
%elif TEST_BPP = 15
    mov ax, 0x001f
    stosw
%else
    mov ax, 0x00ff
    stosw
    xor al, al
    stosb
%endif
    loop .fill
%if TEST_BPP = 8
    mov dx, 0x3c8
    mov al, 1
    out dx, al
    inc dx
    xor al, al
    out dx, al
    out dx, al
    mov al, 63
    out dx, al
%endif
    mov cx, 12
    mov dx, 0x3da
.wait_frame:
    in al, dx
    test al, 8
    jnz .wait_frame
.wait_retrace:
    in al, dx
    test al, 8
    jz .wait_retrace
    loop .wait_frame
    call configure_tester
    mov dx, 0xe0
    mov al, 1
    out dx, al
    inc dx
    out dx, al
    call wait_tester
    call read_word
    cmp ax, TEST_WIDTH
    jne fail_width
    call read_word
    cmp ax, TEST_HEIGHT
    jne fail_height
    mov cx, 8
.discard:
    in al, dx
    loop .discard
    dec dx
    mov al, 2
    out dx, al
    inc dx
    mov al, 1
    out dx, al
    xor al, al
    out dx, al
    inc ax
    out dx, al
    xor al, al
    out dx, al
    out dx, al
    out dx, al
    out dx, al
    out dx, al
    call wait_tester
    in al, dx
    cmp al, 255
    jne fail_pixel
    in al, dx
    test al, al
    jnz fail_pixel
    in al, dx
    test al, al
    jnz fail_pixel
%endif
    ; Returning from graphics must restore text-mode memory addressing too.
    call test_text_scroll
    xor bl, bl
exit:
    call configure_tester
    mov dx, 0xe0
    mov al, 4
    out dx, al
    inc dx
    mov al, bl
    out dx, al
.halt:
    hlt
    jmp .halt
fail_scroll:
    mov bl, 1
    jmp exit
fail_vram:
    mov bl, 2
    jmp exit
fail_width:
    mov bl, 3
    jmp exit
fail_height:
    mov bl, 4
    jmp exit
fail_pixel:
    mov bl, 5
    jmp exit
test_text_scroll:
    mov ax, 3
    int 0x10
    mov ax, 0xb800
    mov es, ax
    xor di, di
    mov ax, 0x0741
    mov bp, 25
.fill_row:
    mov cx, 80
    rep stosw
    inc ax
    dec bp
    jnz .fill_row
    mov ax, 0x0601
    mov bh, 0x2e
    xor cx, cx
    mov dx, 0x184f
    int 0x10
    xor di, di
    mov ax, 0x0742
    mov bp, 24
.check_row:
    mov cx, 80
    repe scasw
    jne fail_scroll
    inc ax
    dec bp
    jnz .check_row
    ; The BIOS seeds a blank cell through chain-4, then replicates it with
    ; the BitBLT engine. Check the entire new row, not just the copied text.
    mov ax, 0x2e20
    mov cx, 80
    repe scasw
    jne fail_scroll
    ret
read_word:
    in al, dx
    mov ah, al
    in al, dx
    xchg al, ah
    ret
wait_tester:
    dec dx
.wait:
    in al, dx
    test al, 1
    jz .wait
    inc dx
    ret
configure_tester:
    cli
    mov al, '8'
    out 0x80, al
    mov al, '6'
    out 0x80, al
    mov al, 'B'
    out 0x80, al
    mov al, 'o'
    out 0x80, al
    mov al, 'x'
    out 0x80, al
    mov al, 0xe0
    out 0x80, al
    mov al, 0
    out 0x80, al
    ret
interrupt:
    iret
times 0xfff0-($-$$) db 0xff
    jmp 0xf000:start
times 0x10000-($-$$) db 0xff
