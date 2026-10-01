[BITS 16]
[ORG 0x7C00]

start:
  mov ax, cs
  mov ds, ax
  mov ss, ax
  mov sp, start
  mov ah, 0x0e
  
  mov al, 'b'
  int 0x10

  mov al, 'o'
  int 0x10
  int 0x10

  mov al, 't'
  int 0x10

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

  cli
  lgdt [gdt_info]
  in al, 0x92
  or al, 2
  out 0x92, al

  mov eax, cr0
  or al, 1
  mov cr0, eax
  jmp dword 0x8:protected_mode 

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

