; Лабораторная работа 3, Вариант 3 — Модуль часов
; Антонов Г.Е.
;
; Отображение: PORTC — сегменты, PA0-PA3 — выбор разряда
; Кнопки: PD0 (+), PD1 (-), PD2 (INT0), PD3 (INT1)
;
; Режимы:
;   mode=0: отображение (часы идут)
;   mode=1: настройка (часы стоят, настраиваемый элемент мигает 2 Гц)
;
; Форматы отображения (disp_fmt):
;   0 = ЧЧ.ММ
;   1 = ММ.СС
;
; Настраиваемый элемент (edit_sel):
;   0 = ЧЧ, 1 = ММ, 2 = СС
;
; Таймер T0: делитель /64, preload=131 ? ~1 мс на тик при 8 МГц
; 255 тиков ? 1 секунда (255 * 1/977 Гц ? 0.996 с ? достаточно точно)
; Точнее: 8000000/64 = 125000 Гц, (256-131)=125 ? 125000/125 = 1000 Гц (1 мс/тик)
; 1000 тиков = 1 секунда
; 500 тиков  = 0.5 с (мигание 2 Гц)
; 200 тиков  = 0.2 с (быстрое изменение после 2с)
; 100 тиков  = 0.1 с (очень быстрое после 4с)
; 2000 тиков = 2 с (порог ускорения 1)
; 4000 тиков = 4 с (порог ускорения 2)
;
; Тики хранятся как 16-бит в двух байтах (H:L)

.include "m32def.inc"

.def temp    = r16
.def temp1   = r17
.def temp2   = r18
.def temp3   = r19

.equ T0_PRELOAD = 131       ; (256-131)*64/8000000 = 0.001 с = 1 мс

; Пороги в тиках (16-бит, хранятся как старший:младший)
.equ TICKS_1SEC_H   = high(1000)
.equ TICKS_1SEC_L   = low(1000)
.equ TICKS_BLINK_H  = high(500)
.equ TICKS_BLINK_L  = low(500)
.equ TICKS_HOLD2_H  = high(2000)   ; 2 секунды удержания
.equ TICKS_HOLD2_L  = low(2000)
.equ TICKS_HOLD4_H  = high(4000)   ; 4 секунды удержания
.equ TICKS_HOLD4_L  = low(4000)
.equ TICKS_FAST_H   = high(200)    ; каждые 0.2 с
.equ TICKS_FAST_L   = low(200)
.equ TICKS_VFAST_H  = high(100)    ; каждые 0.1 с
.equ TICKS_VFAST_L  = low(100)

; ========= ПЕРЕМЕННЫЕ В SRAM =========
.dseg
tick_cnt_l:  .byte 1    ; счётчик тиков таймера (мл. байт)
tick_cnt_h:  .byte 1    ; счётчик тиков таймера (ст. байт)

sec_tick_l:  .byte 1    ; счётчик тиков для секунды
sec_tick_h:  .byte 1

blink_tick_l: .byte 1   ; счётчик тиков для мигания
blink_tick_h: .byte 1

hold_tick_l: .byte 1    ; сколько тиков зажата кнопка PD0/PD1
hold_tick_h: .byte 1

rep_tick_l:  .byte 1    ; счётчик тиков для повторного срабатывания
rep_tick_h:  .byte 1

mode:        .byte 1    ; 0=отображение, 1=настройка
disp_fmt:    .byte 1    ; 0=ЧЧ.ММ, 1=ММ.СС
edit_sel:    .byte 1    ; 0=ЧЧ, 1=ММ, 2=СС
blink_state: .byte 1    ; 0=элемент виден, 1=элемент скрыт

hh:          .byte 1    ; часы   (0-23)
mm:          .byte 1    ; минуты (0-59)
ss:          .byte 1    ; секунды (0-59)

btn0_prev:   .byte 1    ; предыдущее состояние PD0
btn1_prev:   .byte 1    ; предыдущее состояние PD1
btn0_held:   .byte 1    ; 1 = кнопка PD0 сейчас зажата
btn1_held:   .byte 1    ; 1 = кнопка PD1 сейчас зажата

; ========= КОД =========
.cseg
.org 0x0000
    rjmp RESET

.org INT0addr           ; 0x0002 — вектор INT0
    rjmp INT0_ISR

.org INT1addr           ; 0x0004 — вектор INT1
    rjmp INT1_ISR

.org 0x0016             ; вектор переполнения Timer0
    rjmp TIMER0_ISR

.org 0x0020

; ========= СБРОС / ИНИЦИАЛИЗАЦИЯ =========
RESET:
    ; стек
    ldi temp, high(RAMEND)
    out SPH, temp
    ldi temp, low(RAMEND)
    out SPL, temp

    ; PORTC — сегменты (все выходы)
    ldi temp, 0xFF
    out DDRC, temp

    ; PA0-PA3 — выбор разряда (выходы), PA4-PA7 — входы
    in temp, DDRA
    ori temp, 0x0F
    out DDRA, temp

    ; PORTD — все входы (кнопки)
    clr temp
    out DDRD, temp

    ; инициализация переменных
    clr temp
    sts tick_cnt_l,  temp
    sts tick_cnt_h,  temp
    sts sec_tick_l,  temp
    sts sec_tick_h,  temp
    sts blink_tick_l, temp
    sts blink_tick_h, temp
    sts hold_tick_l, temp
    sts hold_tick_h, temp
    sts rep_tick_l,  temp
    sts rep_tick_h,  temp

    sts mode,        temp   ; режим отображения
    sts disp_fmt,    temp   ; формат ЧЧ.ММ
    sts edit_sel,    temp   ; настраиваем ЧЧ
    sts blink_state, temp

    sts hh, temp
    sts mm, temp
    sts ss, temp

    ldi temp, 1
    sts btn0_prev, temp     ; кнопки не нажаты (1)
    sts btn1_prev, temp
    clr temp
    sts btn0_held, temp
    sts btn1_held, temp

    ; настройка Timer0: /64, preload, прерывание по переполнению
    ldi temp, T0_PRELOAD
    out TCNT0, temp
    ldi temp, (1<<CS01)|(1<<CS00)   ; делитель /64
    out TCCR0, temp
    ldi temp, (1<<TOIE0)            ; разрешаем прерывание по переполнению
    out TIMSK, temp

    ; настройка INT0, INT1 по фронту 0?1
    ldi temp, (1<<ISC11)|(1<<ISC10)|(1<<ISC01)|(1<<ISC00)
    out MCUCR, temp
    ldi temp, (1<<INT1)|(1<<INT0)
    out GICR, temp
    out GIFR, temp          ; сбрасываем флаги

    sei

; ========= ГЛАВНЫЙ ЦИКЛ =========
MAIN:
    rcall BUTTONS_PD01      ; обработка PD0/PD1 (инкремент/декремент)
    rcall SHOW              ; вывод на индикаторы
    rjmp MAIN

; ========= ПРЕРЫВАНИЕ TIMER0 (каждые 1 мс) =========
TIMER0_ISR:
    push temp
    push temp1
    in temp, SREG
    push temp

    ldi temp, T0_PRELOAD
    out TCNT0, temp         ; перезагружаем таймер

    ; --- инкремент общего счётчика тиков ---
    lds temp,  tick_cnt_l
    lds temp1, tick_cnt_h
    inc temp
    brne TC_NO_CARRY
    inc temp1
TC_NO_CARRY:
    sts tick_cnt_l, temp
    sts tick_cnt_h, temp1

    ; --- счётчик для секунды ---
    lds temp,  sec_tick_l
    lds temp1, sec_tick_h
    inc temp
    brne ST_NO_CARRY
    inc temp1
ST_NO_CARRY:
    sts sec_tick_l, temp
    sts sec_tick_h, temp1

    ; проверяем: sec_tick >= 1000?
    cpi temp1, TICKS_1SEC_H
    brlo SEC_TICK_END
    cpi temp,  TICKS_1SEC_L
    brlo SEC_TICK_END

    ; сбрасываем sec_tick
    clr temp
    sts sec_tick_l, temp
    sts sec_tick_h, temp

    ; добавляем секунду только в режиме отображения
    lds temp, mode
    tst temp
    brne SEC_TICK_END       ; в режиме настройки — не считаем
    rcall INC_TIME

SEC_TICK_END:

    ; --- счётчик для мигания ---
    lds temp,  blink_tick_l
    lds temp1, blink_tick_h
    inc temp
    brne BT_NO_CARRY
    inc temp1
BT_NO_CARRY:
    sts blink_tick_l, temp
    sts blink_tick_h, temp1

    cpi temp1, TICKS_BLINK_H
    brlo BLINK_END
    cpi temp, TICKS_BLINK_L
    brlo BLINK_END

    clr temp
    sts blink_tick_l, temp
    sts blink_tick_h, temp

    lds temp, mode
    tst temp
    breq BLINK_RESET

    lds temp, blink_state
    ldi temp1, 1
    eor temp, temp1
    sts blink_state, temp
    rjmp BLINK_END

BLINK_RESET:
    clr temp
    sts blink_state, temp
BLINK_END:

    ; --- счётчик удержания кнопки ---
    ; увеличиваем только если кнопка зажата
    lds temp, btn0_held
    tst temp
    brne INC_HOLD
    lds temp, btn1_held
    tst temp
    brne INC_HOLD
    ; ни одна не зажата — сбрасываем
    clr temp
    sts hold_tick_l, temp
    sts hold_tick_h, temp
    sts rep_tick_l,  temp
    sts rep_tick_h,  temp
    rjmp ISR_END

INC_HOLD:
    lds temp,  hold_tick_l
    lds temp1, hold_tick_h
    ; ограничиваем на 4001 чтобы не переполнилось
    cpi temp1, high(4001)
    brsh HOLD_CAP
    cpi temp,  low(4001)
    brsh HOLD_CAP
    inc temp
    brne HT_NO_CARRY
    inc temp1
HT_NO_CARRY:
    sts hold_tick_l, temp
    sts hold_tick_h, temp1

    ; --- счётчик повторного срабатывания ---
    lds temp,  rep_tick_l
    lds temp1, rep_tick_h
    inc temp
    brne RT_NO_CARRY
    inc temp1
RT_NO_CARRY:
    sts rep_tick_l, temp
    sts rep_tick_h, temp1
    rjmp ISR_END

HOLD_CAP:
    ; уже на максимуме — ничего не делаем
ISR_END:
    pop temp
    out SREG, temp
    pop temp1
    pop temp
    reti

; ========= ОБРАБОТЧИК INT0 — ПЕРЕКЛЮЧЕНИЕ НАСТРОЙКА/ОТОБРАЖЕНИЕ =========
INT0_ISR:
    push temp
    in temp, SREG
    push temp

    lds temp, mode
    ldi r18, 1
    eor temp, r18           ; инвертируем mode (0?1?0)
    sts mode, temp

    tst temp
    brne INT0_TO_EDIT

    ; переходим в режим ОТОБРАЖЕНИЯ
    clr temp
    sts blink_state, temp   ; гасим мигание
    sts blink_tick_l, temp
    sts blink_tick_h, temp
    rjmp INT0_END

INT0_TO_EDIT:
    ; переходим в режим НАСТРОЙКИ
    clr temp
    sts edit_sel, temp      ; начинаем с ЧЧ
    sts blink_state, temp
    sts blink_tick_l, temp
    sts blink_tick_h, temp

INT0_END:
    pop temp
    out SREG, temp
    pop temp
    reti

; ========= ОБРАБОТЧИК INT1 =========
; В режиме отображения: переключает формат ЧЧ.ММ ? ММ.СС
; В режиме настройки:   циклически переключает ЧЧ?ММ?СС?ЧЧ
INT1_ISR:
    push temp
    push temp1
    in temp, SREG
    push temp

    lds temp, mode
    tst temp
    brne INT1_EDIT_MODE

    ; --- режим отображения: меняем формат ---
    lds temp, disp_fmt
    ldi temp1, 1
    eor temp, temp1         ; 0?1?0
    sts disp_fmt, temp
    rjmp INT1_END

INT1_EDIT_MODE:
    ; --- режим настройки: следующий элемент ЧЧ?ММ?СС?ЧЧ ---
    lds temp, edit_sel
    inc temp
    cpi temp, 3
    brlo INT1_STORE_SEL
    clr temp
INT1_STORE_SEL:
    sts edit_sel, temp

    ; сбрасываем мигание
    clr temp
    sts blink_state, temp
    sts blink_tick_l, temp
    sts blink_tick_h, temp

INT1_END:
    pop temp
    out SREG, temp
    pop temp1
    pop temp
    reti

; ========= ОБРАБОТКА КНОПОК PD0 (+) И PD1 (-) =========
; Вызывается из главного цикла.
; Логика: нажатие ? +1 сразу; держим >2с ? каждые 0.2с; держим >4с ? каждые 0.1с
BUTTONS_PD01:
    push temp
    push temp1
    push temp2

    ; работают только в режиме настройки
    lds temp, mode
    tst temp
    brne BTN_ACTIVE
    rjmp BTN_END
BTN_ACTIVE:

    in temp, PIND

    ; ---- PD0 (увеличить) ----
    sbrs temp, 0            ; если PD0=1 (не нажата) ? перейти к отпусканию
    rjmp PD0_IS_PRESSED

PD0_RELEASED:
    ldi temp1, 1
    sts btn0_prev, temp1
    clr temp1
    sts btn0_held, temp1
    rjmp CHECK_PD1_BTN

PD0_IS_PRESSED:
    ; кнопка нажата (PD0=0)
    lds temp1, btn0_prev
    tst temp1
    breq PD0_HELD           ; уже была нажата — обрабатываем удержание

    ; первое нажатие
    clr temp1
    sts btn0_prev, temp1
    ldi temp1, 1
    sts btn0_held, temp1
    ; сбрасываем счётчики
    clr temp1
    sts hold_tick_l, temp1
    sts hold_tick_h, temp1
    sts rep_tick_l,  temp1
    sts rep_tick_h,  temp1
    rcall INC_EDIT          ; немедленно +1
    rjmp CHECK_PD1_BTN

PD0_HELD:
    ; кнопка удерживается
    ldi temp1, 1
    sts btn0_held, temp1
    rcall CHECK_REPEAT_INC
    rjmp CHECK_PD1_BTN

    ; ---- PD1 (уменьшить) ----
CHECK_PD1_BTN:
    in temp, PIND
    sbrs temp, 1
    rjmp PD1_IS_PRESSED

PD1_RELEASED:
    ldi temp1, 1
    sts btn1_prev, temp1
    clr temp1
    sts btn1_held, temp1
    rjmp BTN_END

PD1_IS_PRESSED:
    lds temp1, btn1_prev
    tst temp1
    breq PD1_HELD

    ; первое нажатие
    clr temp1
    sts btn1_prev, temp1
    ldi temp1, 1
    sts btn1_held, temp1
    clr temp1
    sts hold_tick_l, temp1
    sts hold_tick_h, temp1
    sts rep_tick_l,  temp1
    sts rep_tick_h,  temp1
    rcall DEC_EDIT          ; немедленно -1
    rjmp BTN_END

PD1_HELD:
    ldi temp1, 1
    sts btn1_held, temp1
    rcall CHECK_REPEAT_DEC

BTN_END:
    pop temp2
    pop temp1
    pop temp
    ret

; ========= ПРОВЕРКА ПОВТОРНОГО СРАБАТЫВАНИЯ (для +) =========
; Смотрим hold_tick и rep_tick, решаем — пора ли снова +1
CHECK_REPEAT_INC:
    push temp
    push temp1
    push temp2

    rcall GET_REPEAT_INTERVAL  ; в temp2:temp1 — нужный интервал (или 0 если рано)
    tst temp2
    breq CRI_NO_ACTION
    tst temp1
    breq CRI_NO_ACTION

    ; сравниваем rep_tick >= интервал
    lds temp,  rep_tick_h
    cp  temp,  temp2
    brlo CRI_NO_ACTION
    brne CRI_DO             ; H > нужного
    lds temp,  rep_tick_l
    cp  temp,  temp1
    brlo CRI_NO_ACTION

CRI_DO:
    ; сбрасываем rep_tick
    clr temp
    sts rep_tick_l, temp
    sts rep_tick_h, temp
    rcall INC_EDIT

CRI_NO_ACTION:
    pop temp2
    pop temp1
    pop temp
    ret

; ========= ПРОВЕРКА ПОВТОРНОГО СРАБАТЫВАНИЯ (для -) =========
CHECK_REPEAT_DEC:
    push temp
    push temp1
    push temp2

    rcall GET_REPEAT_INTERVAL
    tst temp2
    breq CRD_NO_ACTION
    tst temp1
    breq CRD_NO_ACTION

    lds temp,  rep_tick_h
    cp  temp,  temp2
    brlo CRD_NO_ACTION
    brne CRD_DO
    lds temp,  rep_tick_l
    cp  temp,  temp1
    brlo CRD_NO_ACTION

CRD_DO:
    clr temp
    sts rep_tick_l, temp
    sts rep_tick_h, temp
    rcall DEC_EDIT

CRD_NO_ACTION:
    pop temp2
    pop temp1
    pop temp
    ret

; ========= ПОЛУЧИТЬ ИНТЕРВАЛ ПОВТОРА =========
; Возвращает в temp2:temp1 нужный интервал в тиках
; Если удержание ещё меньше 2с — возвращает 0:0 (не повторять)
GET_REPEAT_INTERVAL:
    ; читаем hold_tick
    lds temp,  hold_tick_h
    lds temp1, hold_tick_l  ; temp:temp1 = hold_tick (H:L)

    ; hold >= 4000? ? интервал 100 мс
    cpi temp, TICKS_HOLD4_H
    brlo GRI_CHECK2
    cpi temp1, TICKS_HOLD4_L
    brlo GRI_CHECK2
    ldi temp2, TICKS_VFAST_H
    ldi temp1, TICKS_VFAST_L
    ret

GRI_CHECK2:
    ; hold >= 2000? ? интервал 200 мс
    cpi temp, TICKS_HOLD2_H
    brlo GRI_TOO_EARLY
    cpi temp1, TICKS_HOLD2_L
    brlo GRI_TOO_EARLY
    ldi temp2, TICKS_FAST_H
    ldi temp1, TICKS_FAST_L
    ret

GRI_TOO_EARLY:
    ; ещё рано
    clr temp2
    clr temp1
    ret

; ========= УВЕЛИЧИТЬ НАСТРАИВАЕМЫЙ ЭЛЕМЕНТ =========
INC_EDIT:
    push temp
    push temp1

    lds temp1, edit_sel
    cpi temp1, 0
    breq INC_HH
    cpi temp1, 1
    breq INC_MM
    rjmp INC_SS

INC_HH:
    lds temp, hh
    inc temp
    cpi temp, 24
    brlo INC_HH_OK
    clr temp
INC_HH_OK:
    sts hh, temp
    rjmp INC_EDIT_END

INC_MM:
    lds temp, mm
    inc temp
    cpi temp, 60
    brlo INC_MM_OK
    clr temp
INC_MM_OK:
    sts mm, temp
    rjmp INC_EDIT_END

INC_SS:
    lds temp, ss
    inc temp
    cpi temp, 60
    brlo INC_SS_OK
    clr temp
INC_SS_OK:
    sts ss, temp

INC_EDIT_END:
    pop temp1
    pop temp
    ret

; ========= УМЕНЬШИТЬ НАСТРАИВАЕМЫЙ ЭЛЕМЕНТ =========
DEC_EDIT:
    push temp
    push temp1

    lds temp1, edit_sel
    cpi temp1, 0
    breq DEC_HH
    cpi temp1, 1
    breq DEC_MM
    rjmp DEC_SS

DEC_HH:
    lds temp, hh
    tst temp
    brne DEC_HH_NOROLL
    ldi temp, 24
DEC_HH_NOROLL:
    dec temp
    sts hh, temp
    rjmp DEC_EDIT_END

DEC_MM:
    lds temp, mm
    tst temp
    brne DEC_MM_NOROLL
    ldi temp, 60
DEC_MM_NOROLL:
    dec temp
    sts mm, temp
    rjmp DEC_EDIT_END

DEC_SS:
    lds temp, ss
    tst temp
    brne DEC_SS_NOROLL
    ldi temp, 60
DEC_SS_NOROLL:
    dec temp
    sts ss, temp

DEC_EDIT_END:
    pop temp1
    pop temp
    ret

; ========= ИНКРЕМЕНТ ВРЕМЕНИ (+1 секунда) =========
INC_TIME:
    push temp

    lds temp, ss
    inc temp
    cpi temp, 60
    brlo IT_SS_OK
    clr temp
    sts ss, temp

    lds temp, mm
    inc temp
    cpi temp, 60
    brlo IT_MM_OK
    clr temp
    sts mm, temp

    lds temp, hh
    inc temp
    cpi temp, 24
    brlo IT_HH_OK
    clr temp
IT_HH_OK:
    sts hh, temp
    rjmp IT_END

IT_MM_OK:
    sts mm, temp
    rjmp IT_END

IT_SS_OK:
    sts ss, temp

IT_END:
    pop temp
    ret

; ========= ВЫВОД НА ИНДИКАТОРЫ =========
; Разряды: 1=старший левый, 4=младший правый
; Формат ЧЧ.ММ: [ч10][ч1.][м10][м1]
; Формат ММ.СС: [м10][м1.][с10][с1]
;
; Мигание: если mode=1 и blink_state=1, настраиваемые два разряда гасим
SHOW:
    push temp
    push temp1
    push temp2
    push temp3

    ; определяем что показывать
    lds temp3, disp_fmt     ; 0=ЧЧ.ММ, 1=ММ.СС

    tst temp3
    brne SHOW_MMSS

    ; === формат ЧЧ.ММ ===
    lds temp,  hh
    rcall SPLIT_TENS        ; temp2=десятки, temp1=единицы
    mov temp3, temp2        ; d1 = ч10
    push temp1              ; стек: ч1

    lds temp,  mm
    rcall SPLIT_TENS
    push temp2              ; стек: ч1, м10
    push temp1              ; стек: ч1, м10, м1

    ; применяем мигание к ЧЧ (edit_sel=0) или ММ (edit_sel=1)
    rcall APPLY_BLINK_HHMM
    rjmp SHOW_DISPLAY

SHOW_MMSS:
    ; === формат ММ.СС ===
    lds temp,  mm
    rcall SPLIT_TENS
    mov temp3, temp2        ; d1 = м10
    push temp1              ; стек: м1

    lds temp,  ss
    rcall SPLIT_TENS
    push temp2              ; стек: м1, с10
    push temp1              ; стек: м1, с10, с1

    ; применяем мигание к ММ (edit_sel=1) или СС (edit_sel=2)
    rcall APPLY_BLINK_MMSS

SHOW_DISPLAY:
    ; стек содержит d4, d3, d2 (снизу вверх), temp3=d1
    ; извлекаем
    pop temp2               ; d4 (единицы правого)
    pop temp1               ; d3 (десятки правого)
    pop temp                ; d2 (единицы левого)
    ; temp3 = d1 (десятки левого)

    ; --- разряд 1 (самый левый, PA3) ---
    push temp
    push temp1
    push temp2
    clr temp
    out PORTA, temp
    mov temp1, temp3
    rcall DIG
    ldi temp, 0x08
    out PORTA, temp
    rcall SMALL

    ; --- разряд 2 (PA2) + точка ---
    pop temp2
    pop temp1
    pop temp
    push temp1
    push temp2
    clr r20
    out PORTA, r20
    mov temp1, temp
    rcall DIG
    ori temp, 0x80          ; точка
    out PORTC, temp
    ldi temp, 0x04
    out PORTA, temp
    rcall SMALL

    ; --- разряд 3 (PA1) ---
    pop temp2
    pop temp1
    clr temp
    out PORTA, temp
    mov temp1, temp1
    rcall DIG
    ldi temp, 0x02
    out PORTA, temp
    rcall SMALL

    ; --- разряд 4 (PA0, самый правый) ---
    clr temp
    out PORTA, temp
    mov temp1, temp2
    rcall DIG
    ldi temp, 0x01
    out PORTA, temp
    rcall SMALL

    ; гасим все разряды
    clr temp
    out PORTA, temp

    pop temp3
    pop temp2
    pop temp1
    pop temp
    ret

; ========= ПРИМЕНИТЬ МИГАНИЕ ДЛЯ ЧЧ.ММ =========
; На стеке (снизу): ч1, м10, м1; temp3=ч10
; Если mode=1 и blink_state=1:
;   edit_sel=0 (ЧЧ): гасим ч10 и ч1
;   edit_sel=1 (ММ): гасим м10 и м1
APPLY_BLINK_HHMM:
    push temp
    push temp1
    lds temp, mode
    tst temp
    breq ABH_END
    lds temp, blink_state
    tst temp                  ; если blink_state=1 — гасим
    breq ABH_END

    lds temp1, edit_sel
    cpi temp1, 0
    brne ABH_CHECK_MM
    ; === Гасим ЧЧ (разряды 1 и 2) ===
    clr temp3                 ; ч10
    in YL, SPL
    in YH, SPH
    clr temp
    std Y+2, temp             ; ч1 (правильное смещение!)
    rjmp ABH_END

ABH_CHECK_MM:
    cpi temp1, 1
    brne ABH_END
    ; === Гасим ММ (разряды 3 и 4) ===
    in YL, SPL
    in YH, SPH
    clr temp
    std Y+0, temp             ; м1
    std Y+1, temp             ; м10
ABH_END:
    pop temp1
    pop temp
    ret

; ========= ПРИМЕНИТЬ МИГАНИЕ ДЛЯ ММ.СС =========
; На стеке (снизу): м1, с10, с1; temp3=м10
; edit_sel=1 (ММ): гасим м10 и м1
; edit_sel=2 (СС): гасим с10 и с1
APPLY_BLINK_MMSS:
    push temp
    push temp1
    lds temp, mode
    tst temp
    breq ABM_END
    lds temp, blink_state
    tst temp
    breq ABM_END

    lds temp1, edit_sel
    cpi temp1, 1
    brne ABM_CHECK_SS
    ; === Гасим ММ ===
    clr temp3                 ; м10
    in YL, SPL
    in YH, SPH
    clr temp
    std Y+2, temp             ; м1
    rjmp ABM_END

ABM_CHECK_SS:
    cpi temp1, 2
    brne ABM_END
    ; === Гасим СС ===
    in YL, SPL
    in YH, SPH
    clr temp
    std Y+0, temp             ; с1
    std Y+1, temp             ; с10
ABM_END:
    pop temp1
    pop temp
    ret

; ========= РАЗБИТЬ ЧИСЛО НА ДЕСЯТКИ И ЕДИНИЦЫ =========
; Вход:  temp = число (0-59 или 0-23)
; Выход: temp2 = десятки, temp1 = единицы
SPLIT_TENS:
    push temp
    clr temp2
ST_LOOP:
    cpi temp, 10
    brlo ST_DONE
    subi temp, 10
    inc temp2
    rjmp ST_LOOP
ST_DONE:
    mov temp1, temp
    pop temp
    ret

; ========= ВЫВЕСТИ ЦИФРУ НА PORTC =========
; Вход: temp1 = цифра (0-9)
DIG:
    push ZL
    push ZH
    ldi ZH, high(TAB*2)
    ldi ZL, low(TAB*2)
    add ZL, temp1
    clr temp
    adc ZH, temp
    lpm temp, Z
    out PORTC, temp
    pop ZH
    pop ZL
    ret

; ========= ПАУЗА ДЛЯ МУЛЬТИПЛЕКСИРОВАНИЯ =========
SMALL:
    ldi temp2, 120
SMALL_LOOP:
    dec temp2
    brne SMALL_LOOP
    ret

; ========= ТАБЛИЦА СЕГМЕНТОВ (0-9) =========
TAB:
    .db 0x3F, 0x06  ; 0, 1
    .db 0x5B, 0x4F  ; 2, 3
    .db 0x66, 0x6D  ; 4, 5
    .db 0x7D, 0x07  ; 6, 7
    .db 0x7F, 0x6F  ; 8, 9