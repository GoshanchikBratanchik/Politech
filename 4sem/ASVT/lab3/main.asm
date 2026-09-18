

.include "m32def.inc"

.dseg
digit_buffer: .byte 4      ; сегментные коды для разрядов 0..3

.cseg
.org 0x0000
    rjmp RESET
.org 0x0002                 ; вектор INT0
    rjmp INT0_ISR
.org 0x0004                 ; вектор INT1
    rjmp INT1_ISR
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

    ; заполняем буфер разрядов кодом "0" (начальное время 00:00:00)
    ldi  YL, LOW(digit_buffer)
    ldi  YH, HIGH(digit_buffer)
    ldi  r16, 0x3F
    st   Y+, r16
    st   Y+, r16
    st   Y+, r16
    st   Y,  r16

    ; переменные состояния
    ldi  r17, 0x01     ; маска активного разряда
    clr  r18            ; индекс активного разряда (0..3), для мультиплексирования
    clr  r19            ; бит0=режим(0=отображение,1=настройка)
                         ; бит1=фаза мигания
                         ; бит2=формат отображения (0=ММСС, 1=ЧЧММ)
    clr  r20            ; счётчик тиков секундного интервала (младший байт)
    clr  r21            ; счётчик тиков секундного интервала (старший байт)
    clr  r23            ; секунды (0..59)
    clr  r24            ; минуты  (0..59)
    clr  r22            ; часы    (0..23)
    clr  r25            ; счётчик тиков для мигания (0..121)
    clr  r27             ; редактируемый элемент: 0=ЧЧ, 1=ММ, 2=СС
    clr  r5               ; предыдущее состояние кнопок PD0,PD1 (биты 0,1)

    clr  r8                ; счётчик удержания PD0 (младший байт)
    clr  r9                ; счётчик удержания PD0 (старший байт)
    clr  r10               ; счётчик удержания PD1 (младший байт)
    clr  r11               ; счётчик удержания PD1 (старший байт)
    clr  r12               ; суб-счётчик автоповтора PD0
    clr  r13               ; суб-счётчик автоповтора PD1

    out  PORTA, r17

    ; ---- настройка INT0 (PD2) и INT1 (PD3), нарастающий фронт 0->1 ----
    ; (на плате уровень покоя = 0 уже обеспечен аппаратно, подтяжка не нужна)
    ldi  r16, (1<<ISC11)|(1<<ISC10)|(1<<ISC01)|(1<<ISC00)
    out  MCUCR, r16
    ldi  r16, (1<<INT1)|(1<<INT0)
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
; INT0 - нажатие кнопки PD2: переключаем режим (бит0 регистра r19).
;   Если этим переключением ВОШЛИ в режим настройки - подгоняем
;   формат отображения под текущий редактируемый элемент.
; ================================================================
INT0_ISR:
    push r16
    in   r16, SREG
    push r16
    push r6
    push r7
    push YL
    push YH
    push ZL
    push ZH

    ldi  r16, 0x01
    eor  r19, r16

    sbrs r19, 0              ; бит0=0 после переключения -> ушли в отображение
    rjmp INT0_DONE            ; -> формат не трогаем

    ; вошли в настройку -> подогнать формат и обновить буфер
    rcall APPLY_ELEMENT_FORMAT
    rcall UPDATE_DISPLAY_BUFFER

INT0_DONE:
    pop  ZH
    pop  ZL
    pop  YH
    pop  YL
    pop  r7
    pop  r6
    pop  r16
    out  SREG, r16
    pop  r16
    reti

; ================================================================
; INT1 - нажатие кнопки PD3:
;   - в режиме отображения (r19 бит0=0): переключает формат
;     ЧЧММ <-> ММСС и сразу пересчитывает digit_buffer;
;   - в режиме настройки (r19 бит0=1): циклически переключает
;     редактируемый элемент r27: 0(ЧЧ) -> 1(ММ) -> 2(СС) -> 0 ...
;     и подгоняет формат отображения под новый элемент.
; ================================================================
INT1_ISR:
    push r16
    in   r16, SREG
    push r16
    push r6
    push r7
    push YL
    push YH
    push ZL
    push ZH

    sbrc r19, 0
    rjmp INT1_SETTINGS

    ; --- режим отображения: переключаем формат ЧЧММ <-> ММСС ---
    ldi  r16, 0x04
    eor  r19, r16
    rcall UPDATE_DISPLAY_BUFFER
    rjmp INT1_DONE

INT1_SETTINGS:
    ; --- режим настройки: циклический выбор элемента ЧЧ->ММ->СС->ЧЧ ---
    inc  r27
    cpi  r27, 3
    brne INT1_ELEM_OK
    clr  r27
INT1_ELEM_OK:
    rcall APPLY_ELEMENT_FORMAT
    rcall UPDATE_DISPLAY_BUFFER

INT1_DONE:
    pop  ZH
    pop  ZL
    pop  YH
    pop  YL
    pop  r7
    pop  r6
    pop  r16
    out  SREG, r16
    pop  r16
    reti


TIMER0_OVF_ISR:
    push r16
    in   r16, SREG
    push r16
    push YL
    push YH
    push ZL
    push ZH
    push r4

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

    ; --- эффект мигания: если сейчас показывается разряд из ПАРЫ,
    ;     в которой лежит редактируемый элемент r27, режим=настройка
    ;     и фаза="выключено" -> гасим этот разряд ---
    mov  r16, r18
    lsr  r16                  ; r16 = номер пары текущего разряда (0=правая,1=левая)

    ; --- номер ЦЕЛЕВОЙ пары (в которой сейчас должен быть виден
    ;     редактируемый элемент r27: 0=ЧЧ,1=ММ,2=СС) ---
    cpi  r27, 0
    breq PART1_TP_LEFT         ; ЧЧ -> левая пара (формат уже принудительно ЧЧММ)
    cpi  r27, 2
    breq PART1_TP_RIGHT        ; СС -> правая пара (формат уже принудительно ММСС)
    ; r27 == 1 (ММ): позиция зависит от текущего формата
    sbrc r19, 2
    rjmp PART1_TP_RIGHT         ; формат ЧЧММ
PART1_TP_LEFT:
    clr  r7
    inc  r7                    ; r7 = 1 (LDI недопустим для r7, диапазон r16-r31)
    rjmp PART1_TP_GOT
PART1_TP_RIGHT:
    clr  r7
PART1_TP_GOT:

    cp   r16, r7
    brne PART1_NOBLINK

    sbrs r19, 0              ; бит0=0 (отображение) -> мигания нет
    rjmp PART1_NOBLINK

    sbrc r19, 1              ; бит1=0 (фаза "выкл") -> гасим ниже
    rjmp PART1_NOBLINK        ; бит1=1 (фаза "вкл") -> оставляем как есть

    clr  r17

PART1_NOBLINK:
    out  PORTA, r17

    ; ---------- часть 2: переключение фазы мигания (2 Гц) ----------
    inc  r25
    cpi  r25, 122             ; ~250 мс при шаге ~2.048 мс
    brlo PART2_DONE
    clr  r25
    ldi  r16, 0x02
    eor  r19, r16             ; инвертируем бит1 (фаза мигания)
PART2_DONE:

    ; ---------- часть 3: секундный интервал + перенос ЧЧ:ММ:СС ----------
    inc  r20
    brne PART3_NOCARRY
    inc  r21
PART3_NOCARRY:
    ldi  r16, LOW(488)
    cp   r20, r16
    ldi  r16, HIGH(488)
    cpc  r21, r16
    brlo PART3_DONE

    clr  r20
    clr  r21

    sbrc r19, 0               ; бит0=1 (настройка) -> автоход времени стоит
    rjmp PART3_DONE

    ; --- секунды -> минуты -> часы, переход 23:59:59 -> 00:00:00 ---
    inc  r23                  ; r23 = секунды (0..59)
    cpi  r23, 60
    brne PART3_UPDBUF

    clr  r23
    inc  r24                  ; r24 = минуты (0..59)
    cpi  r24, 60
    brne PART3_UPDBUF

    clr  r24
    inc  r22                  ; r22 = часы (0..23)
    cpi  r22, 24
    brne PART3_UPDBUF
    clr  r22

PART3_UPDBUF:
    rcall UPDATE_DISPLAY_BUFFER

PART3_DONE:

    ; ---------- часть 4: кнопки PD0 (+1) / PD1 (-1) ----------
    ; ловим именно МОМЕНТ нажатия (переход 0->1), а не "держится нажатой"
    in   r16, PIND
    andi r16, 0x03           ; интересуют только биты PD0(0), PD1(1)
    mov  r4, r5               ; r4 = предыдущее состояние
    mov  r5, r16               ; запоминаем текущее состояние на следующий раз
    com  r4                     ; r4 = НЕ(предыдущее)
    and  r4, r16                 ; r4 = биты, которые СЕЙЧАС=1, а РАНЬШЕ были=0

    sbrs r19, 0                ; бит0=0 (отображение) -> кнопки PD0/PD1 не действуют
    rjmp PART4_DONE

    sbrc r4, 0                  ; PD0 только что нажали?
    rcall INC_SELECTED

    sbrc r4, 1                  ; PD1 только что нажали?
    rcall DEC_SELECTED

PART4_DONE:

    ; ---------- часть 5: автоповтор при удержании (2 ступени) ----------
    ;   удержание >= ~2с  -> период автоповтора ~200 мс (98 тиков)
    ;   удержание >= ~4с  -> период автоповтора ~100 мс (49 тиков)
    sbrs r19, 0                 ; бит0=0 (отображение) -> автоповтор не нужен
    rjmp PART5_DONE

    ; --- PD0: счётчик удержания и ускоряющийся автоповтор (+1) ---
    in   r16, PIND
    andi r16, 0x03
    sbrc r16, 0
    rjmp PD0_HELD
    clr  r8
    clr  r9
    clr  r12
    rjmp PD0_DONE
PD0_HELD:
    inc  r8
    brne PD0_NOCARRY
    inc  r9
PD0_NOCARRY:
    ldi  r16, LOW(977)          ; ~2 секунды при шаге ~2.048 мс
    cp   r8, r16
    ldi  r16, HIGH(977)
    cpc  r9, r16
    brlo PD0_DONE                ; ещё не набрали 2 секунды

    ldi  r16, LOW(1953)          ; ~4 секунды при шаге ~2.048 мс
    cp   r8, r16
    ldi  r16, HIGH(1953)
    cpc  r9, r16
    brlo PD0_PERIOD_200           ; от 2 до 4 секунд -> период ~200 мс

    ; --- удержание >= 4 сек -> период ~100 мс ---
    inc  r12
    ldi  r16, 49
    cp   r12, r16
    brlo PD0_DONE
    clr  r12
    rcall INC_SELECTED
    rjmp PD0_DONE

PD0_PERIOD_200:
    inc  r12
    ldi  r16, 98
    cp   r12, r16
    brlo PD0_DONE
    clr  r12
    rcall INC_SELECTED
PD0_DONE:

    ; --- PD1: счётчик удержания и ускоряющийся автоповтор (-1) ---
    in   r16, PIND
    andi r16, 0x03
    sbrc r16, 1
    rjmp PD1_HELD
    clr  r10
    clr  r11
    clr  r13
    rjmp PD1_DONE
PD1_HELD:
    inc  r10
    brne PD1_NOCARRY
    inc  r11
PD1_NOCARRY:
    ldi  r16, LOW(977)
    cp   r10, r16
    ldi  r16, HIGH(977)
    cpc  r11, r16
    brlo PD1_DONE

    ldi  r16, LOW(1953)
    cp   r10, r16
    ldi  r16, HIGH(1953)
    cpc  r11, r16
    brlo PD1_PERIOD_200

    inc  r13
    ldi  r16, 49
    cp   r13, r16
    brlo PD1_DONE
    clr  r13
    rcall DEC_SELECTED
    rjmp PD1_DONE

PD1_PERIOD_200:
    inc  r13
    ldi  r16, 98
    cp   r13, r16
    brlo PD1_DONE
    clr  r13
    rcall DEC_SELECTED
PD1_DONE:

PART5_DONE:
    pop  r4
    pop  ZH
    pop  ZL
    pop  YH
    pop  YL
    pop  r16
    out  SREG, r16
    pop  r16
    reti

; ================================================================
; INC_SELECTED - увеличивает на 1 значение элемента, выбранного
;                для настройки (r27: 0=ЧЧ,1=ММ,2=СС), с циклическим
;                переходом через максимум, и обновляет digit_buffer.
; ================================================================
INC_SELECTED:
    cpi  r27, 0
    breq INC_HOURS
    cpi  r27, 1
    breq INC_MINUTES

    ; r27 == 2 (секунды)
    inc  r23
    cpi  r23, 60
    brne INC_DONE
    clr  r23
    rjmp INC_DONE

INC_HOURS:
    inc  r22
    cpi  r22, 24
    brne INC_DONE
    clr  r22
    rjmp INC_DONE

INC_MINUTES:
    inc  r24
    cpi  r24, 60
    brne INC_DONE
    clr  r24

INC_DONE:
    rcall UPDATE_DISPLAY_BUFFER
    ret

; ================================================================
; DEC_SELECTED - уменьшает на 1 значение выбранного элемента
;                (r27: 0=ЧЧ,1=ММ,2=СС), с переходом через 0, и
;                обновляет digit_buffer.
; ================================================================
DEC_SELECTED:
    cpi  r27, 0
    breq DEC_HOURS
    cpi  r27, 1
    breq DEC_MINUTES

    ; r27 == 2 (секунды)
    cpi  r23, 0
    brne DEC_SEC_SUB
    ldi  r23, 59
    rjmp DEC_DONE
DEC_SEC_SUB:
    dec  r23
    rjmp DEC_DONE

DEC_HOURS:
    cpi  r22, 0
    brne DEC_HOUR_SUB
    ldi  r22, 23
    rjmp DEC_DONE
DEC_HOUR_SUB:
    dec  r22
    rjmp DEC_DONE

DEC_MINUTES:
    cpi  r24, 0
    brne DEC_MIN_SUB
    ldi  r24, 59
    rjmp DEC_DONE
DEC_MIN_SUB:
    dec  r24

DEC_DONE:
    rcall UPDATE_DISPLAY_BUFFER
    ret

; ================================================================
; APPLY_ELEMENT_FORMAT - при необходимости принудительно
;   переключает формат отображения (бит2 r19), чтобы редактируемый
;   элемент (r27: 0=ЧЧ,1=ММ,2=СС) был виден на экране:
;     r27=0 (ЧЧ) -> формат ЧЧММ (бит2=1)
;     r27=2 (СС) -> формат ММСС (бит2=0)
;     r27=1 (ММ) -> формат не меняется (минуты видны в обоих форматах)
;   портит r16
; ================================================================
APPLY_ELEMENT_FORMAT:
    cpi  r27, 0
    breq AEF_HH
    cpi  r27, 2
    breq AEF_SS
    ret

AEF_HH:
    ori  r19, 0x04
    ret

AEF_SS:
    andi r19, 0xFB
    ret

; ================================================================
; UPDATE_DISPLAY_BUFFER - пересчитывает 4 сегментных кода в
;   digit_buffer из текущих часов/минут/секунд (r22/r24/r23)
;   согласно текущему формату отображения (бит2 регистра r19):
;     бит2=0 -> ММСС: digit_buffer[3:2]=минуты, [1:0]=секунды
;     бит2=1 -> ЧЧММ: digit_buffer[3:2]=часы,   [1:0]=минуты
;   Портит r6, r7, r16, Y, Z.
; ================================================================
UPDATE_DISPLAY_BUFFER:
    sbrc r19, 2
    rjmp UDB_HHMM

UDB_MMSS:
    mov  r16, r24             ; левая пара = минуты
    rcall SPLIT10
    rcall WRITE_PAIR23
    mov  r16, r23             ; правая пара = секунды
    rcall SPLIT10
    rcall WRITE_PAIR01
    ret

UDB_HHMM:
    mov  r16, r22             ; левая пара = часы
    rcall SPLIT10
    rcall WRITE_PAIR23
    mov  r16, r24             ; правая пара = минуты
    rcall SPLIT10
    rcall WRITE_PAIR01
    ret

; ----------------------------------------------------------------
; SPLIT10 - деление на 10 повторным вычитанием.
;   вход:  r16 = значение (0..99)
;   выход: r16 = единицы (остаток), r6 = десятки (частное)
; ----------------------------------------------------------------
SPLIT10:
    clr  r6
SPLIT10_LOOP:
    cpi  r16, 10
    brlo SPLIT10_DONE
    subi r16, 10
    inc  r6
    rjmp SPLIT10_LOOP
SPLIT10_DONE:
    ret

; ----------------------------------------------------------------
; WRITE_PAIR01 - пишет сегментные коды в правую пару разрядов:
;   вход: r16 = единицы (-> digit_buffer+0), r6 = десятки (-> +1)
;   портит r7, Y, Z
; ----------------------------------------------------------------
WRITE_PAIR01:
    push r6
    ldi  ZL, LOW(SEG_TABLE*2)
    ldi  ZH, HIGH(SEG_TABLE*2)
    clr  r7
    add  ZL, r16
    adc  ZH, r7
    lpm  r16, Z
    ldi  YL, LOW(digit_buffer)
    ldi  YH, HIGH(digit_buffer)
    st   Y, r16
    pop  r16                   ; десятки
    ldi  ZL, LOW(SEG_TABLE*2)
    ldi  ZH, HIGH(SEG_TABLE*2)
    clr  r7
    add  ZL, r16
    adc  ZH, r7
    lpm  r16, Z
    ldi  YL, LOW(digit_buffer+1)
    ldi  YH, HIGH(digit_buffer+1)
    st   Y, r16
    ret

; ----------------------------------------------------------------
; WRITE_PAIR23 - пишет сегментные коды в левую пару разрядов:
;   вход: r16 = единицы (-> digit_buffer+2), r6 = десятки (-> +3)
;   портит r7, Y, Z
; ----------------------------------------------------------------
WRITE_PAIR23:
    push r6
    ldi  ZL, LOW(SEG_TABLE*2)
    ldi  ZH, HIGH(SEG_TABLE*2)
    clr  r7
    add  ZL, r16
    adc  ZH, r7
    lpm  r16, Z
    ldi  YL, LOW(digit_buffer+2)
    ldi  YH, HIGH(digit_buffer+2)
    st   Y, r16
    pop  r16                   ; десятки
    ldi  ZL, LOW(SEG_TABLE*2)
    ldi  ZH, HIGH(SEG_TABLE*2)
    clr  r7
    add  ZL, r16
    adc  ZH, r7
    lpm  r16, Z
    ldi  YL, LOW(digit_buffer+3)
    ldi  YH, HIGH(digit_buffer+3)
    st   Y, r16
    ret
