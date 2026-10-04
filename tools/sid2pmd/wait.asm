; SPDX-License-Identifier: GPL-2.0-or-later
; 386 DOS delay for a PMD playlist. Argument is centiseconds; ESC exits with 1.
bits 16
cpu 386
org 100h
    xor ecx,ecx
    mov si,81h
.parse:
    lodsb
    cmp al,' '
    je .parse
    cmp al,'0'
    jb .ready
    cmp al,'9'
    ja .ready
    sub al,'0'
    movzx eax,al
    imul ecx,ecx,10
    add ecx,eax
    jmp .parse
.ready:
    mov ebp,ecx
    call time
    mov edi,eax
.wait:
    call time
    sub eax,edi
    jns .compare
    add eax,8640000
.compare:
    cmp eax,ebp
    jae .done
    mov ah,0bh
    int 21h
    test al,al
    jz .idle
    mov ah,08h
    int 21h
    cmp al,27
    je .cancel
.idle:
    int 28h
    jmp .wait
.done:
    mov ax,4c00h
    int 21h
.cancel:
    mov ax,4c01h
    int 21h
time:
    mov ah,2ch
    int 21h
    movzx eax,ch
    imul eax,eax,3600
    movzx ebx,cl
    imul ebx,ebx,60
    add eax,ebx
    movzx ebx,dh
    add eax,ebx
    imul eax,eax,100
    movzx ebx,dl
    add eax,ebx
    ret
