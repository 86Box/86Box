# SPDX-License-Identifier: GPL-2.0-or-later
# MSCDEX INT 2Fh/1510h probe for drive R: (zero-based drive number 17).
# PROBE.BIN contains 16 records: uint16 sequence, uint16 request status,
# then 16 bytes of IOCTL data (only the command's defined length is valid).
# 1: disc info; 2/3: first/last track; 4: play last track for six seconds;
# 5/6: advancing sub-Q; 7: pause; 8: paused status; 9/10: stationary sub-Q;
# 11: resume; 12: advancing sub-Q; 13: pause; 14: play one second;
# 15: completed status; 16: final position, exactly 75 frames from start.
# Request layouts follow Microsoft's MSCDEX device-driver specification.
.intel_syntax noprefix
.code16
.text
.global _start
_start:
    mov dx,offset filename
    xor cx,cx
    mov ah,0x3c
    int 0x21
    jc exit
    mov word ptr [handle],ax
    # Acknowledge the initial disc change. Older SLCD.SYS versions keep
    # returning 800Fh until the client reads IOCTL 9 (media changed).
    mov al,9
    mov cx,2
    call ioctl_in
    mov byte ptr [recording],1
    mov al,10
    mov cx,7
    call ioctl_in
    mov al,byte ptr [control+2]
    mov byte ptr [last_track],al
    mov byte ptr [control+1],1
    mov al,11
    mov cx,7
    call ioctl_in
    mov al,byte ptr [last_track]
    mov byte ptr [control+1],al
    mov al,11
    mov cx,7
    call ioctl_in
    xor ax,ax
    mov al,byte ptr [control+4]
    mov bx,60
    mul bx
    xor bx,bx
    mov bl,byte ptr [control+3]
    add ax,bx
    mov bx,75
    mul bx
    xor bx,bx
    mov bl,byte ptr [control+2]
    add ax,bx
    sub ax,150
    mov word ptr [start_lba],ax
    mov word ptr [start_lba+2],0
    call play
    mov cx,20
    call wait_ticks
    call qinfo
    mov cx,20
    call wait_ticks
    call qinfo
    mov al,133
    call simple
    call astatus
    call qinfo
    mov cx,20
    call wait_ticks
    call qinfo
    mov al,136
    call simple
    mov cx,20
    call wait_ticks
    call qinfo
    mov al,133
    call simple
    mov word ptr [play_len],75
    call play
    mov cx,30
    call wait_ticks
    call astatus
    call qinfo
    mov bx,word ptr [handle]
    mov ah,0x3e
    int 0x21
exit:
    mov ax,0x4c00
    int 0x21
ioctl_in:
    mov byte ptr [control],al
    mov byte ptr [request],26
    mov byte ptr [request+2],3
    mov byte ptr [request+13],0
    mov word ptr [request+14],offset control
    mov word ptr [request+16],ds
    mov word ptr [request+18],cx
    call send
    ret
qinfo:
    mov al,12
    mov cx,11
    call ioctl_in
    ret
astatus:
    mov al,15
    mov cx,11
    call ioctl_in
    ret
simple:
    mov byte ptr [request],13
    mov byte ptr [request+2],al
    call send
    ret
play:
    mov byte ptr [request],22
    mov byte ptr [request+2],132
    mov byte ptr [request+13],0
    mov ax,word ptr [start_lba]
    mov word ptr [request+14],ax
    mov word ptr [request+16],0
    mov ax,word ptr [play_len]
    mov word ptr [request+18],ax
    mov word ptr [request+20],0
    call send
    ret
send:
    pusha
    push ds
    push es
    push ds
    pop es
    mov bx,offset request
    mov word ptr [request+3],0
    mov cx,17
    mov ax,0x1510
    int 0x2f
    pop es
    pop ds
    popa
    cmp byte ptr [recording],0
    je send_done
    pusha
    inc word ptr [record]
    mov ax,word ptr [request+3]
    mov word ptr [record+2],ax
    push es
    push ds
    pop es
    mov si,offset control
    mov di,offset record+4
    mov cx,16
    rep movsb
    pop es
    mov bx,word ptr [handle]
    mov dx,offset record
    mov cx,20
    mov ah,0x40
    int 0x21
    popa
send_done:
    ret
wait_ticks:
    pusha
    mov si,cx
    xor ah,ah
    int 0x1a
    mov bx,dx
wait_loop:
    sti
    hlt
    xor ah,ah
    int 0x1a
    mov ax,dx
    sub ax,bx
    cmp ax,si
    jb wait_loop
    popa
    ret
filename: .asciz "C:\\PROBE.BIN"
recording: .byte 0
handle: .word 0
last_track: .byte 0
start_lba: .long 0
play_len: .word 450
request: .space 32
control: .space 16
record: .space 20
