#define F_CPU 8000000UL
#define GARLAND_DIV 64

#include <avr/interrupt.h>
#include <avr/io.h>

// ================================================================
// Гирлянда, Логика 3, Вариант 3.
//
// Общий для всех трёх портов массив "картинок" (наборов горящих
// светодиодов) b_arr[0..4]. Каждый порт хранит СВОЙ индекс текущего
// элемента этого массива и циклически перебирает его с шагом h
// (свой для каждого порта, может быть отрицательным). Скорость смены
// состояния берётся из ОБЩЕГО массива p_arr[0..4] по ТЕКУЩЕМУ индексу
// порта (т.е. чем дальше уехал индекс, тем другая скорость - если
// p_arr[i] разные).
//
// Настраиваемые параметры (по заданию варианта 3):
//   b0 - b_arr[0], диапазон [0;255]
//   p0 - p_arr[0], диапазон [1;10] (раз в 2 секунды)
//   ha - шаг индекса для PORTA, диапазон [-2;2]
// Зафиксировано заданием:
//   b1=1,b2=2,b3=3,b4=4 ; hb=1,hc=2 ; p1=1,p2=2,p3=3,p4=4 ; da=db=dc=0
// ================================================================

uint8_t b_arr[5] = {0, 1, 2, 3, 4};    // b_arr[0] = b0 (настраиваемый)
uint8_t p_arr[5] = {0x0F, 1, 2, 3, 4}; // p_arr[0] = p0 (настраиваемый)

int8_t ha = 3; // настраиваемый, [-2;2]
int8_t hb = 1; // фиксирован
int8_t hc = 2; // фиксирован

uint8_t da = 0, db = 0, dc = 0; // фиксированы (начальные индексы)

uint8_t idxA, idxB, idxC; // текущий индекс в b_arr для каждого порта
uint8_t count_a = 0, count_b = 0, count_c = 0; // счётчики шагов гирлянды

volatile int mode = 0;      // 0 - гирлянда, 1 - режим настройки
volatile int cur_param = 0; // 0 -> b0, 1 -> p0, 2 -> ha
volatile unsigned int ind[4] = {0, 0, 0, 0};
volatile int cur_ind = 0; // 0-3

const unsigned char numb[] = {
    0b11000000, // 0  — индекс 0
    0b11111001, // 1  — индекс 1
    0b10100100, // 2  — индекс 2
    0b10110000, // 3  — индекс 3
    0b10011001, // 4  — индекс 4
    0b10010010, // 5  — индекс 5
    0b10000010, // 6  — индекс 6
    0b11111000, // 7  — индекс 7
    0b10000000, // 8  — индекс 8
    0b10010000, // 9  — индекс 9
    0b00001000, // a. — индекс 10
    0b00000011, // b. — индекс 11
    0b01000110, // c. — индекс 12
    0b10001100, // p  — индекс 13
    0b10001011, // h  — индекс 14
    0b10000011, // b  — индекс 15
    0b10100001, // d  — индекс 16
    0b10111111, // -  — индекс 17
    0b11111111  // ' '— индекс 18
};

// переменные АЦП
volatile unsigned int adc_value = 0;
volatile uint8_t adc_ready = 0;

// выводит b_arr[текущий индекс] на все три порта
void update_garland(void) {
  PORTA = b_arr[idxA];
  PORTB = b_arr[idxB];
  PORTC = b_arr[idxC];
}

// сдвигает индекс idx на шаг h по кругу в пределах 0..4;
// h может быть отрицательным (движение "назад" по массиву)
uint8_t advance_index(uint8_t idx, int8_t h) {
  int tmp = (int)idx + h;
  tmp = ((tmp % 5) + 5) % 5; // приводим к 0..4 даже если tmp отрицательное
  return (uint8_t)tmp;
}

void init_garlands(void) {
  idxA = da;
  idxB = db;
  idxC = dc;
  count_a = count_b = count_c = 0;
  update_garland();
}

// обновляет ind[] под текущий выбранный параметр (b0/p0/ha) и его значение
void show_param_name(void) {
  switch (cur_param) {
  case 0:        // b0, значение 0-255 - нужны ДВЕ hex-цифры
    ind[0] = 15; // 'b' (без точки)
    ind[1] = 0;  // '0' - точку на эту позицию форсируем в прерывании таймера
    ind[2] = b_arr[0] >> 4;   // старший полубайт
    ind[3] = b_arr[0] & 0x0F; // младший полубайт
    break;

  case 1:        // p0, значение 1-10, влезает в 1 hex-разряд
    ind[0] = 13; // 'p'
    ind[1] = 0;  // '0'
    ind[2] = 18; // пусто
    ind[3] = p_arr[0];
    break;

  default:       // ha, значение -2..2, может быть отрицательным
    ind[0] = 14; // 'h'
    ind[1] = 10; // 'a.' (точка уже встроена в этот глиф)
    if (ha < 0) {
      ind[2] = 17; // '-'
      ind[3] = (unsigned int)(-ha);
    } else {
      ind[2] = 18; // пусто
      ind[3] = (unsigned int)ha;
    }
    break;
  }
}

void timer_init(void) {
  TCCR0 =
      (1 << WGM01) | (1 << CS01) | (1 << CS00); // режим CTC, предделитель 64
  OCR0 = 154;
  TIMSK |= (1 << OCIE0); // разрешить прерывание

  TCCR1A =
      (1 << COM1B1) | (1 << WGM10); // ШИМ на ножке PD4, неинвертированный режим
  TCCR1B = (1 << WGM12) | (1 << CS10); // делитель 1, максимальная скорость

  OCR1B = 0; // начальное значение (0% скважности)

  // Timer2 (ШИМ на PD7)
  TCCR2 =
      (1 << WGM21) | (1 << WGM20) | (1 << COM21) | (1 << COM20) | (1 << CS20);
  OCR2 = 0;
}

void ADC_init(void) {
  ADMUX =
      (1 << REFS0) | (1 << MUX2) | (1 << MUX0); // AVCC, канал ADC5 (ножка PA5)
  ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADATE) | (1 << ADPS2) |
           (1 << ADPS1) | (1 << ADIE);
}

ISR(ADC_vect) {
  adc_value = ADC;
  adc_ready = 1;
}

ISR(TIMER0_COMP_vect) {
  // режим настройки
  if (mode == 1) {
    PORTA = 0x00; // 1. гасим все индикаторы
    if (++cur_ind > 3)
      cur_ind = 0; // 2. переходим к следующему разряду

    unsigned char code = numb[ind[3 - cur_ind]];
    if (cur_ind == 2) { // это позиция ind[1] - конец "имени"
      code &= 0x7F;     // принудительно включаем точку (бит7 = DP)
    }

    PORTC = ~code;          // 3. устанавливаем символ
    PORTA = (1 << cur_ind); // 4. включаем нужный индикатор
  }

  // режим гирлянды
  if (mode == 0) {
    static uint8_t garland_tick = 0;
    garland_tick++;
    if (garland_tick >= GARLAND_DIV) { // каждые 64 тика = один шаг гирлянды
      garland_tick = 0;

      if (++count_a >= (25 / p_arr[idxA])) {
        count_a = 0;
        idxA = advance_index(idxA, ha);
      }
      if (++count_b >= (25 / p_arr[idxB])) {
        count_b = 0;
        idxB = advance_index(idxB, hb);
      }
      if (++count_c >= (25 / p_arr[idxC])) {
        count_c = 0;
        idxC = advance_index(idxC, hc);
      }
      update_garland();
    }
  }
}

// кнопка PD2 - переключатель между режимами
ISR(INT0_vect) {
  if (mode == 0) {
    mode = 1;
    cur_param = 0;
    PORTA = 0x00;
    PORTB = 0x00;
    PORTC = 0x00;
    show_param_name();
  } else {
    mode = 0;
    init_garlands();
  }
}

// кнопка PD3 - циклическое переключение между настраиваемыми
// параметрами (b0 -> p0 -> ha -> b0 ...)
ISR(INT1_vect) {
  if (mode == 1) {
    cur_param++;
    if (cur_param > 2)
      cur_param = 0;
    show_param_name();
  }
}

int main() {
  timer_init();
  ADC_init();

  DDRA = 0xDF;        // PA5 вход (потенциометр), остальные выходы
  DDRB = DDRC = 0xFF; // все выходы (гирлянда)
  DDRD = 0x90;        // PD7 и PD4 выходы (ШИМ), PD2 PD3 входы (кнопки)

  MCUCR = (1 << ISC11) | (1 << ISC10) | (1 << ISC01) | (1 << ISC00);
  GICR = (1 << INT1) | (1 << INT0);

  init_garlands();
  sei();

  while (1) {
    if (mode == 1) {
      if (adc_ready) {
        adc_ready = 0;

        static unsigned int adc_sum = 0;
        static uint8_t adc_count = 0;

        adc_sum += adc_value;
        adc_count++;

        if (adc_count >= 16) {
          unsigned int adc_avg = adc_sum / 16;
          adc_sum = 0;
          adc_count = 0;

          uint8_t brightness = (uint8_t)(adc_avg >> 2);
          OCR2 = brightness;  // PD7
          OCR1B = brightness; // PD4

          switch (cur_param) {
          case 0: // b0: [0;255]
            b_arr[0] = (uint8_t)(adc_avg * 256UL / 1024);
            break;

          case 1: // p0: [1;10]
            p_arr[0] = (uint8_t)(adc_avg * 10UL / 1024) + 1;
            if (p_arr[0] > 10)
              p_arr[0] = 10;
            break;

          default: { // ha: [-2;2]
            int idx = (int)(adc_avg * 5UL / 1024);
            if (idx > 4)
              idx = 4;
            ha = (int8_t)(idx - 2);
            break;
          }
          }
          show_param_name();
        }
      }
    }
  }
}
