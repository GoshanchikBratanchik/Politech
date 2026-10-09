__asm("jmp kmain");

#define VIDEO_BUF_PTR 0xb8000
#define VIDEO_WIDTH 80
#define VIDEO_HEIGHT 25
#define CURSOR_PORT 0X3D4
#define IDT_TYPE_INTR 0x0E
#define IDT_TYPE_TRAP 0x0F
#define GDT_CS 0x08
#define PIC1_PORT 0x20

unsigned int cur_row = 0;
unsigned int cur_col = 0;
unsigned char cur_color = 0x07;

struct idt_entry {
  unsigned short base_lo;
  unsigned short base_hi;
  unsigned short segm_sel;
  unsigned char always0;
  unsigned char flags;
} __attribute__((packed));

struct idt_ptr {
  unsigned short limit;
  unsigned int base;
} __attribute__((packed));

struct idt_entry g_idt[256];
struct idt_ptr g_idtp;

void default_intr_handler() {
  asm("pusha");

  asm("popa; leave; iret");
}

typedef void (*intr_handler)();
void intr_reg_handler(int num, unsigned short segm_sel, unsigned short flags,
                      intr_handler hndlr) {
  unsigned int hndlr_addr = (unsigned int)hndlr;

  g_idt[num].base_lo = (unsigned short)(hndlr_addr & 0xFFFF);
  g_idt[num].segm_sel = segm_sel;
  g_idt[num].always0 = 0;
  g_idt[num].flags = flags;
  g_idt[num].base_hi = (unsigned short)(hndlr_addr >> 16);
}

void intr_init() {
  int idt_count = sizeof(g_idt) / sizeof(g_idt[0]);

  for (int i = 0; i < idt_count; i++) {
    intr_reg_handler(i, GDT_CS, 0x80 | IDT_TYPE_INTR, default_intr_handler());
  }
}

void intr_start() {
  int idt_count = sizeof(g_idt) / sizeof(g_idt[0]);

  g_idtp.base = (unsigned int)(&g_idt[0]);
  g_idtp.limit = (sizeof(struct idt_entry) * idt_count) - 1;

  asm("lidt %0" : : "m"(g_idtp));
}

void intr_enable() { asm("sti"); }

void intr_disable() { asm("cli"); }

static inline unsigned char inb(unsigned short port) {
  unsigned char data;
  asm volatile("inb %w1, %b0" : "=a"(data) : "Nd"(port));
  return data;
}

static inline void outb(unsigned short port, unsigned char data) {
  asm volatile("outb %b0, %w1" : : "a"(data), "Nd"(port));
}

void keyb_process_keys() {
  if (inb(0x64) & 0x01) {
    unsigned char scan_code;
    unsigned char state;
    scan_code = inb(0x60);

    if (scan_code < 128)
      on_key(scan_code);
  }
}

void keyb_handler() {
  asm("pusha");
  keyb_process_keys();
  outb(PIC1_PORT, 0x20);
  asm("popa; leave; iret");
}

void keyb_init() {
  intr_reg_handler(0x09, GDT_CS, 0x80 | IDT_TYPE_INTR, keyb_handler());
  outb(PIC1_PORT + 1, 0xFF ^ 0x02);
}

void cursor_moveto(unsigned int strnum, unsigned int pos) {
  unsigned short new_pos = (strnum * VIDEO_WIDTH) + pos;
  outb(CURSOR_PORT, 0x0F);
  outb(CURSOR_PORT + 1, (unsigned char)(new_pos & 0xFF));
  outb(CURSOR_PORT, 0x0E);
  outb(CURSOR_PORT + 1, (unsigned)(new_pos >> 8) & 0xFF);
}

void clear_line(int strnum) {
  unsigned char *video_buf = (unsigned char *)VIDEO_BUF_PTR;
  for (int i = 0; i < VIDEO_WIDTH; i++) {
    video_buf[(strnum * VIDEO_WIDTH + i) * 2] = ' ';
    video_buf[(strnum * VIDEO_WIDTH + i) * 2 + 1] = cur_color;
  }
  cur_col = 0;
  cur_row = strnum;
}

void clear_screen() {
  unsigned char *video_buf = (unsigned char *)VIDEO_BUF_PTR;
  for (int i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT; i++) {
    video_buf[i * 2] = ' ';
    video_buf[i * 2 + 1] = cur_color;
  }
  cur_row = 0;
  cur_col = 0;
}

void scroll() {
  unsigned short *video_buf = (unsigned short *)VIDEO_BUF_PTR;
  for (int i = 80; i < VIDEO_WIDTH * VIDEO_HEIGHT; i++) {
    video_buf[i - 80] = video_buf[i];
  }
  clear_line(24);
}

void newline() {
  cur_col = 0;
  cur_row += 1;
  if (cur_row == VIDEO_HEIGHT)
    scroll();
}

void putchar(char c) {
  unsigned char *video_buf = (unsigned char *)VIDEO_BUF_PTR;
  if (c == '\n') {
    newline();
    return;
  }
  video_buf[(cur_row * VIDEO_WIDTH + cur_col) * 2] = c;
  video_buf[((cur_row * VIDEO_WIDTH + cur_col) * 2) + 1] = cur_color;
  cur_col += 1;
  if (cur_col == VIDEO_WIDTH)
    newline();
  cursor_moveto(cur_row, cur_col);
}

void puts(const char *s) {
  for (int i = 0; s[i] != '\0'; i++) {
    putchar(s[i]);
  }
}

void out_str(int color, const char *ptr, unsigned int strnum) {
  unsigned char *video_buf = (unsigned char *)VIDEO_BUF_PTR;
  video_buf += VIDEO_WIDTH * 2 * strnum;
  while (*ptr) {
    video_buf[0] = (unsigned char)*ptr;
    video_buf[1] = color;
    video_buf += 2;
    ptr++;
  }
}

extern "C" int kmain() {
  unsigned int strnum = 9;
  const char *hello = "Welcome to CurlyHackOS!\n";

  const char *colors[] = {"gray",  "cyan",   "white",
                          "green", "yellow", "magenta"};
  unsigned char colors_cods[] = {0x07, 0x0B, 0x0F, 0x0A, 0x0E, 0x0D};

  unsigned char color_index = *(unsigned char *)0x8000;
  cur_color = colors_cods[color_index];
  clear_screen();
  for (int i = 0; i < 30; i++) {
    putchar('A' + i % 26);
    putchar('\n');
  }
  while (1) {
    asm("hlt");
  }

  return 0;
}
