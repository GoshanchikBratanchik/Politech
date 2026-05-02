#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Таблица для хранения кодов завершения процессов */
#define MAX_PROCESSES 128
static struct { tid_t tid; tid_t parent_tid; int status; bool used; } exit_table[MAX_PROCESSES];
static struct lock exit_table_lock;
static bool exit_table_inited = false;

/* Структура для передачи информации между родительским и дочерним процессами.
   Используется для синхронизации запуска и передачи статуса загрузки. */
struct process_info
{
  char *cmdline;          /* Полная командная строка */
  struct semaphore loaded; /* Семафор: дочерний сигналит после load() */
  bool success;           /* Результат load() */
  struct thread *child;   /* Указатель на дочерний поток */
  tid_t parent_tid; 
};

/* Запускает новый поток для пользовательской программы из FILENAME.
   Возвращает tid нового процесса или TID_ERROR при ошибке.
   Ждёт завершения load() дочернего процесса перед возвратом. */
tid_t
process_execute (const char *file_name)
{
  struct process_info *info;
  tid_t tid;

  if (!exit_table_inited) {
    lock_init (&exit_table_lock);
    exit_table_inited = true;
  }

  /* Выделяем структуру синхронизации */
  info = palloc_get_page (0);
  if (info == NULL)
    return TID_ERROR;

  /* Копируем командную строку */
  info->cmdline = palloc_get_page (0);
  if (info->cmdline == NULL)
    {
      palloc_free_page (info);
      return TID_ERROR;
    }
  strlcpy (info->cmdline, file_name, PGSIZE);

  info->parent_tid = thread_current ()->tid;
  sema_init (&info->loaded, 0);
  info->success = false;
  info->child = NULL;

  /* Извлекаем только имя файла для имени потока */
  char *name_copy = palloc_get_page (0);
  if (name_copy == NULL)
    {
      palloc_free_page (info->cmdline);
      palloc_free_page (info);
      return TID_ERROR;
    }
  strlcpy (name_copy, file_name, PGSIZE);
  char *save_ptr;
  char *thread_name = strtok_r (name_copy, " ", &save_ptr);

  /* Создаём дочерний поток */
  tid = thread_create (thread_name, PRI_DEFAULT, start_process, info);
  palloc_free_page (name_copy);

  if (tid == TID_ERROR)
    {
      palloc_free_page (info->cmdline);
      palloc_free_page (info);
      return TID_ERROR;
    }

  /* Ждём пока дочерний процесс завершит load() */
  sema_down (&info->loaded);

  if (!info->success)
    tid = TID_ERROR;

  /* Сохраняем tid дочернего потока в структуре текущего потока
     для последующего ожидания в process_wait */
  if (info->success && info->child != NULL)
    thread_current ()->child_tid = info->child->tid;

  palloc_free_page (info->cmdline);
  palloc_free_page (info);

  return tid;
}

/* Функция потока — загружает пользовательскую программу и запускает её. */
static void
start_process (void *info_)
{
  struct process_info *info = info_;
  char *file_name = info->cmdline;
  struct intr_frame if_;
  bool success;

  /* Инициализируем фрейм прерывания */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  /* Загружаем ELF и настраиваем стек с аргументами */
  success = load (file_name, &if_.eip, &if_.esp);

  /* Сообщаем родителю о результате загрузки */
  info->success = success;
  info->child = thread_current ();
  thread_current ()->parent_tid = info->parent_tid;
  sema_up (&info->loaded);

  if (!success)
    {
      thread_current ()->exit_status = -1;
      thread_exit ();
    }
  /* Запускаем пользовательский процесс через возврат из прерывания */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Ожидает завершения процесса с идентификатором CHILD_TID.
   Возвращает код выхода программы или -1 при ошибке.
   Реализовано через семафор ожидания в структуре дочернего потока. */
int
process_wait (tid_t child_tid)
{
  if (child_tid == TID_ERROR)
    return -1;

  tid_t my_tid = thread_current ()->tid;

  /* Сначала ищем живой дочерний поток */
  struct thread *child = thread_get_by_tid (child_tid);
  if (child != NULL)
    {
      /* Проверяем что это наш дочерний процесс */
      if (child->parent_tid != my_tid)
        return -1;

      /* Ждём завершения */
      sema_down (&child->wait_sema);
    }

  /* Ищем в таблице завершённых процессов */
  int status = -1;
  lock_acquire (&exit_table_lock);
  for (int i = 0; i < MAX_PROCESSES; i++)
    if (exit_table[i].used
        && exit_table[i].tid == child_tid
        && exit_table[i].parent_tid == my_tid)
      {
        status = exit_table[i].status;
        exit_table[i].used = false;
        break;
      }
  lock_release (&exit_table_lock);
  return status;
}

/* Освобождает ресурсы текущего процесса и выводит диагностику. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  if (cur->pagedir != NULL)
    {
      printf ("%s: exit(%d)\n", cur->name, cur->exit_status);

      /* Сохраняем код завершения в таблицу ДО освобождения памяти */
      lock_acquire (&exit_table_lock);
      for (int i = 0; i < MAX_PROCESSES; i++)
        if (!exit_table[i].used)
          {
            exit_table[i].tid = cur->tid;
            exit_table[i].parent_tid = cur->parent_tid;
            exit_table[i].status = cur->exit_status;
            exit_table[i].used = true;
            break;
          }
      lock_release (&exit_table_lock);
      syscall_close_all_files ();
    }

  pd = cur->pagedir;
  if (pd != NULL)
    {
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}
/* Настраивает CPU для выполнения пользовательского кода. */
void
process_activate (void)
{
  struct thread *t = thread_current ();
  pagedir_activate (t->pagedir);
  tss_update ();
}

/* ==================== ELF загрузчик ==================== */

typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

#define PE32Wx PRIx32
#define PE32Ax PRIx32
#define PE32Ox PRIx32
#define PE32Hx PRIx16

struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_STACK   0x6474e551

#define PF_X 1
#define PF_W 2
#define PF_R 4

static bool setup_stack (void **esp, const char *file_name);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Загружает ELF-исполняемый файл из FILE_NAME в текущий поток.
   Разбирает аргументы командной строки и размещает их в стеке. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Создаём страничный каталог */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Извлекаем только имя исполняемого файла из командной строки */
  char *fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    goto done;
  strlcpy (fn_copy, file_name, PGSIZE);
  char *save_ptr;
  char *exec_name = strtok_r (fn_copy, " ", &save_ptr);

  /* Открываем исполняемый файл */
  file = filesys_open (exec_name);
  palloc_free_page (fn_copy);

  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  /* Читаем и проверяем ELF-заголовок */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Читаем заголовки программных сегментов */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Настраиваем стек с аргументами командной строки */
  if (!setup_stack (esp, file_name))
    goto done;

  /* Устанавливаем точку входа */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

done:
  file_close (file);
  return success;
}

static bool install_page (void *upage, void *kpage, bool writable);

static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;
  if (phdr->p_memsz < phdr->p_filesz)
    return false;
  if (phdr->p_memsz == 0)
    return false;
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;
  if (phdr->p_vaddr < PGSIZE)
    return false;
  return true;
}

static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Создаёт начальный стек пользовательского процесса и размещает
   в нём аргументы командной строки по соглашению x86.

   Схема стека (адреса убывают вниз):
     PHYS_BASE
     строки аргументов (argv[0], argv[1], ...)
     выравнивание до 4 байт
     argv[argc] = NULL  (sentinel)
     argv[argc-1] ... argv[0]  (указатели на строки)
     argv  (указатель на argv[0])
     argc  (количество аргументов)
     return address = 0  (фиктивный)
*/
static bool
setup_stack (void **esp, const char *file_name)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage == NULL)
    return false;

  success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
  if (!success)
    {
      palloc_free_page (kpage);
      return false;
    }

  /* Копируем командную строку для токенизации */
  char *args = palloc_get_page (0);
  if (args == NULL)
    return false;
  strlcpy (args, file_name, PGSIZE);

  /* Первый проход: считаем количество аргументов */
  int argc = 0;
  {
    char *tmp = palloc_get_page (0);
    if (tmp == NULL) { palloc_free_page (args); return false; }
    strlcpy (tmp, args, PGSIZE);
    char *tok, *sp;
    for (tok = strtok_r (tmp, " ", &sp); tok != NULL;
         tok = strtok_r (NULL, " ", &sp))
      argc++;
    palloc_free_page (tmp);
  }

  if (argc == 0) { palloc_free_page (args); return false; }

  /* Массив для хранения указателей на строки в стеке пользователя */
  char **argv_addrs = palloc_get_page (0);
  if (argv_addrs == NULL) { palloc_free_page (args); return false; }

  *esp = PHYS_BASE;

  /* Шаг 1: копируем строки аргументов в стек (от argv[0] до argv[argc-1]) */
  char *tok, *sp;
  int i = 0;
  for (tok = strtok_r (args, " ", &sp); tok != NULL;
       tok = strtok_r (NULL, " ", &sp))
    {
      size_t len = strlen (tok) + 1;     /* +1 для '\0' */
      *esp = (char *) *esp - len;
      memcpy (*esp, tok, len);
      argv_addrs[i++] = (char *) *esp;
    }

  /* Шаг 2: выравниваем esp до кратного 4 байтам */
  while ((uintptr_t) *esp % 4 != 0)
    {
      *esp = (char *) *esp - 1;
      *(uint8_t *) *esp = 0;
    }

  /* Шаг 3: помещаем sentinel — argv[argc] = NULL */
  *esp = (char **) *esp - 1;
  *(char **) *esp = NULL;

  /* Шаг 4: помещаем указатели argv[argc-1]..argv[0] в обратном порядке */
  for (i = argc - 1; i >= 0; i--)
    {
      *esp = (char **) *esp - 1;
      *(char **) *esp = argv_addrs[i];
    }

  /* Шаг 5: помещаем argv — указатель на argv[0] */
  char **argv_ptr = (char **) *esp;
  *esp = (char ***) *esp - 1;
  *(char ***) *esp = argv_ptr;

  /* Шаг 6: помещаем argc */
  *esp = (int *) *esp - 1;
  *(int *) *esp = argc;

  /* Шаг 7: фиктивный адрес возврата */
  *esp = (void **) *esp - 1;
  *(void **) *esp = NULL;

  palloc_free_page (args);
  palloc_free_page (argv_addrs);

  return true;
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
