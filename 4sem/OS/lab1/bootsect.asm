MENU_TAB equ 2          ; строка, с которой начинается меню цветов
[BITS 16]
[ORG 0x7C00]

start:
  ; Инициализация сегментов и стека
  mov ax, cs
  mov ds, ax
  mov ss, ax
  mov sp, start

  ; Сброс второго дисковода
  mov dl, 1
  mov ah, 0
  int 0x13

  ; Чтение ядра со второго диска по адресу 0x1000:0x0000 = 0x10000
  mov ax, 0x1000
  mov es, ax
  mov bx, 0
  mov ah, 0x02          ; функция: чтение секторов
  mov al, 18            ; 18 секторов (целая дорожка, до 9 КБ ядра)
  mov ch, 0             ; цилиндр
  mov cl, 1             ; сектор (нумерация с 1)
  mov dh, 0             ; головка
  mov dl, 1             ; диск B:
  int 0x13
  jc error_read

  ; Текстовый режим 80x25, заодно очищает экран
  mov ah, 0x00
  mov al, 0x03
  int 0x10

  ; Заголовок в строке 0
  mov ah, 0x02
  mov bh, 0
  mov dh, 0
  mov dl, 0
  int 0x10
  mov si, title_str
  call puts

menu_loop:
  xor cl, cl            ; номер рисуемого пункта

puts_cycle:
  cmp cl, 6
  je end_puts_cycle

  ; Адрес названия: colors + номер * 16
  movzx si, cl
  shl si, 4
  add si, colors

  ; Курсор: строка MENU_TAB + номер, столбец 0
  mov ah, 0x02
  mov bh, 0
  mov dh, cl
  add dh, MENU_TAB
  mov dl, 0
  int 0x10
  call puts

  ; Маркер у выбранного пункта, пробелы у остальных
  mov si, marker
  cmp cl, [selected]
  je marker_set
  mov si, no_marker
marker_set:
  call puts

  add cl, 1
  jmp puts_cycle

end_puts_cycle:
  ; Ожидание клавиши: ah = скан-код, al = ASCII
  mov ah, 0x00
  int 0x16

  cmp ah, 0x48          ; стрелка вверх
  je key_up
  cmp ah, 0x50          ; стрелка вниз
  je key_down
  cmp al, 0x0D          ; Enter
  je start_kernel
  jmp menu_loop

key_up:
  cmp byte [selected], 0
  je menu_loop
  dec byte [selected]
  jmp menu_loop

key_down:
  cmp byte [selected], 5
  je menu_loop
  inc byte [selected]
  jmp menu_loop

start_kernel:
  ; Передача выбранного цвета ядру через адрес 0x8000
  mov al, [selected]
  mov [0x8000], al

  ; Переход в защищённый режим
  cli
  lgdt [gdt_info]
  in al, 0x92           ; включение адресной линии A20
  or al, 2
  out 0x92, al

  mov eax, cr0
  or al, 1              ; бит PE
  mov cr0, eax
  jmp dword 0x8:protected_mode

; Вывод строки, оканчивающейся нулём; адрес строки в si
puts:
  mov al, [si]
  test al, al
  jz end_puts
  mov ah, 0x0e
  int 0x10
  add si, 1
  jmp puts
end_puts:
  ret

error_read:
  mov ah, 0x0e
  mov al, 'E'
  int 0x10
  jmp $

title_str: db "Choose color:", 0
colors:
  db "1.Gray", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  db "2.Cyan", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  db "3.White", 0, 0, 0, 0, 0, 0, 0, 0, 0
  db "4.Green", 0, 0, 0, 0, 0, 0, 0, 0, 0
  db "5.Yellow", 0, 0, 0, 0, 0, 0, 0, 0
  db "6.Magenta", 0, 0, 0, 0, 0, 0, 0

marker:    db " <", 0
no_marker: db "  ", 0
selected:  db 0

gdt:
  db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   ; нулевой дескриптор
  db 0xff, 0xff, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00   ; код: база 0, 4 ГБ
  db 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00   ; данные: база 0, 4 ГБ

gdt_info:
  dw gdt_info - gdt - 1
  dw gdt, 0

[BITS 32]
protected_mode:
  mov ax, 0x10          ; селектор сегмента данных
  mov es, ax
  mov ds, ax
  mov ss, ax
  mov esp, 0x90000
  call 0x10000          ; передача управления ядру

inf_loop:
  jmp inf_loop

  times 510 - ($ - $$) db 0
  db 0x55, 0xAA
