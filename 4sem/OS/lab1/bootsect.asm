MENU_TAB equ 2
[BITS 16]
[ORG 0x7C00]

start:
  mov ax, cs
  mov ds, ax
  mov ss, ax
  mov sp, start
  mov ah, 0x0e
  
  mov dl, 1
  mov ah, 0
  int 0x13

  mov ax, 0x1000
  mov es, ax
  mov bx, 0

  mov ah, 0x02
  mov al, 1
  mov ch, 0
  mov cl, 1
  mov dh, 0
  mov dl, 1
  int 0x13
  jc error_read

  mov ah, 0x00
  mov al, 0x03
  int 0x10

  mov ah, 0x02
  mov bh, 0
  mov dh, 0
  mov dl, 0
  int 0x10

  mov si, loading_str
  call puts
  
  mov ah, 0x02
  mov bh, 0
  mov dh, 1
  mov dl, 0
  int 0x10
  xor cl, cl
menu_loop:

  puts_cycle:
    cmp cl, 0x06
    je end_puts_cycle
    movzx si, cl
    shl si, 4
    add si, colors
    mov ah, 0x02
    mov dh, cl
    add dh, MENU_TAB
    int 0x10
    call puts
    mov si, marker
    cmp cl, [selected]
    je marker_set
    mov si, no_marker
    marker_set: 
    call puts
    add cl, 1
    jmp puts_cycle

    end_puts_cycle: 
  
  mov ah, 0x00
  int 0x16

  cmp ah, 0x48
  je key_up
  cmp ah, 0x50
  je key_down
  cmp ah, 0x0D
  je start_kernel
  jmp menu_loop

  key_up:
    cmp byte [selected], 0
    je menu_loop
    dec byte [selected]
    jmp menu_loop

  key_down:
    cmp byte [selected], 0
    je menu_loop
    inc byte [selected]
    jmp menu_loop

start_kernel:
  mov al, [selected]
  mov [0x8000], al
  cli
  lgdt [gdt_info]
  in al, 0x92
  or al, 2
  out 0x92, al

  mov eax, cr0
  or al, 1
  mov cr0, eax
  jmp dword 0x8:protected_mode 

loading_str: db "Loading...", 0
colors:    
  db "1.Gray", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  db "2.Cyan", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  db "3.White", 0, 0, 0, 0, 0, 0, 0, 0, 0
  db "4.Green", 0, 0, 0, 0, 0, 0, 0, 0, 0
  db "5.Yellow", 0, 0, 0, 0, 0, 0, 0, 0
  db "6.Magenta", 0, 0, 0, 0, 0, 0, 0

marker: db "<", 0
no_marker: db "  ", 0
selected: db 0

puts:
  mov al, [si]
  test al, al
  jz end_puts
  mov ah, 0x0e
  int 0x10
  add si, 1
  jmp puts

  end_puts: ret

error_read:
  mov ah, 0x0e
  mov al, 'E'
  int 0x10
  jmp $

gdt:
  db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db 0xff, 0xff, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00
  db 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00

gdt_info:
  dw gdt_info - gdt - 1
  dw gdt, 0

[BITS 32]
protected_mode:
  mov ax, 0x10
  mov es, ax
  mov ds, ax
  mov ss, ax
  mov esp, 0x90000
  call 0x10000

inf_loop:
  jmp inf_loop

  times (512 - ($ - start) - 2) db 0
  db 0x55, 0xAA

