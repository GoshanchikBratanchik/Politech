#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* Счётный семафор. */
struct semaphore 
  {
    unsigned value;             /* Текущее значение. */
    struct list waiters;        /* Список ожидающих потоков. */
  };

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Блокировка. */
struct lock 
  {
    struct thread *holder;      /* Поток, удерживающий блокировку (для отладки). */
    struct semaphore semaphore;
    struct list_elem elem; /* Бинарный семафор, контролирующий доступ. */
  };

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Условная переменная. */
struct condition 
  {
    struct list waiters;        /* Список ожидающих потоков. */
  };

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* Барьер оптимизации.

   Компилятор не будет переупорядочивать операции через
   барьер оптимизации. См. "Барьеры оптимизации" в
   справочном руководстве для получения дополнительной информации.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
