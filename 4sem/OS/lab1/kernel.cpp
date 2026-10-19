// Эта инструкция обязательно должна быть первой: загрузчик передаёт управление
// на начало образа ядра, а отсюда идёт переход на kmain
__asm("jmp kmain");

#define VIDEO_BUF_PTR 0xb8000
#define VIDEO_WIDTH 80
#define VIDEO_HEIGHT 25
#define CURSOR_PORT 0x3D4
#define IDT_TYPE_INTR 0x0E
#define IDT_TYPE_TRAP 0x0F
#define GDT_CS 0x08
#define PIC1_PORT 0x20
#define COMMANDS_COUNT 6
#define MAX_COMMAND_LEN 40

// ---------------------------------------------------------------------------
// Глобальные данные
// ---------------------------------------------------------------------------

unsigned int cur_row = 0;       // текущая строка вывода
unsigned int cur_col = 0;       // текущий столбец вывода
unsigned char cur_color = 0x07; // атрибут цвета текста
unsigned char flag_shift = 0;   // зажат ли Shift
int count_symbols = 0;          // длина введённой команды
int what_command = 0;           // номер распознанной команды
unsigned char color_index = 0;  // номер цвета, выбранного в загрузчике

// Порядок совпадает с порядком пунктов меню в загрузчике
const char *colors[] = {"gray", "cyan", "white", "green", "yellow", "magenta"};
unsigned char colors_cods[] = {0x07, 0x0B, 0x0F, 0x0A, 0x0E, 0x0D};

// Таблицы перевода скан-кодов в символы: индекс = скан-код
unsigned char symbols[58] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',  '\b',
    0,   'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0,   0,   '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 0,   0,   0,   0,   0,    'z',
    'x', 'c', 'v', 'b', 'n', 'm', 0,   0,   '/', 0,   '*', 0,   ' '};

unsigned char symbols_shift[58] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   '*', 0,   0, 0,  '+',  '\b',
    0,   'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0, 0,  '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0,   0,   0, 0,  0,    'Z',
    'X', 'C', 'V', 'B', 'N', 'M', 0,   0,   '/', 0,   '*', 0, ' '};

unsigned char command[MAX_COMMAND_LEN + 1] = {0}; // буфер вводимой команды

const char *my_commands[] = {"info", "gcd", "solve", "lcm", "div", "shutdown"};

// ---------------------------------------------------------------------------
// Прототипы
// ---------------------------------------------------------------------------

void on_key(unsigned char scan_code);
void check_command();
void run_command();
int is_digit(char c);
int skip_spaces(int pos);
int check_number(int pos, int allow_minus);
int check_two_numbers(int pos, int allow_minus);
int check_equation(int pos);
int check_args();

// ---------------------------------------------------------------------------
// Прерывания
// ---------------------------------------------------------------------------

struct idt_entry {
  unsigned short base_lo;  // младшие биты адреса обработчика
  unsigned short segm_sel; // селектор сегмента кода
  unsigned char always0;   // всегда 0
  unsigned char flags;     // P, DPL, тип шлюза
  unsigned short base_hi;  // старшие биты адреса обработчика
} __attribute__((packed));

struct idt_ptr {
  unsigned short limit;
  unsigned int base;
} __attribute__((packed));

struct idt_entry g_idt[256]; // таблица IDT
struct idt_ptr g_idtp;       // описатель таблицы для lidt

// Пустой обработчик для всех прерываний, которые мы не обрабатываем
void default_intr_handler() {
  asm("pusha");
  asm("popa; leave; iret");
}

typedef void (*intr_handler)();

// Записать обработчик hndlr в элемент num таблицы IDT
void intr_reg_handler(int num, unsigned short segm_sel, unsigned short flags,
                      intr_handler hndlr) {
  unsigned int hndlr_addr = (unsigned int)hndlr;

  g_idt[num].base_lo = (unsigned short)(hndlr_addr & 0xFFFF);
  g_idt[num].segm_sel = segm_sel;
  g_idt[num].always0 = 0;
  g_idt[num].flags = flags;
  g_idt[num].base_hi = (unsigned short)(hndlr_addr >> 16);
}

// Заполнить всю таблицу пустыми обработчиками
void intr_init() {
  int idt_count = sizeof(g_idt) / sizeof(g_idt[0]);

  for (int i = 0; i < idt_count; i++)
    intr_reg_handler(i, GDT_CS, 0x80 | IDT_TYPE_INTR, default_intr_handler);
}

// Загрузить таблицу в регистр IDTR
void intr_start() {
  int idt_count = sizeof(g_idt) / sizeof(g_idt[0]);

  g_idtp.base = (unsigned int)(&g_idt[0]);
  g_idtp.limit = (sizeof(struct idt_entry) * idt_count) - 1;

  asm("lidt %0" : : "m"(g_idtp));
}

void intr_enable() { asm("sti"); }

void intr_disable() { asm("cli"); }

// ---------------------------------------------------------------------------
// Порты ввода-вывода
// ---------------------------------------------------------------------------

static inline unsigned char inb(unsigned short port) {
  unsigned char data;
  asm volatile("inb %w1, %b0" : "=a"(data) : "Nd"(port));
  return data;
}

static inline void outb(unsigned short port, unsigned char data) {
  asm volatile("outb %b0, %w1" : : "a"(data), "Nd"(port));
}

static inline void outw(unsigned short port, unsigned short data) {
  asm volatile("outw %w0, %w1" : : "a"(data), "Nd"(port));
}

// ---------------------------------------------------------------------------
// Клавиатура
// ---------------------------------------------------------------------------

// Прочитать скан-код из контроллера клавиатуры
void keyb_process_keys() {
  if (inb(0x64) & 0x01) {
    unsigned char scan_code = inb(0x60);
    on_key(scan_code);
  }
}

// Обработчик прерывания клавиатуры (IRQ1, вектор 0x09)
void keyb_handler() {
  asm("pusha");
  keyb_process_keys();
  outb(PIC1_PORT, 0x20); // сообщить контроллеру 8259, что прерывание обработано
  asm("popa; leave; iret");
}

// Зарегистрировать обработчик и разрешить только прерывания клавиатуры
void keyb_init() {
  intr_reg_handler(0x09, GDT_CS, 0x80 | IDT_TYPE_INTR, keyb_handler);
  outb(PIC1_PORT + 1, 0xFF ^ 0x02);
}

// ---------------------------------------------------------------------------
// Вывод на экран
// ---------------------------------------------------------------------------

// Переместить мигающий курсор в строку strnum, столбец pos
void cursor_moveto(unsigned int strnum, unsigned int pos) {
  unsigned short new_pos = (strnum * VIDEO_WIDTH) + pos;
  outb(CURSOR_PORT, 0x0F);
  outb(CURSOR_PORT + 1, (unsigned char)(new_pos & 0xFF));
  outb(CURSOR_PORT, 0x0E);
  outb(CURSOR_PORT + 1, (unsigned char)((new_pos >> 8) & 0xFF));
}

// Заполнить строку strnum пробелами
void clear_line(int strnum) {
  unsigned char *video_buf = (unsigned char *)VIDEO_BUF_PTR;
  for (int i = 0; i < VIDEO_WIDTH; i++) {
    video_buf[(strnum * VIDEO_WIDTH + i) * 2] = ' ';
    video_buf[(strnum * VIDEO_WIDTH + i) * 2 + 1] = cur_color;
  }
  cur_col = 0;
  cur_row = strnum;
}

// Очистить весь экран
void clear_screen() {
  unsigned char *video_buf = (unsigned char *)VIDEO_BUF_PTR;
  for (int i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT; i++) {
    video_buf[i * 2] = ' ';
    video_buf[i * 2 + 1] = cur_color;
  }
  cur_row = 0;
  cur_col = 0;
  cursor_moveto(0, 0);
}

// Сдвинуть экран на одну строку вверх, последнюю строку очистить
void scroll() {
  unsigned short *video_buf = (unsigned short *)VIDEO_BUF_PTR;
  for (int i = VIDEO_WIDTH; i < VIDEO_WIDTH * VIDEO_HEIGHT; i++)
    video_buf[i - VIDEO_WIDTH] = video_buf[i];
  clear_line(VIDEO_HEIGHT - 1);
}

// Перейти на новую строку, при необходимости прокрутить экран
void newline() {
  cur_col = 0;
  cur_row += 1;
  if (cur_row == VIDEO_HEIGHT)
    scroll();
  cursor_moveto(cur_row, cur_col);
}

// Вывести один символ в текущую позицию
void my_putchar(char c) {
  unsigned char *video_buf = (unsigned char *)VIDEO_BUF_PTR;
  if (c == '\n') {
    newline();
    return;
  }
  video_buf[(cur_row * VIDEO_WIDTH + cur_col) * 2] = c;
  video_buf[(cur_row * VIDEO_WIDTH + cur_col) * 2 + 1] = cur_color;
  cur_col += 1;
  if (cur_col == VIDEO_WIDTH)
    newline();
  cursor_moveto(cur_row, cur_col);
}

// Вывести строку, оканчивающуюся нулём
void my_puts(const char *s) {
  for (int i = 0; s[i] != '\0'; i++)
    my_putchar(s[i]);
}

// Вывести беззнаковое число в десятичном виде
void print_uint(unsigned int n) {
  char buf[10]; // в unsigned int не больше 10 цифр
  int i = 0;
  do {
    buf[i++] = '0' + n % 10;
    n /= 10;
  } while (n != 0);
  while (i > 0)
    my_putchar(buf[--i]);
}

// Вывести целое число со знаком
void print_int(int n) {
  if (n < 0) {
    my_putchar('-');
    // 0u - n работает и для -2147483648, у которого нет положительной пары в
    // int
    print_uint(0u - (unsigned int)n);
  } else {
    print_uint((unsigned int)n);
  }
}

// Вывести num/den с 5 знаками после запятой (с округлением).
// Вещественных чисел нет, поэтому цифры дробной части получаются
// целочисленным делением «в столбик»: остаток * 10, делим на den, и так далее.
void print_decimal(int negative, unsigned int num, unsigned int den) {
  unsigned int int_part = num / den;
  unsigned int rem = num % den;
  unsigned char digits[6]; // 5 знаков + 1 для округления

  for (int i = 0; i < 6; i++) {
    // Нужно digit = rem * 10 / den и rem = rem * 10 % den, но rem * 10 может
    // не поместиться в 32 бита. Поэтому прибавляем rem десять раз и вычитаем
    // den, как только сумма его достигла: t < den и rem < den, значит
    // t + rem < 2 * den <= 2^32, переполнения нет.
    unsigned int t = 0;
    unsigned char digit = 0;
    for (int k = 0; k < 10; k++) {
      t += rem;
      if (t >= den) {
        t -= den;
        digit++;
      }
    }
    digits[i] = digit;
    rem = t;
  }

  // Округление по шестой цифре с переносом в старшие разряды
  if (digits[5] >= 5) {
    int i = 4;
    while (i >= 0 && digits[i] == 9) {
      digits[i] = 0;
      i--;
    }
    if (i >= 0)
      digits[i]++;
    else
      int_part++;
  }

  // Не выводить «-0.00000»
  int is_zero = (int_part == 0);
  for (int i = 0; i < 5; i++)
    if (digits[i] != 0)
      is_zero = 0;

  if (negative && !is_zero)
    my_putchar('-');
  print_uint(int_part);
  my_putchar('.');
  for (int i = 0; i < 5; i++)
    my_putchar('0' + digits[i]);
}

// ---------------------------------------------------------------------------
// Обработка нажатий клавиш
// ---------------------------------------------------------------------------

void on_key(unsigned char scan_code) {
  unsigned char *video_buf = (unsigned char *)VIDEO_BUF_PTR;

  // Левый (0x2A) и правый (0x36) Shift нажаты / отпущены (+0x80)
  if (scan_code == 0x2A || scan_code == 0x36) {
    flag_shift = 1;
    return;
  }
  if (scan_code == 0xAA || scan_code == 0xB6) {
    flag_shift = 0;
    return;
  }
  // Отпускание остальных клавиш и клавиши за пределами таблиц не нужны
  if (scan_code >= sizeof(symbols))
    return;

  // Backspace: стереть последний введённый символ команды
  if (scan_code == 0x0E) {
    if (count_symbols > 0) {
      cur_col--;
      count_symbols--;
      command[count_symbols] = '\0';
      video_buf[(cur_row * VIDEO_WIDTH + cur_col) * 2] = ' ';
      video_buf[(cur_row * VIDEO_WIDTH + cur_col) * 2 + 1] = cur_color;
      cursor_moveto(cur_row, cur_col);
    }
    return;
  }

  // Enter: выполнить команду и вывести новое приглашение
  if (scan_code == 0x1C) {
    my_putchar('\n');
    if (count_symbols > 0)
      run_command();
    my_puts("> ");
    command[0] = '\0';
    count_symbols = 0;
    return;
  }

  // Обычный символ: дописать в буфер, если не превышен лимит
  unsigned char c = flag_shift ? symbols_shift[scan_code] : symbols[scan_code];
  if (c != 0 && count_symbols < MAX_COMMAND_LEN) {
    my_putchar(c);
    command[count_symbols++] = c;
    command[count_symbols] = '\0';
  }
}

// ---------------------------------------------------------------------------
// Распознавание команды и проверка аргументов
// ---------------------------------------------------------------------------

// Сравнить первое слово в command[] со всеми командами.
// Результат в what_command: номер команды или COMMANDS_COUNT, если не найдена.
void check_command() {
  for (what_command = 0; what_command < COMMANDS_COUNT; what_command++) {
    const char *name = my_commands[what_command];

    int i = 0;
    while (name[i] != '\0' && command[i] == name[i])
      i++;

    if (name[i] == '\0' && (command[i] == ' ' || command[i] == '\0'))
      return;
  }
}

int is_digit(char c) { return c >= '0' && c <= '9'; }

// Пропустить пробелы, вернуть позицию первого непробела
int skip_spaces(int pos) {
  while (command[pos] == ' ')
    pos++;
  return pos;
}

// Позиция сразу после имени команды (там начинаются аргументы)
int args_start() {
  int pos = 0;
  while (command[pos] != ' ' && command[pos] != '\0')
    pos++;
  return pos;
}

// Одно число [-]цифры, после него пробел или конец строки.
// Возвращает позицию после числа или -1.
int check_number(int pos, int allow_minus) {
  if (allow_minus && command[pos] == '-')
    pos++;
  if (!is_digit(command[pos]))
    return -1;
  while (is_digit(command[pos]))
    pos++;
  if (command[pos] != ' ' && command[pos] != '\0')
    return -1;
  return pos;
}

// Ровно два числа через пробел и больше ничего
int check_two_numbers(int pos, int allow_minus) {
  pos = check_number(skip_spaces(pos), allow_minus);
  if (pos < 0)
    return 0;
  pos = check_number(skip_spaces(pos), allow_minus);
  if (pos < 0)
    return 0;
  return command[skip_spaces(pos)] == '\0';
}

// Уравнение ax+b=c: a и b могут отсутствовать (x=5, -x-1=5, 5x=10),
// перед a и c может быть минус, перед b знак + или -
int check_equation(int pos) {
  pos = skip_spaces(pos);

  if (command[pos] == '-') // минус перед a
    pos++;
  while (is_digit(command[pos])) // a (необязательно)
    pos++;

  if (command[pos] != 'x' && command[pos] != 'X')
    return 0;
  pos++;

  if (command[pos] == '+' || command[pos] == '-') { // b (необязательно)
    pos++;
    if (!is_digit(command[pos]))
      return 0;
    while (is_digit(command[pos]))
      pos++;
  }

  if (command[pos] != '=')
    return 0;
  pos++;

  if (command[pos] == '-') // минус перед c
    pos++;
  if (!is_digit(command[pos]))
    return 0;
  while (is_digit(command[pos]))
    pos++;

  return command[skip_spaces(pos)] == '\0'; // после уравнения ничего нет
}

// Возвращает 1, если аргументы команды what_command записаны правильно, иначе 0
int check_args() {
  int pos = args_start();

  switch (what_command) {
  case 0: // info: без аргументов
    return command[skip_spaces(pos)] == '\0';
  case 1: // gcd: два числа без минуса
    return check_two_numbers(pos, 0);
  case 2: // solve: одно уравнение
    return check_equation(pos);
  case 3: // lcm: как gcd
    return check_two_numbers(pos, 0);
  case 4: // div: два целых числа, минус разрешён
    return check_two_numbers(pos, 1);
  case 5: // shutdown: без аргументов
    return command[skip_spaces(pos)] == '\0';
  default:
    return 0;
  }
}

// ---------------------------------------------------------------------------
// Разбор чисел (форма уже проверена check_args)
// ---------------------------------------------------------------------------

// Прочитать целое число (с необязательным минусом) начиная с pos в *value.
// Возвращает позицию после числа или -1, если число не помещается в int.
int parse_int(int pos, int *value) {
  int negative = 0;
  if (command[pos] == '-') {
    negative = 1;
    pos++;
  }
  // У отрицательных чисел предел на единицу больше: -2147483648
  unsigned int limit = negative ? 2147483648u : 2147483647u;
  unsigned int result = 0;

  while (is_digit(command[pos])) {
    unsigned int d = command[pos] - '0';
    // result * 10 + d <= limit  <=>  result <= (limit - d) / 10
    if (result > (limit - d) / 10)
      return -1;
    result = result * 10 + d;
    pos++;
  }

  *value = negative ? (int)(0u - result) : (int)result;
  return pos;
}

// Прочитать два числа после имени команды. 0 при переполнении.
int parse_two_numbers(int *x, int *y) {
  int pos = parse_int(skip_spaces(args_start()), x);
  if (pos < 0)
    return 0;
  pos = parse_int(skip_spaces(pos), y);
  return pos >= 0;
}

// Разобрать уравнение ax+b=c. 0, если какое-то число не помещается в int.
int parse_equation(int *a, int *b, int *c) {
  int pos = skip_spaces(args_start());

  // a: "x" -> 1, "-x" -> -1, иначе число
  if (command[pos] == 'x' || command[pos] == 'X') {
    *a = 1;
  } else if (command[pos] == '-' &&
             (command[pos + 1] == 'x' || command[pos + 1] == 'X')) {
    *a = -1;
    pos++;
  } else {
    pos = parse_int(pos, a);
    if (pos < 0)
      return 0;
  }
  pos++; // пропустить x

  // b: необязательно, со знаком + или -
  *b = 0;
  if (command[pos] == '+')
    pos = parse_int(pos + 1, b);
  else if (command[pos] == '-')
    pos = parse_int(pos, b);
  if (pos < 0)
    return 0;
  pos++; // пропустить '='

  // c
  return parse_int(pos, c) >= 0;
}

// ---------------------------------------------------------------------------
// Математика
// ---------------------------------------------------------------------------

// НОД по алгоритму Евклида
unsigned int gcd(unsigned int a, unsigned int b) {
  while (b != 0) {
    unsigned int t = a % b;
    a = b;
    b = t;
  }
  return a;
}

// Модуль числа как беззнаковое: работает и для -2147483648
unsigned int abs_u(int n) {
  return n < 0 ? 0u - (unsigned int)n : (unsigned int)n;
}

void cmd_gcd() {
  int a, b;
  if (!parse_two_numbers(&a, &b)) {
    my_puts("Error: Integer overflow\n");
    return;
  }
  if (a <= 0 || b <= 0) {
    my_puts("Error: command incorrect\n");
    return;
  }
  my_puts("Result: ");
  print_uint(gcd(a, b));
  my_putchar('\n');
}

void cmd_lcm() {
  int a, b;
  if (!parse_two_numbers(&a, &b)) {
    my_puts("Error: Integer overflow\n");
    return;
  }
  if (a <= 0 || b <= 0) {
    my_puts("Error: command incorrect\n");
    return;
  }
  // НОК = a / НОД * b. Сначала делим, чтобы не переполниться раньше времени,
  // и проверяем, что произведение помещается в int.
  unsigned int q = (unsigned int)a / gcd(a, b);
  if (q > 2147483647u / (unsigned int)b) {
    my_puts("Error: Integer overflow\n");
    return;
  }
  my_puts("Result: ");
  print_uint(q * (unsigned int)b);
  my_putchar('\n');
}

void cmd_div() {
  int a, b;
  if (!parse_two_numbers(&a, &b)) {
    my_puts("Error: Integer overflow\n");
    return;
  }
  if (b == 0) {
    my_puts("Error: division by 0\n");
    return;
  }
  my_puts("Result: ");
  print_decimal((a < 0) != (b < 0), abs_u(a), abs_u(b));
  my_putchar('\n');
}

void cmd_solve() {
  int a, b, c;
  if (!parse_equation(&a, &b, &c)) {
    my_puts("Error: Integer overflow\n");
    return;
  }

  // ax + b = c  =>  ax = c - b. Проверяем, что c - b помещается в int.
  if ((b > 0 && c < -2147483647 - 1 + b) || (b < 0 && c > 2147483647 + b)) {
    my_puts("Error: Integer overflow\n");
    return;
  }
  int num = c - b;

  if (a == 0) {
    if (num == 0)
      my_puts("Result: x is any number\n");
    else
      my_puts("Error: no solution\n");
    return;
  }

  int negative = (num < 0) != (a < 0) && num != 0;
  unsigned int n = abs_u(num);
  unsigned int d = abs_u(a);

  my_puts("Result: x=");
  if (n % d == 0) {
    // Целое решение
    if (negative)
      my_putchar('-');
    print_uint(n / d);
  } else {
    // Вещественное решение: 5 знаков после запятой (доп. задание)
    print_decimal(negative, n, d);
  }
  my_putchar('\n');
}

// ---------------------------------------------------------------------------
// Выполнение команды по Enter
// ---------------------------------------------------------------------------

void run_command() {
  check_command();
  if (what_command == COMMANDS_COUNT) {
    my_puts("Error: command not recognized\n");
    return;
  }
  if (!check_args()) {
    my_puts("Error: command incorrect\n");
    return;
  }

  switch (what_command) {
  case 0: // info
    my_puts("Curly OS: v.01. Developer: Antonov Georgiy, 5151001/40202, SpbPU, "
            "2026\n");
    my_puts("Compilers: bootloader: yasm, kernel: gcc\n");
    my_puts("Bootloader parameters: ");
    my_puts(colors[color_index]);
    my_puts(" color.\n");
    break;
  case 1: // gcd
    cmd_gcd();
    break;
  case 2: // solve
    cmd_solve();
    break;
  case 3: // lcm
    cmd_lcm();
    break;
  case 4: // div
    cmd_div();
    break;
  case 5: // shutdown
    my_puts("Powering off...\n");
    outw(0x604, 0x2000);
    break;
  }
}

extern "C" int kmain() {
  // Номер цвета, сохранённый загрузчиком по адресу 0x8000
  color_index = *(unsigned char *)0x8000;
  if (color_index >= sizeof(colors_cods))
    color_index = 0;
  cur_color = colors_cods[color_index];

  clear_screen();
  my_puts("Welcome to CurlyOS!\n");
  my_puts("> ");

  intr_init();
  keyb_init();
  intr_start();
  intr_enable();

  while (1)
    asm("hlt");

  return 0;
}
