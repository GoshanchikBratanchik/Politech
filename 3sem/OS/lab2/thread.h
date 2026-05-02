#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>


/* Состояния в жизненном цикле потока. */
enum thread_status
  {
    THREAD_RUNNING,     /* Выполняющийся поток. */
    THREAD_READY,       /* Не выполняется, но готов к выполнению. */
    THREAD_BLOCKED,     /* Ожидает срабатывания события. */
    THREAD_DYING        /* Скоро будет уничтожен. */
  };

/* Тип идентификатора потока.
   Вы можете переопределить это на любой тип, который вам нравится. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Значение ошибки для tid_t. */

/* Приоритеты потоков. */
#define PRI_MIN 0                       /* Наименьший приоритет. */
#define PRI_DEFAULT 31                  /* Приоритет по умолчанию. */
#define PRI_MAX 63                      /* Наивысший приоритет. */

/* Поток ядра или пользовательский процесс.

   Каждая структура потока хранится в своей собственной 4 КБ странице.
   Сама структура потока находится в самом низу страницы
   (по смещению 0). Остальная часть страницы зарезервирована под
   стек ядра потока, который растёт вниз от верха страницы
   (по смещению 4 КБ). Вот иллюстрация:

        4 КБ +---------------------------------+
             |          стек ядра              |
             |                |                |
             |                |                |
             |                V                |
             |         растёт вниз             |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |             magic               |
             |                :                |
             |                :                |
             |              name               |
             |             status              |
        0 КБ +---------------------------------+

   Из этого следует два вывода:

      1. Во-первых, `struct thread' не должна становиться слишком
         большой. Если это произойдёт, то не останется достаточно места
         для стека ядра. Наша базовая `struct thread' занимает всего
         несколько байт. Она, вероятно, должна оставаться значительно меньше 1 КБ.

      2. Во-вторых, стеки ядра не должны становиться слишком
         большими. Если стек переполнится, он повредит состояние
         потока. Таким образом, функции ядра не должны выделять большие
         структуры или массивы в качестве нестатических локальных переменных. Используйте
         динамическое выделение с помощью malloc() или palloc_get_page()
         вместо этого.

   Первым симптомом любой из этих проблем, вероятно, будет
   сбой утверждения (assertion failure) в thread_current(), которая проверяет,
   что член `magic' выполняющегося потока `struct thread'
   установлен в THREAD_MAGIC. Переполнение стека обычно изменит это
   значение, вызывая сбой утверждения. */

/* Член `elem' имеет двойное назначение. Он может быть элементом
   очереди выполнения (thread.c) или элементом в
   списке ожидания семафора (synch.c). Он может использоваться этими двумя способами
   только потому, что они взаимно исключают друг друга: только поток в
   состоянии готовности находится в очереди выполнения, в то время как только поток в
   состоянии блокировки находится в списке ожидания семафора. */
struct lock;

   struct thread
  {
    /* Принадлежит thread.c. */
    tid_t tid;                          /* Идентификатор потока. */
    enum thread_status status;          /* Состояние потока. */
    char name[16];                      /* Имя (для целей отладки). */
    uint8_t *stack;
    bool is_new_alg;
    int ticks;
    int cpu_burst;
    struct list held_locks;
    struct lock *waiter_lock;
    int base_priority;                     /* Сохранённый указатель стека. */
    int priority;                       /* Приоритет. */
    struct list_elem allelem;           /* Элемент списка для списка всех потоков. */

    /* Общий для thread.c и synch.c. */
    struct list_elem elem;              /* Элемент списка. */

#ifdef USERPROG
    /* Принадлежит userprog/process.c. */
    uint32_t *pagedir;                  /* Каталог страниц. */
#endif

    /* Принадлежит thread.c. */
    unsigned magic;                     /* Обнаруживает переполнение стека. */
  };

/* Если false (по умолчанию), используется циклический планировщик.
   Если true, используется многоуровневый планировщик с обратной связью по очереди.
   Управляется параметром командной строки ядра "-o mlfqs". */
extern bool thread_mlfqs;


void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);
tid_t thread_create_burst (const char *name, int priority, int , thread_func *, void *); 

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Выполняет некоторую операцию над потоком t, используя вспомогательные данные AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);
void thread_set_priority_for(struct thread *, int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
