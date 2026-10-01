__asm("jmp kmain");

#define VIDEO_BUF_PTR (0xb8000)

void out_str(int color, const char *ptr, unsigned int strnum) {
  unsigned char *video_buf = (unsigned char *)VIDEO_BUF_PTR;
  video_buf += 80 * 2 * strnum;

  while (*ptr) {
    video_buf[0] = (unsigned char)*ptr;
    video_buf[1] = color;
    video_buf += 2;
    ptr++;
  }
}

const char *g_test = "i love ibks";

extern "C" int kmain() {
  const char *hello = "Welcome to CurlyHackOS!";
  out_str(0x07, hello, 0);
  out_str(0x07, g_test, 1);

  while (1) {
    asm("hlt");
  }

  return 0;
}
