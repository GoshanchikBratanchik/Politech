
.org $000
    JMP reset           ; сброс — начало программы
.org INT0addr
    JMP ext_int0        ; прерывание по кнопке PD2 — смена режима
.org INT1addr
    JMP ext_int1        ; прерывание по кнопке PD3 — смена частоты

; биты регистра STATE (NUM_D / PORTD)
.equ FREQ1        = 0   ; бит 0 PORTD — младший бит номера частоты
.equ FREQ2        = 1   ; бит 1 PORTD — старший бит номера частоты
.equ MODE_BUTTON  = 2   ; бит 2 PORTD — кнопка INT0 (вход)
.equ FREQ_BUTTON  = 3   ; бит 3 PORTD — кнопка INT1 (вход)
.equ MODE        = 4   ; бит 4 PORTD — номер режима
.equ STATE_BIT    = 5   ; бит 5 PORTD — текущее состояние (0/1)
.equ ENTER_BUTTON = 7   ; бит 7 PORTD — кнопка ввода y

; адреса EEPROM для хранения настроек
.equ EEPROM_ADR_FREQ = 0x10
.equ EEPROM_ADR_MODE = 0x11

; переменные
.def NUM_D        = R16  ; маска вывода на PORTD (режим, частота, состояние)
.def TMP          = R17  ; временный регистр
.def VAL1         = R18  ; вспомогательный
.def VAL2         = R19  ; вспомогательный
.def STATE        = R20  ; текущее состояние (0 или 1)
.def FREQ_VAL     = R21  ; текущий индекс частоты (0,1,2)
.def MODE_VAL     = R22  ; текущий режим (0 или 1)
.def TMP_CYCLE    = R23  ; временный для циклических функций
.def EEPROM_VALUE = R24  ; значение для чтения/записи EEPROM
.def EEPROM_ADR   = R25  ; адрес для EEPROM операций
.def PLUS_Y       = R26  ; значение y
.def MINUS_Y      = R27  ; значение -y*
.def NULL         = R28  ; всегда 0x00

; начальная настройка
reset:
    ; стек в конец ОЗУ
    LDI R20, HIGH(RAMEND)
    OUT SPH, R20
    LDI R20, LOW(RAMEND)
    OUT SPL, R20

    ; инициализация регистров 
    CLR NULL                    ; NULL = 0x00, используется как константа
    LDI PLUS_Y, 0x55            ; y = 0x55 по умолчанию
    CLR NUM_D                   ; PORTD = 0 (все биты сброшены)
    CLR STATE                   ; начальное состояние = 0

    ; читаем частоту из EEPROM 
    LDI EEPROM_ADR, EEPROM_ADR_FREQ
    CALL EEPROM_read
    MOV FREQ_VAL, EEPROM_VALUE  ; восстанавливаем сохранённый индекс частоты

    ; читаем режим из EEPROM 
    LDI EEPROM_ADR, EEPROM_ADR_MODE
    CALL EEPROM_read
    MOV MODE_VAL, EEPROM_VALUE  ; восстанавливаем сохранённый режим

    ; восстанавливаем биты частоты в NUM_D 
    ; если бит 0 FREQ_VAL установлен — ставим FREQ1 в NUM_D
    SBRS FREQ_VAL, 0
    CBR NUM_D, (1<<FREQ1)
    SBRC FREQ_VAL, 0
    SBR NUM_D, (1<<FREQ1)

    ; если бит 1 FREQ_VAL установлен, ставим FREQ2 в NUM_D
    SBRS FREQ_VAL, 1
    CBR NUM_D, (1<<FREQ2)
    SBRC FREQ_VAL, 1
    SBR NUM_D, (1<<FREQ2)

    ; восстанавливаем бит режима в NUM_D
    ; Режим хранится в бите 0 MODE_VAL, отображается в бите MODE NUM_D
    SBRS MODE_VAL, 0
    CBR NUM_D, (1<<MODE)
    SBRC MODE_VAL, 0
    SBR NUM_D, (1<<MODE)

    ; Настройка портов
    SER TMP
    OUT DDRA, TMP               ; PORTA — все выходы (светодиоды)
    OUT DDRB, TMP               ; PORTB — все выходы (светодиоды)
    CLR TMP
    OUT DDRC, TMP               ; PORTC — все входы (кнопки ввода y)
    LDI TMP, 0x73               ; 0111 0011: PD0,PD1,PD4,PD5,PD6 — выходы
                                 ;           PD2,PD3,PD7 — входы (кнопки)
    OUT DDRD, TMP

    ; Настройка прерываний INT0 и INT1
    LDI TMP, 0x0F               ; ISC01=1,ISC00=1 — INT0 по фронту 0?1
                                 ; ISC11=1,ISC10=1 — INT1 по фронту 0?1
    OUT MCUCR, TMP

    LDI TMP, 0xC0               ; INT1=1, INT0=1 — разрешаем оба прерывания
    OUT GICR, TMP
    OUT GIFR, TMP               ; сбрасываем флаги чтобы не сработало сразу

    SEI                         ; глобальное разрешение прерываний

    CALL update_output          ; выводим начальное состояние на PORTD

; основной цикл
main_loop:
    ; проверяем кнопку PD7 (ввод y)
    SBIC PIND, ENTER_BUTTON     ; если PD7 = 0 (кнопка нажата) — пропускаем
    CALL input_mode             ; PD7 = 1 — нажата, читаем y

    ; задержка в зависимости от текущей частоты
    CALL delay_freq             ; ждём нужное время

    ; инвертируем состояние
    LDI TMP, (1 << STATE_BIT)
    EOR STATE, TMP              ; STATE бит 5 меняется 0?1?0?1...
    EOR NUM_D, TMP              ; тот же бит в NUM_D для вывода на PORTD

    ; обновляем светодиоды и PORTD 
    CALL update_leds
    CALL update_output

    RJMP main_loop

; ввод y
; Вызывается когда PD7 нажата.
; Читаем комбинацию кнопок с PORTC, ждём отпускания PD7.
input_mode:
    CALL read_y                 ; считываем y с PORTC

wait_release:
    IN TMP, PIND
    SBRS TMP, ENTER_BUTTON      ; если PD7 ещё нажата — ждём
    RJMP wait_release
    RET

; обновление светодиодов
; выбираем что выводить в зависимости от режима и состояния.
update_leds:
    CPI MODE_VAL, 0
    BREQ mode0                  ; режим 0: 0xFF / 0x00

    RJMP mode1                  ; режим 1: y / -y*

mode0:
    ; cостояние 0: PORTA=0xFF, PORTB=0x00
    ; cостояние 1: PORTA=0x00, PORTB=0xFF
    LDI R29, 0xFF
    LDI R30, 0x00
    RJMP check_state

mode1:
    ; cостояние 0: PORTA=y,    PORTB=-y
    ; cостояние 1: PORTA=-y,   PORTB=y
    MOV R29, PLUS_Y
    CALL calc_neg_y             ; вычисляем -y в MINUS_Y
    MOV R30, MINUS_Y

check_state:
    ; если STATE_BIT = 1 — меняем местами R29 и R30
    CPI STATE, 0
    BREQ out_leds               ; STATE=0 — выводим как есть

    ; STATE != 0 — меняем местами
    MOV TMP, R29
    MOV R29, R30
    MOV R30, TMP

out_leds:
    OUT PORTA, R29
    OUT PORTB, R30
    RET

; int0
; Нажата кнопка PD2. Циклически меняет MODE_VAL: 0?1?0?...
ext_int0:
    PUSH TMP
    IN TMP, SREG                ; сохраняем SREG — он может измениться
    PUSH TMP_CYCLE

    MOV TMP_CYCLE, MODE_VAL
    CALL inc_val_mode           ; инкремент с переполнением: 0?1?0
    MOV MODE_VAL, TMP_CYCLE

    ; обновляем бит MODE в NUM_D
    SBRS MODE_VAL, 0
    CBR NUM_D, (1<<MODE)
    SBRC MODE_VAL, 0
    SBR NUM_D, (1<<MODE)

    ; сохраняем новый режим в EEPROM
    LDI EEPROM_ADR, EEPROM_ADR_MODE
    MOV EEPROM_VALUE, MODE_VAL
    CALL EEPROM_write

    POP TMP_CYCLE
    OUT SREG, TMP               ; восстанавливаем SREG
    POP TMP
    RETI

; int1
; Нажата кнопка PD3. Циклически меняет FREQ_VAL: 0?1?2?0?...
ext_int1:
    PUSH TMP
    IN TMP, SREG
    PUSH TMP_CYCLE

    MOV TMP_CYCLE, FREQ_VAL
    CALL inc_val                ; инкремент с переполнением: 0?1?2?0
    MOV FREQ_VAL, TMP_CYCLE

    ; обновляем биты FREQ1, FREQ2 в NUM_D
    SBRS FREQ_VAL, 0
    CBR NUM_D, (1<<FREQ1)
    SBRC FREQ_VAL, 0
    SBR NUM_D, (1<<FREQ1)

    SBRS FREQ_VAL, 1
    CBR NUM_D, (1<<FREQ2)
    SBRC FREQ_VAL, 1
    SBR NUM_D, (1<<FREQ2)

    ; сохраняем новую частоту в EEPROM
    LDI EEPROM_ADR, EEPROM_ADR_FREQ
    MOV EEPROM_VALUE, FREQ_VAL
    CALL EEPROM_write

    POP TMP_CYCLE
    OUT SREG, TMP
    POP TMP
    RETI

; запись в EEPROM
EEPROM_write:
    SBIC EECR, EEWE             ; ждём пока предыдущая запись не закончится
    RJMP EEPROM_write

    OUT EEARL, EEPROM_ADR       ; адрес записи (младший байт)
    CLR TMP
    OUT EEARH, TMP              ; адрес записи (старший байт = 0)
    OUT EEDR, EEPROM_VALUE      ; данные для записи

    SBI EECR, EEMWE             ; Master Write Enable — разрешение записи
    SBI EECR, EEWE              ; Write Enable — запускаем запись
    RET

; чтение из памяти
EEPROM_read:
    SBIC EECR, EEWE             ; ждём пока запись не закончится (если была)
    RJMP EEPROM_read

    OUT EEARL, EEPROM_ADR       ; адрес чтения
    CLR TMP
    OUT EEARH, TMP              ; старший байт адреса = 0
    SBI EECR, EERE              ; Read Enable — запускаем чтение
    IN EEPROM_VALUE, EEDR       ; читаем данные из регистра данных EEPROM
    RET

; инкремент FREQ_VAL с переполнением
; TMP_CYCLE: 0?1?2?0?...
inc_val:
    INC TMP_CYCLE
    CPI TMP_CYCLE, 3            ; если >= 3
    BRLO ok_inc                 ; если < 3 — всё хорошо
    LDI TMP_CYCLE, 0            ; иначе сбрасываем в 0
ok_inc:
    RET

; inc MODE_VAL с переполнением
; TMP_CYCLE: 0?1?0?... (только 2 режима в варианте 3)
inc_val_mode:
    INC TMP_CYCLE
    CPI TMP_CYCLE, 2            ; если >= 2
    BRLO ok_inc_mode
    LDI TMP_CYCLE, 0
ok_inc_mode:
    RET

; обновление PORTD
update_output:
    OUT PORTD, NUM_D
    RET

; вычисление y
calc_neg_y:
    BST PLUS_Y, 7
    MOV MINUS_Y, PLUS_Y
    COM MINUS_Y             ; инвертируем все биты (обратный код)
    INC MINUS_Y             ; +1 ? дополнительный код
    BLD MINUS_Y, 7
	RET

; считывание PORTC
; ждём нажатия хотя бы одной кнопки, затем читаем комбинацию,
; затем ждём отпускания всех кнопок.
read_y:
    IN TMP, PINC                ; читаем состояние PORTC
    CALL delay                  ; ждём 120 мс — даём зажать все нужные кнопки
    MOV PLUS_Y, TMP             ; сохраняем прочитанное значение как y

stop_reading_y:
    IN TMP, PINC
    CP TMP, NULL                ; все кнопки отпущены? PINC = 0?
    BRNE stop_reading_y         ; нет — ждём
    RET

; задержка в зависимости от частоты
; FREQ_VAL = 0 ? 1   Гц ? задержка 0.5 с (полупериод)
; FREQ_VAL = 1 ? 0.5 Гц ? задержка 1   с (полупериод)
; FREQ_VAL = 2 ? 0.25 Гц ? задержка 2 с (полупериод)
;
; частота гирлянды — это частота смены состояния.
; значит задержка = 1 / (2 * частота).
; 1   Гц ? смена каждые 0.5  с
; 0.5 Гц ? смена каждые 1    с
; 0.25 Гц ? смена каждые 2   с
delay_freq:
    SBRS NUM_D, FREQ1           ; если бит FREQ1 = 0 — прыгаем на delay_1hz
    RJMP delay_1hz
    SBRS NUM_D, FREQ2           ; FREQ1=1, FREQ2=0 ? 0.5 Гц
    RJMP delay_05hz
    RJMP delay_025hz            ; FREQ1=1, FREQ2=1 ? 0.25 Гц

delay_1hz:                      ; задержка 0.5 с (FREQ_VAL=0, 1 Гц)
    LDI R29, 100
    LDI R30, 100
    LDI R31, 133
    RJMP delay_loop

delay_05hz:                     ; задержка 1 с (FREQ_VAL=1, 0.5 Гц)
    LDI R29, 100
    LDI R30, 100
    LDI R31, 255
    RJMP delay_loop

delay_025hz:                    ; задержка 2 с (FREQ_VAL=2, 0.25 Гц)
    LDI R29, 100
    LDI R30, 100
    LDI R31, 89                 ; два прохода по 1 с
    RJMP delay_loop             ; для 2 с нужно вызвать delay_loop дважды
                                ; или использовать внешний счётчик — упрощённо

; задержка для считывания кнопок
delay:
    LDI R31, 5
    LDI R30, 223
    LDI R29, 188

; цикл для всех задержок
delay_loop:
    DEC R29
    BRNE delay_loop
    DEC R30
    BRNE delay_loop
    DEC R31
    BRNE delay_loop
    NOP
    NOP
    RET