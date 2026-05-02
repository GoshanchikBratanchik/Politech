.def TMP = R20

.org $000
   JMP reset ; Указатель на начало программы

; Функция паузы
delay: ; задержка 120 мс
   LDI  R31, 5
   LDI  R30, 223
   LDI  R29, 188
delay_sub:
   DEC  R29
   BRNE delay_sub
   DEC  R30
   BRNE delay_sub
   DEC  R31
   BRNE delay_sub
   NOP
   NOP
   RET

; Начальная настройка
reset:
; настройка исходных значений
   LDI  TMP, 0x01;
   MOV  R0, TMP
   CLR  TMP;
   MOV  R1, TMP
   MOV  R2, TMP
   MOV  R3, TMP
; настройка портов ввода-вывода
   SER  TMP ; 0xFF
   OUT  DDRA, TMP ; Вывод
   OUT  DDRB, TMP ; Вывод
   OUT  DDRC, TMP ; Вывод
   OUT  DDRD, TMP ; Вывод
; Установка вершины стека в конец ОЗУ
   LDI  TMP, HIGH(RAMEND) ; Старшие разряды адреса
   OUT  SPH, TMP 
   LDI  TMP, LOW(RAMEND) ; Младшие разряды адреса
   OUT  SPL, TMP

; Основной цикл
loop:
; Циклический сдвиг 32-разрядного числа R0-R3 
   BST R0, 0 ; сохранение младшего бита во флаге Т
   LSR R3 ; логический сдвиг вправо 
   ROR R2 ; циклический сдвиг вправо
   ROR R1 ; циклический сдвиг вправо
   ROR R0 ; циклический сдвиг вправо
   BLD R3, 7 ; заполнение 7 бита значением из флага T
; Вывод 32-разрядного числа R0-R3 на порты PORTA-PORTD
   OUT PORTA, R0
   OUT PORTB, R1
   OUT PORTC, R2
   OUT PORTD, R3
; Пауза
   CALL delay;
; Возврат в начало основного цикла
   RJMP loop ;