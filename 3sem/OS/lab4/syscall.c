#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

/* Читает 4-байтовое значение со стека пользователя по байтовому смещению.
   Завершает процесс если указатель недействителен (указывает в ядро или NULL). */
static uint32_t
read_stack (struct intr_frame *f, int byte_offset)
{
  uint8_t *ptr = (uint8_t *) f->esp + byte_offset;
  if (ptr == NULL
      || !is_user_vaddr (ptr)
      || !is_user_vaddr (ptr + 3))
    {
      thread_current ()->exit_status = -1;
      thread_exit ();
    }
  return *(uint32_t *) ptr;
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t syscall_num = read_stack (f, 0);

  switch (syscall_num)
    {
    /* SYS_HALT — выключает эмулятор */
    case SYS_HALT:
      shutdown_power_off ();
      break;

    /* SYS_EXIT — завершает пользовательский процесс с кодом выхода.
       Код выхода сохраняется в структуре потока для вывода в process_exit
       и возврата из process_wait. */
    case SYS_EXIT:
      {
        int status = (int) read_stack (f, 4);   /* esp+4: первый аргумент */
        thread_current ()->exit_status = status;
        thread_exit ();
      }
      break;

    /* SYS_WRITE — записывает SIZE байт из BUFFER в файловый дескриптор FD.
       При fd=1 выводит в стандартный вывод (консоль). */
    case SYS_WRITE:
      {
        int fd        = (int)      read_stack (f, 4);   /* esp+4  */
        void *buffer  = (void *)   read_stack (f, 8);   /* esp+8  */
        unsigned size = (unsigned) read_stack (f, 12);  /* esp+12 */

        /* Проверяем что буфер полностью в пользовательском пространстве */
        if (buffer == NULL
            || !is_user_vaddr (buffer)
            || !is_user_vaddr ((char *) buffer + size))
          {
            thread_current ()->exit_status = -1;
            thread_exit ();
          }

        if (fd == 1)
          {
            /* Вывод в консоль */
            putbuf ((const char *) buffer, size);
            f->eax = size;
          }
        else
          {
            /* Другие файловые дескрипторы не поддерживаются на данном этапе */
            f->eax = -1;
          }
      }
      break;

    /* Все остальные системные вызовы пока не реализованы */
    default:
      printf ("system call! (num=%d)\n", syscall_num);
      thread_current ()->exit_status = -1;
      thread_exit ();
      break;
    }
}
