#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

/* Заголовок файла редактора — хранится в начале файла */
struct editor_header
{
  int max_lines;        /* Максимальное количество строк */
  int max_line_length;  /* Максимальная длина строки */
  int line_count;       /* Текущее количество строк */
};

/* Статический буфер для строк — максимальная длина строки 256 байт */
#define MAX_LINE_LEN 256
static char line_buf[MAX_LINE_LEN + 1];
static char tmp_buf[MAX_LINE_LEN + 1];

static int
line_size (int max_line_length)
{
  return max_line_length + 1;
}

static int
line_offset (int line_number, int max_line_length)
{
  return sizeof (struct editor_header)
         + (line_number - 1) * line_size (max_line_length);
}

static void
print_help (void)
{
  printf ("editor - simple line-based file editor\n");
  printf ("Commands:\n");
  printf ("  editor help\n");
  printf ("  editor create [filename] [max_lines] [max_line_length]\n");
  printf ("  editor info [filename]\n");
  printf ("  editor lines [filename]\n");
  printf ("  editor read [filename] [line_number]\n");
  printf ("  editor del [filename] [line_number]\n");
  printf ("  editor ins [filename] [line_number] [text]\n");
}

static int
open_and_read_header (const char *filename, struct editor_header *hdr)
{
  int fd = open (filename);
  if (fd < 0)
    {
      printf ("editor: cannot open '%s'\n", filename);
      return -1;
    }
  if (read (fd, hdr, sizeof *hdr) != (int) sizeof *hdr)
    {
      printf ("editor: cannot read header of '%s'\n", filename);
      close (fd);
      return -1;
    }
  return fd;
}

static void
cmd_create (const char *filename, int max_lines, int max_line_length)
{
  int i;

  if (max_lines <= 0 || max_line_length <= 0
      || max_line_length > MAX_LINE_LEN)
    {
      printf ("editor: invalid parameters (max_line_length max %d)\n",
              MAX_LINE_LEN);
      return;
    }

  int file_size = sizeof (struct editor_header)
                  + max_lines * line_size (max_line_length);

  if (!create (filename, file_size))
    {
      printf ("editor: cannot create '%s'\n", filename);
      return;
    }

  int fd = open (filename);
  if (fd < 0)
    {
      printf ("editor: cannot open '%s'\n", filename);
      return;
    }

  struct editor_header hdr;
  hdr.max_lines = max_lines;
  hdr.max_line_length = max_line_length;
  hdr.line_count = 0;

  write (fd, &hdr, sizeof hdr);

  /* Заполняем строки нулями по одной */
  int lsize = line_size (max_line_length);
  memset (line_buf, 0, lsize);
  for (i = 0; i < max_lines; i++)
    write (fd, line_buf, lsize);

  close (fd);
  printf ("editor: created '%s' (max_lines=%d, max_line_length=%d)\n",
          filename, max_lines, max_line_length);
}

static void
cmd_info (const char *filename)
{
  struct editor_header hdr;
  int fd = open_and_read_header (filename, &hdr);
  if (fd < 0)
    return;
  close (fd);

  printf ("File: %s\n", filename);
  printf ("Max lines: %d\n", hdr.max_lines);
  printf ("Max line length: %d\n", hdr.max_line_length);
  printf ("Current lines: %d\n", hdr.line_count);
}

static void
cmd_lines (const char *filename)
{
  struct editor_header hdr;
  int fd = open_and_read_header (filename, &hdr);
  if (fd < 0)
    return;
  close (fd);
  printf ("%d\n", hdr.line_count);
}

static void
cmd_read (const char *filename, int line_number)
{
  struct editor_header hdr;
  int fd = open_and_read_header (filename, &hdr);
  if (fd < 0)
    return;

  if (line_number < 1 || line_number > hdr.line_count)
    {
      printf ("editor: line %d out of range (1..%d)\n",
              line_number, hdr.line_count);
      close (fd);
      return;
    }

  int lsize = line_size (hdr.max_line_length);
  seek (fd, line_offset (line_number, hdr.max_line_length));
  read (fd, line_buf, lsize);
  line_buf[hdr.max_line_length] = '\0';
  printf ("%s\n", line_buf);
  close (fd);
}

static void
cmd_del (const char *filename, int line_number)
{
  struct editor_header hdr;
  int fd = open_and_read_header (filename, &hdr);
  if (fd < 0)
    return;

  if (line_number < 1 || line_number > hdr.line_count)
    {
      printf ("editor: line %d out of range (1..%d)\n",
              line_number, hdr.line_count);
      close (fd);
      return;
    }

  int lsize = line_size (hdr.max_line_length);
  int i;

  /* Сдвигаем строки вверх */
  for (i = line_number; i < hdr.line_count; i++)
    {
      seek (fd, line_offset (i + 1, hdr.max_line_length));
      read (fd, tmp_buf, lsize);
      seek (fd, line_offset (i, hdr.max_line_length));
      write (fd, tmp_buf, lsize);
    }

  /* Затираем последнюю строку */
  memset (line_buf, 0, lsize);
  seek (fd, line_offset (hdr.line_count, hdr.max_line_length));
  write (fd, line_buf, lsize);

  /* Обновляем заголовок */
  hdr.line_count--;
  seek (fd, 0);
  write (fd, &hdr, sizeof hdr);

  close (fd);
  printf ("editor: deleted line %d\n", line_number);
}

static void
cmd_ins (const char *filename, int line_number, const char *text)
{
  struct editor_header hdr;
  int fd = open_and_read_header (filename, &hdr);
  if (fd < 0)
    return;

  if ((int) strlen (text) > hdr.max_line_length)
    {
      printf ("editor: text too long (max %d chars)\n", hdr.max_line_length);
      close (fd);
      return;
    }

  if (line_number < 1 || line_number > hdr.line_count + 1)
    {
      printf ("editor: line %d out of range (1..%d)\n",
              line_number, hdr.line_count + 1);
      close (fd);
      return;
    }

  if (hdr.line_count >= hdr.max_lines)
    {
      printf ("editor: file is full (%d lines max)\n", hdr.max_lines);
      close (fd);
      return;
    }

  int lsize = line_size (hdr.max_line_length);
  int i;

  /* Сдвигаем строки вниз */
  for (i = hdr.line_count; i >= line_number; i--)
    {
      seek (fd, line_offset (i, hdr.max_line_length));
      read (fd, tmp_buf, lsize);
      seek (fd, line_offset (i + 1, hdr.max_line_length));
      write (fd, tmp_buf, lsize);
    }

  /* Записываем новую строку */
  memset (line_buf, 0, lsize);
  strlcpy (line_buf, text, hdr.max_line_length + 1);
  seek (fd, line_offset (line_number, hdr.max_line_length));
  write (fd, line_buf, lsize);

  /* Обновляем заголовок */
  hdr.line_count++;
  seek (fd, 0);
  write (fd, &hdr, sizeof hdr);

  close (fd);
  printf ("editor: inserted at line %d\n", line_number);
}

int
main (int argc, const char *argv[])
{
  if (argc < 2)
    {
      print_help ();
      return EXIT_FAILURE;
    }

  const char *cmd = argv[1];

  if (strcmp (cmd, "help") == 0)
    {
      print_help ();
    }
  else if (strcmp (cmd, "create") == 0)
    {
      if (argc != 5)
        {
          printf ("Usage: editor create [filename] [max_lines] [max_line_length]\n");
          return EXIT_FAILURE;
        }
      cmd_create (argv[2], atoi (argv[3]), atoi (argv[4]));
    }
  else if (strcmp (cmd, "info") == 0)
    {
      if (argc != 3)
        {
          printf ("Usage: editor info [filename]\n");
          return EXIT_FAILURE;
        }
      cmd_info (argv[2]);
    }
  else if (strcmp (cmd, "lines") == 0)
    {
      if (argc != 3)
        {
          printf ("Usage: editor lines [filename]\n");
          return EXIT_FAILURE;
        }
      cmd_lines (argv[2]);
    }
  else if (strcmp (cmd, "read") == 0)
    {
      if (argc != 4)
        {
          printf ("Usage: editor read [filename] [line_number]\n");
          return EXIT_FAILURE;
        }
      cmd_read (argv[2], atoi (argv[3]));
    }
  else if (strcmp (cmd, "del") == 0)
    {
      if (argc != 4)
        {
          printf ("Usage: editor del [filename] [line_number]\n");
          return EXIT_FAILURE;
        }
      cmd_del (argv[2], atoi (argv[3]));
    }
  else if (strcmp (cmd, "ins") == 0)
    {
      if (argc != 5)
        {
          printf ("Usage: editor ins [filename] [line_number] [text]\n");
          return EXIT_FAILURE;
        }
      cmd_ins (argv[2], atoi (argv[3]), argv[4]);
    }
  else
    {
      printf ("editor: unknown command '%s'\n", cmd);
      print_help ();
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
