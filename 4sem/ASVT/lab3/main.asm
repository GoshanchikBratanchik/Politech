; ============================================================
; Тест: кнопка PD2 (INT0) переключает режим
;   0 = отображение (секунды идут)
;   1 = настройка   (секунды остановлены)
; Младший разряд (DIS0) показывает секунды 0-9 по кругу,
; остальные 3 разряда держат "0".
; ============================================================
; ВАЖНО (аппаратно, на плате):
;   - включите SW8.1-SW8.4 (PA0-PA3 -> выбор разряда)
;   - убедитесь, что кнопка PD2 подключена к линии INT0
; ============================================================

.include "m32def.inc"

.dseg
digit_buffer: .byte 4      ; сегментные коды для разрядов 0..3

.cseg
.org 0x0000
    rjmp RESET
.org 0x0002                 ; вектор INT0
    rjmp INT0_ISR
.org 0x0016                 ; вектор Timer0 Overflow
    rjmp TIMER0_OVF_ISR

.org 0x0030
SEG_TABLE:
    .db 0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F   ; коды цифр 0-9

RESET:
    ldi  r16, LOW(RAMEND)
    out  SPL, r16
    ldi  r16, HIGH(RAMEND)
    out  SPH, r16

    ; PORTC - сегменты на выход
    ldi  r16, 0xFF
    out  DDRC, r16
    ; PORTA биты 0-3 - выбор разряда на выход
    ldi  r16, 0x0F
    out  DDRA, r16

    ; заполняем буфер разрядов кодом "0"
    ldi  YL, LOW(digit_buffer)
    ldi  YH, HIGH(digit_buffer)
    ldi  r16, 0x3F
    st   Y+, r16
    st   Y+, r16
    st   Y+, r16
    st   Y,  r16

    ; переменные состояния
    ldi  r17, 0x01     ; маска активного разряда
    clr  r18            ; индекс активного разряда (0..3)
    clr  r19            ; флаг режима: 0=отображение, 1=настройка
    clr  r20            ; счётчик тиков таймера (младший байт)
    clr  r21            ; счётчик тиков таймера (старший байт)
    clr  r23            ; текущее значение секунд (0..9)

    out  PORTA, r17

    ; ---- настройка INT0 (кнопка PD2), нарастающий фронт 0->1 ----
    ; (на плате уровень покоя = 0 уже обеспечен аппаратно, подтяжка не нужна)
    ldi  r16, (1<<ISC01)|(1<<ISC00)
    out  MCUCR, r16
    ldi  r16, (1<<INT0)
    out  GICR, r16

    ; ---- настройка Timer0: Normal, предделитель 64 ----
    ldi  r16, (1<<CS01)|(1<<CS00)
    out  TCCR0, r16
    ldi  r16, (1<<TOIE0)
    out  TIMSK, r16

    sei

MAIN_LOOP:
    rjmp MAIN_LOOP

; ================================================================
; INT0 - нажатие кнопки PD2: переключаем режим
; ================================================================
INT0_ISR:
    push r16
    in   r16, SREG
    push r16

    ldi  r16, 0x01
    eor  r19, r16          ; инвертируем бит режима

    pop  r16
    out  SREG, r16
    pop  r16
    reti

; ================================================================
; Timer0 Overflow - каждые ~2.048 мс:
;   часть 1 - мультиплексирование разрядов
;   часть 2 - раз в секунду (если режим=0) увеличиваем секунды
; ================================================================
TIMER0_OVF_ISR:
    push r16
    in   r16, SREG
    push r16
    push YL
    push YH
    push ZL
    push ZH

    ; ---------- часть 1: переключение активного разряда ----------
    clr  r16
    out  PORTA, r16          ; гасим все разряды на момент смены данных

    inc  r18
    cpi  r18, 4
    brne PART1_NOWRAP
    clr  r18
PART1_NOWRAP:

    ldi  YL, LOW(digit_buffer)
    ldi  YH, HIGH(digit_buffer)
    clr  r16
    add  YL, r18
    adc  YH, r16
    ld   r16, Y
    out  PORTC, r16

    cpi  r18, 0
    breq PART1_M0
    cpi  r18, 1
    breq PART1_M1
    cpi  r18, 2
    breq PART1_M2
    ldi  r17, 0x08
    rjmp PART1_SETDIGIT
PART1_M0:
    ldi  r17, 0x01
    rjmp PART1_SETDIGIT
PART1_M1:
    ldi  r17, 0x02
    rjmp PART1_SETDIGIT
PART1_M2:
    ldi  r17, 0x04
PART1_SETDIGIT:
    out  PORTA, r17

    ; ---------- часть 2: секундный интервал ----------
    inc  r20
    brne PART2_NOCARRY
    inc  r21
PART2_NOCARRY:
    ldi  r16, LOW(488)
    cp   r20, r16
    ldi  r16, HIGH(488)
    cpc  r21, r16
    brlo PART2_DONE

    clr  r20
    clr  r21

    sbrc r19, 0              ; если бит0=1 (настройка/пауза) -> пропускаем инкремент
    rjmp PART2_DONE

    inc  r23
    cpi  r23, 10
    brne PART2_UPDATE
    clr  r23
PART2_UPDATE:
    ldi  ZL, LOW(SEG_TABLE*2)
    ldi  ZH, HIGH(SEG_TABLE*2)
    clr  r16
    add  ZL, r23
    adc  ZH, r16
    lpm  r16, Z

    ldi  YL, LOW(digit_buffer)
    ldi  YH, HIGH(digit_buffer)
    st   Y, r16              ; digit_buffer[0] = новый код секунд

PART2_DONE:
    pop  ZH
    pop  ZL
    pop  YH
    pop  YL
    pop  r16
    out  SREG, r16
    pop  r16
    reti
