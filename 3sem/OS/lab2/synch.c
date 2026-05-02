/* Этот файл основан на исходном коде учебной операционной системы Nachos.
   Уведомление об авторских правах Nachos воспроизведено полностью ниже. */

/* Авторское право (c) 1992-1996 Регенты Калифорнийского университета.
   Все права защищены.

   Разрешение на использование, копирование, модификацию и распространение этого программного обеспечения
   и его документации для любой цели, без оплаты, и
   без письменного соглашения настоящим предоставляется при условии, что
   вышеупомянутое уведомление об авторских правах и следующие два абзаца появляются
   во всех копиях этого программного обеспечения.

   НИ ПРИ КАКИХ ОБСТОЯТЕЛЬСТВАХ КАЛИФОРНИЙСКИЙ УНИВЕРСИТЕТ НЕ НЕСЕТ ОТВЕТСТВЕННОСТИ ПЕРЕД
   ЛЮБОЙ СТОРОНОЙ ЗА ПРЯМОЙ, КОСВЕННЫЙ, СПЕЦИАЛЬНЫЙ, СЛУЧАЙНЫЙ ИЛИ
   ПОСЛЕДОВАВШИЙ УЩЕРБ, ВОЗНИКШИЙ В РЕЗУЛЬТАТЕ ИСПОЛЬЗОВАНИЯ ДАННОГО ПРОГРАММНОГО ОБЕСПЕЧЕНИЯ
   И ЕГО ДОКУМЕНТАЦИИ, ДАЖЕ ЕСЛИ КАЛИФОРНИЙСКИЙ УНИВЕРСИТЕТ
   БЫЛ ПРЕДУПРЕЖДЕН О ВОЗМОЖНОСТИ ТАКОГО УЩЕРБА.

   КАЛИФОРНИЙСКИЙ УНИВЕРСИТЕТ ЯВНО ОТКАЗЫВАЕТСЯ ОТ КАКИХ-ЛИБО ГАРАНТИЙ,
   ВКЛЮЧАЯ, НО НЕ ОГРАНИЧИВАЯСЬ, ПОДРАЗУМЕВАЕМЫМИ ГАРАНТИЯМИ
   ТОВАРНОЙ ПРИГОДНОСТИ И ПРИГОДНОСТИ ДЛЯ ОПРЕДЕЛЕННОЙ ЦЕЛИ.
   ПРОГРАММНОЕ ОБЕСПЕЧЕНИЕ ПРЕДОСТАВЛЯЕТСЯ «КАК ЕСТЬ», И КАЛИФОРНИЙСКИЙ УНИВЕРСИТЕТ НЕ НЕСЕТ ОБЯЗАТЕЛЬСТВ ПО
   ТЕХНИЧЕСКОМУ ОБСЛУЖИВАНИЮ, ПОДДЕРЖКЕ, ОБНОВЛЕНИЮ, УЛУЧШЕНИЮ ИЛИ
   ИЗМЕНЕНИЮ. */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static bool less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
static bool cond_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);


/* Инициализирует семафор SEMA значением VALUE. Семафор - это
   неотрицательное целое число вместе с двумя атомарными операторами для
   манипулирования им:

   - down или "P": ждать, пока значение станет положительным, затем
     уменьшить его.

   - up или "V": увеличить значение (и разбудить один ожидающий
     поток, если таковой имеется). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

static bool less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct thread *t1 = list_entry(a, struct thread, elem);
  struct thread *t2 = list_entry(b, struct thread, elem);
  return t1->priority > t2->priority;
}

static bool max_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct thread *t1 = list_entry(a, struct thread, elem);
  struct thread *t2 = list_entry(b, struct thread, elem);
  return t1->priority < t2->priority;
}

void donate_priority(struct thread *from, struct thread *to)
{
    while (to != NULL && to->priority < from->priority) {
        to->priority = from->priority;

        if (to->waiter_lock != NULL)
            to = to->waiter_lock->holder;
        else
            break;
    }
}


/* Операция down или "P" над семафором. Ожидает, пока значение SEMA
   станет положительным, и затем атомарно уменьшает его.

   Эта функция может погрузить поток в сон, поэтому её не следует вызывать из
   обработчика прерываний. Эта функция может быть вызвана с
   отключенными прерываниями, но если она усыпляет поток, то следующий запланированный
   поток, вероятно, снова включит прерывания. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      list_insert_ordered(&sema->waiters, &thread_current()->elem, less, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Операция down или "P" над семафором, но только если
   семафор ещё не равен 0. Возвращает true, если семафор был
   уменьшен, false в противном случае.

   Эта функция может быть вызвана из обработчика прерываний. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Операция up или "V" над семафором. Увеличивает значение SEMA
   и будит один из потоков, ожидающих SEMA, если таковые имеются.

   Эта функция может быть вызвана из обработчика прерываний. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool yield = false;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty(&sema->waiters)) {
    struct list_elem *e = list_max(&sema->waiters, max_less, NULL);
    list_remove(e);
    struct thread *t = list_entry(e, struct thread, elem);
    thread_unblock(t);
    if (t->priority > thread_current()->priority) yield = true;
  }
  sema->value++;
  intr_set_level (old_level);
  if (yield) thread_yield();
}

static void sema_test_helper (void *sema_);

/* Самопроверка семафоров, при которой управление "перебрасывается"
   между парой потоков. Вставьте вызовы printf(), чтобы увидеть,
   что происходит. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Функция потока, используемая в sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Инициализирует LOCK. Блокировка может удерживаться не более чем одним
   потоком в любой момент времени. Наши блокировки не являются "рекурсивными",
   то есть ошибкой является попытка захвата блокировки, которую уже удерживает текущий поток.

   Блокировка - это специализация семафора с начальным
   значением 1. Разница между блокировкой и таким семафором двойственная.
   Во-первых, семафор может иметь значение больше 1, но блокировка может удерживаться только одним потоком одновременно.
   Во-вторых, семафор не имеет владельца,
   то есть один поток может сделать "down" семафора, а затем
   другой поток может сделать "up", но с блокировкой один и тот же поток должен и захватить, и освободить её.
   Когда эти ограничения становятся обременительными,
   это хороший признак того, что следует использовать семафор вместо блокировки. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Захватывает LOCK, при необходимости погружая поток в сон до тех пор, пока блокировка не станет доступной.
   Блокировка не должна уже удерживаться текущим потоком.

   Эта функция может погрузить поток в сон, поэтому её не следует вызывать из
   обработчика прерываний. Эта функция может быть вызвана с
   отключенными прерываниями, но прерывания будут включены обратно, если
   нам потребуется погрузить поток в сон. */
void lock_acquire(struct lock *lock)
{
    struct thread *cur = thread_current();

    if (lock->holder != NULL) {
      donate_priority(cur, lock->holder);
    }

    cur->waiter_lock = lock;
    sema_down(&lock->semaphore);
    cur->waiter_lock = NULL;

    lock->holder = cur;
    list_push_back(&cur->held_locks, &lock->elem);
}


/* Пытается захватить LOCK и возвращает true в случае успеха или false
   при неудаче. Блокировка не должна уже удерживаться текущим потоком.

   Эта функция не погружает поток в сон, поэтому её можно вызывать из
   обработчика прерываний. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Освобождает LOCK, который должен принадлежать текущему потоку.

   Обработчик прерываний не может захватить блокировку, поэтому нет смысла
   пытаться освободить блокировку в обработчике прерываний. */
void lock_release(struct lock *lock)
{
    struct thread *cur = thread_current();

    list_remove(&lock->elem);

    int new_priority = cur->base_priority;

    struct list_elem *e;
    for (e = list_begin(&cur->held_locks); e != list_end(&cur->held_locks); e = list_next(e))
    {
        struct lock *l = list_entry(e, struct lock, elem);
        if (!list_empty(&l->semaphore.waiters)) {
          struct thread *w = list_entry(list_front(&l->semaphore.waiters), struct thread, elem);
          if (w->priority > new_priority)
            new_priority = w->priority;
        }
    }

    cur->priority = new_priority;
    lock->holder = NULL;
    sema_up(&lock->semaphore);
}

/* Возвращает true, если текущий поток удерживает LOCK, false
   в противном случае. (Заметьте, что проверка, удерживает ли какой-либо другой поток
   блокировку, была бы небезопасной.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* Один семафор в списке. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* Элемент списка. */
    struct semaphore semaphore;         /* Этот семафор. */
  };

static bool cond_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct semaphore_elem *s1 = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem *s2 = list_entry(b, struct semaphore_elem, elem);
  struct thread *t1;
  struct thread *t2;
  if (!list_empty(&s1->semaphore.waiters)) 
    t1 = list_entry(list_front(&s1->semaphore.waiters), struct thread, elem);
  else t1 = thread_current();
  if (!list_empty(&s2->semaphore.waiters))
    t2 = list_entry(list_front(&s2->semaphore.waiters), struct thread, elem);
  else t2 = thread_current();
  return t1->priority > t2->priority;
  
}

/* Инициализирует условную переменную COND. Условная переменная
   позволяет одному фрагменту кода сигнализировать о состоянии, а другому кооперирующемуся
   коду принять сигнал и действовать в соответствии с ним. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Атомарно освобождает LOCK и ожидает, пока COND получит сигнал от
   какого-либо другого фрагмента кода. После того как COND получит сигнал, LOCK
   повторно захватывается перед возвратом. LOCK должен быть захвачен перед вызовом
   этой функции.

   Монитор, реализованный этой функцией, выполнен в стиле "Mesa", а не
   "Hoare", то есть отправка и получение сигнала не являются
   атомарной операцией. Таким образом, обычно вызывающая сторона должна повторно проверить
   условие после завершения ожидания и, при необходимости, ждать снова.

   Данная условная переменная связана только с одной
   блокировкой, но одна блокировка может быть связана с любым количеством
   условных переменных. То есть существует отношение "один ко многим"
   от блокировок к условным переменным.

   Эта функция может погрузить поток в сон, поэтому её не следует вызывать из
   обработчика прерываний. Эта функция может быть вызвана с
   отключенными прерываниями, но прерывания будут включены обратно, если
   нам потребуется погрузить поток в сон. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_insert_ordered (&cond->waiters, &waiter.elem, cond_less, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* Если какие-либо потоки ожидают на COND (защищенные LOCK), то
   эта функция посылает сигнал одному из них, чтобы он проснулся от ожидания.
   LOCK должен быть захвачен перед вызовом этой функции.

   Обработчик прерываний не может захватить блокировку, поэтому нет смысла
   пытаться сигнализировать условной переменной в обработчике прерываний. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Пробуждает все потоки, если таковые имеются, ожидающие на COND (защищенные
   LOCK). LOCK должен быть захвачен перед вызовом этой функции.

   Обработчик прерываний не может захватить блокировку, поэтому нет смысла
   пытаться сигнализировать условной переменной в обработчике прерываний. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
