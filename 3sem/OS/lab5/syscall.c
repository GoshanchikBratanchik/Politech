#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);

/* Глобальный лок для файловой системы — Pintos FS не потокобезопасна */
static struct lock filesys_lock;

/* Структура для хранения открытого файла процесса */
struct file_descriptor
{
  int fd;                  /* Номер дескриптора */
  struct file *file;       /* Указатель на открытый файл */
  struct list_elem elem;   /* Элемент списка */
};

/* ================================================================
   Вспомогательные функции проверки указателей
   ================================================================ */

/* Проверяет что указатель действителен — не NULL, в user space,
   и страница выделена. Завершает процесс если указатель плохой. */
static void
validate_ptr (const void *ptr)
{
  if (ptr == NULL
      || !is_user_vaddr (ptr)
      || pagedir_get_page (thread_current ()->pagedir, ptr) == NULL)
    {
      thread_current ()->exit_status = -1;
      thread_exit ();
    }
}

/* Проверяет строку — каждый байт до '\0' должен быть в user space */
static void
validate_str (const char *str)
{
  validate_ptr (str);
  while (*str != '\0')
    {
      str++;
      validate_ptr (str);
    }
}

/* Проверяет буфер заданного размера */
static void
validate_buffer (const void *buf, unsigned size)
{
  const char *p = (const char *) buf;
  unsigned i;
  for (i = 0; i < size; i++)
    validate_ptr (p + i);
}

/* Читает 4-байтовое значение со стека пользователя по байтовому смещению */
static uint32_t
read_stack (struct intr_frame *f, int byte_offset)
{
  uint8_t *ptr = (uint8_t *) f->esp + byte_offset;
  validate_ptr (ptr);
  validate_ptr (ptr + 3);
  return *(uint32_t *) ptr;
}

/* ================================================================
   Работа с файловыми дескрипторами
   ================================================================ */

/* Возвращает file_descriptor по номеру fd для текущего процесса,
   или NULL если не найден */
static struct file_descriptor *
get_file_descriptor (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->fd_list); e != list_end (&t->fd_list);
       e = list_next (e))
    {
      struct file_descriptor *fdesc =
        list_entry (e, struct file_descriptor, elem);
      if (fdesc->fd == fd)
        return fdesc;
    }
  return NULL;
}

/* ================================================================
   Инициализация
   ================================================================ */

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* ================================================================
   Закрытие всех файлов процесса при завершении
   Вызывается из process_exit
   ================================================================ */

void
syscall_close_all_files (void)
{
  struct thread *t = thread_current ();
  struct list_elem *e;

  while (!list_empty (&t->fd_list))
    {
      e = list_begin (&t->fd_list);
      struct file_descriptor *fdesc =
        list_entry (e, struct file_descriptor, elem);
      list_remove (e);
      lock_acquire (&filesys_lock);
      file_close (fdesc->file);
      lock_release (&filesys_lock);
      free (fdesc);
    }
}

/* ================================================================
   Обработчик системных вызовов
   ================================================================ */

static void
syscall_handler (struct intr_frame *f)
{
  uint32_t syscall_num = read_stack (f, 0);

  switch (syscall_num)
    {
    /* ---- HALT ---- */
    case SYS_HALT:
      shutdown_power_off ();
      break;

    /* ---- EXIT ---- */
    case SYS_EXIT:
      {
        int status = (int) read_stack (f, 4);
        thread_current ()->exit_status = status;
        thread_exit ();
      }
      break;

    /* ---- EXEC ---- */
    case SYS_EXEC:
      {
        const char *cmd_line = (const char *) read_stack (f, 4);
        validate_str (cmd_line);
        lock_acquire (&filesys_lock);
        tid_t tid = process_execute (cmd_line);
        lock_release (&filesys_lock);
        f->eax = (tid == TID_ERROR) ? -1 : tid;
      }
      break;

    /* ---- WAIT ---- */
    case SYS_WAIT:
      {
        tid_t tid = (tid_t) read_stack (f, 4);
        f->eax = process_wait (tid);
      }
      break;

    /* ---- CREATE ---- */
    case SYS_CREATE:
      {
        const char *file = (const char *) read_stack (f, 4);
        unsigned initial_size = (unsigned) read_stack (f, 8);
        validate_str (file);
        lock_acquire (&filesys_lock);
        bool success = filesys_create (file, initial_size);
        lock_release (&filesys_lock);
        f->eax = success;
      }
      break;

    /* ---- REMOVE ---- */
    case SYS_REMOVE:
      {
        const char *file = (const char *) read_stack (f, 4);
        validate_str (file);
        lock_acquire (&filesys_lock);
        bool success = filesys_remove (file);
        lock_release (&filesys_lock);
        f->eax = success;
      }
      break;

    /* ---- OPEN ---- */
    case SYS_OPEN:
      {
        const char *filename = (const char *) read_stack (f, 4);
        validate_str (filename);

        lock_acquire (&filesys_lock);
        struct file *file = filesys_open (filename);
        lock_release (&filesys_lock);

        if (file == NULL)
          {
            f->eax = -1;
            break;
          }

        /* Создаём дескриптор и добавляем в список процесса */
        struct file_descriptor *fdesc = malloc (sizeof *fdesc);
        if (fdesc == NULL)
          {
            lock_acquire (&filesys_lock);
            file_close (file);
            lock_release (&filesys_lock);
            f->eax = -1;
            break;
          }

        struct thread *t = thread_current ();
        fdesc->fd = t->next_fd++;
        fdesc->file = file;
        list_push_back (&t->fd_list, &fdesc->elem);
        f->eax = fdesc->fd;
      }
      break;

    /* ---- FILESIZE ---- */
    case SYS_FILESIZE:
      {
        int fd = (int) read_stack (f, 4);
        struct file_descriptor *fdesc = get_file_descriptor (fd);
        if (fdesc == NULL)
          {
            f->eax = -1;
            break;
          }
        lock_acquire (&filesys_lock);
        f->eax = file_length (fdesc->file);
        lock_release (&filesys_lock);
      }
      break;

    /* ---- READ ---- */
    case SYS_READ:
      {
        int fd        = (int)      read_stack (f, 4);
        void *buffer  = (void *)   read_stack (f, 8);
        unsigned size = (unsigned) read_stack (f, 12);
        validate_buffer (buffer, size);

        if (fd == 0)
          {
            /* Чтение с клавиатуры */
            unsigned i;
            char *buf = (char *) buffer;
            for (i = 0; i < size; i++)
              buf[i] = input_getc ();
            f->eax = size;
          }
        else if (fd == 1)
          {
            /* Нельзя читать из stdout */
            f->eax = -1;
          }
        else
          {
            struct file_descriptor *fdesc = get_file_descriptor (fd);
            if (fdesc == NULL)
              {
                f->eax = -1;
                break;
              }
            lock_acquire (&filesys_lock);
            f->eax = file_read (fdesc->file, buffer, size);
            lock_release (&filesys_lock);
          }
      }
      break;

    /* ---- WRITE ---- */
    case SYS_WRITE:
      {
        int fd        = (int)      read_stack (f, 4);
        void *buffer  = (void *)   read_stack (f, 8);
        unsigned size = (unsigned) read_stack (f, 12);
        validate_buffer (buffer, size);

        if (fd == 1)
          {
            /* Вывод в консоль */
            putbuf ((const char *) buffer, size);
            f->eax = size;
          }
        else if (fd == 0)
          {
            /* Нельзя писать в stdin */
            f->eax = -1;
          }
        else
          {
            struct file_descriptor *fdesc = get_file_descriptor (fd);
            if (fdesc == NULL)
              {
                f->eax = -1;
                break;
              }
            lock_acquire (&filesys_lock);
            f->eax = file_write (fdesc->file, buffer, size);
            lock_release (&filesys_lock);
          }
      }
      break;

    /* ---- SEEK ---- */
    case SYS_SEEK:
      {
        int fd            = (int)      read_stack (f, 4);
        unsigned position = (unsigned) read_stack (f, 8);
        struct file_descriptor *fdesc = get_file_descriptor (fd);
        if (fdesc != NULL)
          {
            lock_acquire (&filesys_lock);
            file_seek (fdesc->file, position);
            lock_release (&filesys_lock);
          }
      }
      break;

    /* ---- TELL ---- */
    case SYS_TELL:
      {
        int fd = (int) read_stack (f, 4);
        struct file_descriptor *fdesc = get_file_descriptor (fd);
        if (fdesc == NULL)
          {
            f->eax = -1;
            break;
          }
        lock_acquire (&filesys_lock);
        f->eax = file_tell (fdesc->file);
        lock_release (&filesys_lock);
      }
      break;

    /* ---- CLOSE ---- */
    case SYS_CLOSE:
      {
        int fd = (int) read_stack (f, 4);

        /* Попытка закрыть stdout — завершаем с ошибкой */
        if (fd == 1)
          {
            thread_current ()->exit_status = -1;
            thread_exit ();
          }

        struct file_descriptor *fdesc = get_file_descriptor (fd);
        if (fdesc == NULL)
          {
            thread_current ()->exit_status = -1;
            thread_exit ();
          }

        list_remove (&fdesc->elem);
        lock_acquire (&filesys_lock);
        file_close (fdesc->file);
        lock_release (&filesys_lock);
        free (fdesc);
      }
      break;

    /* Все остальные системные вызовы не реализованы */
    default:
      printf ("system call! (num=%d)\n", syscall_num);
      thread_current ()->exit_status = -1;
      thread_exit ();
      break;
    }
}
