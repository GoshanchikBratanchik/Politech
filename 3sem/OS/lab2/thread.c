#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Случайное значение для члена `magic` структуры thread.
   Используется для обнаружения переполнения стека. Подробности смотрите в большом комментарии
   вверху thread.h. */
#define THREAD_MAGIC 0xcd6abf4b

/* Список процессов в состоянии THREAD_READY, то есть процессов,
   которые готовы к выполнению, но фактически не выполняются. */
static struct list ready_list;

/* Список всех процессов. Процессы добавляются в этот список
   при первом планировании и удаляются при завершении. */
static struct list all_list;

/* Поток idle (бездействия). */
static struct thread *idle_thread;

/* Начальный поток, выполняющий init.c:main(). */
static struct thread *initial_thread;

/* Блокировка, используемая allocate_tid(). */
static struct lock tid_lock;

/* Структура кадра стека для kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Адрес возврата. */
    thread_func *function;      /* Функция для вызова. */
    void *aux;                  /* Вспомогательные данные для функции. */
  };

/* Статистика. */
static long long idle_ticks;    /* Количество тиков таймера, потраченных в бездействии. */
static long long kernel_ticks;  /* Количество тиков таймера в потоках ядра. */
static long long user_ticks;    /* Количество тиков таймера в пользовательских программах. */

/* Планирование. */
#define TIME_SLICE 4            /* Количество тиков таймера, выделяемых каждому потоку. */
static unsigned thread_ticks;   /* Количество тиков таймера с последнего yield. */

/* Если false (по умолчанию), используется планировщик round-robin.
   Если true, используется многоуровневый планировщик с обратной связью (MLFQ).
   Управляется параметром командной строки ядра "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
static bool less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void thread_set_priority_for(struct thread *t, int new_priority);


/* Инициализирует систему потоков, преобразуя текущий выполняющийся код
   в поток. Это не может работать в общем случае и возможно здесь только потому,
   что loader.S аккуратно разместил дно стека на границе страницы.

   Также инициализирует очередь выполнения и блокировку tid.

   После вызова этой функции обязательно инициализируйте аллокатор страниц,
   прежде чем пытаться создавать потоки с помощью thread_create().

   Небезопасно вызывать thread_current() до завершения этой функции. */

static int threads_burst = 0;

static bool less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct thread *t1 = list_entry(a, struct thread, elem);
  struct thread *t2 = list_entry(b, struct thread, elem);
  return t1->priority > t2->priority;
}

void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  


  /* Устанавливает структуру потока для текущего выполняющегося потока. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Запускает вытесняющее планирование потоков, разрешая прерывания.
   Также создает поток idle. */
void
thread_start (void) 
{
  /* Создает поток idle. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Запускает вытесняющее планирование потоков. */
  intr_enable ();

  /* Ждет инициализации idle_thread потоком idle. */
  sema_down (&idle_started);
}

/* Вызывается обработчиком прерывания таймера при каждом тике таймера.
   Таким образом, эта функция выполняется в контексте внешнего прерывания. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Обновляет статистику. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;
  
  if (t->is_new_alg) {
    if (thread_current() != idle_thread) {
      struct thread *cur = thread_current();
      cur->ticks++;

      if (cur->cpu_burst > 0 && cur->ticks >= cur->cpu_burst) {
        intr_yield_on_return();
        
      }
    }

  }

  /* Обеспечивает вытеснение. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Печатает статистику потоков. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Создает новый поток ядра с именем NAME и заданным начальным
   PRIORITY, который выполняет FUNCTION, передавая AUX в качестве аргумента,
   и добавляет его в очередь готовых. Возвращает идентификатор потока
   для нового потока или TID_ERROR, если создание не удалось.

   Если был вызван thread_start(), новый поток может быть запланирован
   до возврата из thread_create(). Он может даже завершиться
   до возврата из thread_create(). И наоборот, исходный
   поток может выполняться любое количество времени до планирования нового потока.
   Используйте семафор или другую форму синхронизации, если нужно обеспечить порядок.

   Предоставленный код устанавливает член `priority` нового потока в PRIORITY,
   но фактическое планирование по приоритетам не реализовано.
   Планирование по приоритетам — цель задачи 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;


  ASSERT (function != NULL);

  /* Выделяет память под поток. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Инициализирует поток. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Подготавливает поток к первому запуску, инициализируя его стек.
     Делает это атомарно, чтобы промежуточные значения члена 'stack'
     не могли быть наблюдаемы. */
  old_level = intr_disable ();

  /* Кадр стека для kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Кадр стека для switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Кадр стека для switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Добавляет в очередь выполнения. */
  thread_unblock (t);
  t->base_priority = priority;
  t->is_new_alg = false;

  if (!list_empty(&ready_list)) {
    struct thread *f = list_entry(list_front(&ready_list), struct thread, elem);

    if (thread_current()->priority < f->priority) thread_yield();
}



  return tid;
}

tid_t
thread_create_burst (const char *name, int priority, int cpu_burst, thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;


  ASSERT (function != NULL);

  /* Выделяет память под поток. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Инициализирует поток. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Подготавливает поток к первому запуску, инициализируя его стек.
     Делает это атомарно, чтобы промежуточные значения члена 'stack'
     не могли быть наблюдаемы. */
  old_level = intr_disable ();

  /* Кадр стека для kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Кадр стека для switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Кадр стека для switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Добавляет в очередь выполнения. */
  thread_unblock (t);
  t->base_priority = priority;
  t->cpu_burst = cpu_burst;
  t->ticks = 0;
  t->is_new_alg = true;
  threads_burst++;

  if (!list_empty(&ready_list)) {
    struct thread *f = list_entry(list_front(&ready_list), struct thread, elem);

    if (thread_current()->priority < f->priority) thread_yield();
}

  return tid;
}


/* Переводит текущий поток в состояние сна. Он не будет запланирован
   снова до пробуждения с помощью thread_unblock().

   Эта функция должна вызываться с отключенными прерываниями.
   Обычно лучше использовать один из примитивов синхронизации из synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Переводит заблокированный поток T в состояние готовности к выполнению.
   Это ошибка, если T не заблокирован. (Используйте thread_yield(), чтобы
   сделать выполняющийся поток готовым.)

   Эта функция не вытесняет выполняющийся поток. Это может быть важно:
   если вызывающая сторона сама отключила прерывания,
   она может ожидать, что сможет атомарно разблокировать поток и
   обновить другие данные. */
void
thread_unblock (struct thread *t) 
{

  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

  list_insert_ordered (&ready_list, &t->elem, less, NULL);
  
  t->status = THREAD_READY;
  intr_set_level (old_level);
}
/* Возвращает имя выполняющегося потока. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Возвращает выполняющийся поток.
   Это running_thread() плюс несколько проверок корректности.
   Подробности смотрите в большом комментарии вверху thread.h. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Убеждается, что T действительно поток.
     Если сработает любое из этих утверждений, то ваш поток, возможно,
     переполнил свой стек. У каждого потока менее 4 КБ
     стека, поэтому несколько больших автоматических массивов или умеренная
     рекурсия могут вызвать переполнение стека. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Возвращает tid выполняющегося потока. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Снимает текущий поток с выполнения и уничтожает его. Никогда
   не возвращается к вызывающей стороне. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Удаляет поток из списка всех потоков, устанавливает статус в "умирающий",
     и планирует другой процесс. Этот процесс уничтожит нас,
     когда вызовет thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Отдает процессор. Текущий поток не переводится в сон и
   может быть запланирован снова немедленно по усмотрению планировщика. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered (&ready_list, &cur->elem, less, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Вызывает функцию 'func' для всех потоков, передавая 'aux'.
   Эта функция должна вызываться с отключенными прерываниями. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Устанавливает приоритет текущего потока в NEW_PRIORITY. */
void thread_set_priority (int new_priority)
{
    struct thread *cur = thread_current();
    cur->base_priority = new_priority;

    int effective = new_priority;

    struct list_elem *e;
    for (e = list_begin(&cur->held_locks); e != list_end(&cur->held_locks); e = list_next(e))
    {
        struct lock *l = list_entry(e, struct lock, elem);

        if (!list_empty(&l->semaphore.waiters)) {
            struct thread *w = list_entry(list_front(&l->semaphore.waiters), struct thread, elem);

            if (w->priority > effective)
                effective = w->priority;
        }
    }

    cur->priority = effective;

    /* yield если кто-то сильнее */
    if (!list_empty(&ready_list)) {
        struct thread *t = list_entry(list_front(&ready_list), struct thread, elem);

        if (t->priority > cur->priority)
            thread_yield();
    }
}






/* Возвращает приоритет текущего потока. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Устанавливает значение nice текущего потока в NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Еще не реализовано. */
}

/* Возвращает значение nice текущего потока. */
int
thread_get_nice (void) 
{
  /* Еще не реализовано. */
  return 0;
}

/* Возвращает 100-кратную среднюю загрузку системы. */
int
thread_get_load_avg (void) 
{
  /* Еще не реализовано. */
  return 0;
}

/* Возвращает 100-кратное значение recent_cpu текущего потока. */
int
thread_get_recent_cpu (void) 
{
  /* Еще не реализовано. */
  return 0;
}

/* Поток idle (бездействия). Выполняется, когда нет других готовых потоков.

   Поток idle изначально помещается в список готовых с помощью
   thread_start(). Он будет запланирован один раз изначально, после чего
   инициализирует idle_thread, "поднимает" (up) семафор, переданный
   ему, чтобы позволить продолжить выполнение thread_start(), и немедленно
   блокируется. После этого поток idle никогда не появляется в списке готовых.
   Он возвращается next_thread_to_run() как особый случай, когда список готовых пуст. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Позволяет выполниться другому потоку. */
      intr_disable ();
      thread_block ();

      /* Повторно включает прерывания и ждет следующего.

         Инструкция `sti` отключает прерывания до завершения
         следующей инструкции, поэтому эти две инструкции выполняются атомарно.
         Эта атомарность важна; в противном случае прерывание может быть обработано
         между повторным включением прерываний и ожиданием следующего прерывания,
         тратя впустую до одного тика таймера.

         Смотрите [IA32-v2a] "HLT", [IA32-v2b] "STI" и [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Функция, используемая как основа для потока ядра. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* Планировщик работает с отключенными прерываниями. */
  function (aux);       /* Выполняет функцию потока. */
  thread_exit ();       /* Если function() возвращается, убивает поток. */
}

/* Возвращает выполняющийся поток. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Копирует указатель стека процессора в `esp`, а затем округляет его
     вниз до начала страницы. Поскольку `struct thread` всегда находится
     в начале страницы, а указатель стека — где-то посередине,
     это находит текущий поток. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Возвращает true, если T, по-видимому, указывает на корректный поток. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Выполняет базовую инициализацию T как заблокированного потока с именем
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  list_init(&t->held_locks);
  t->waiter_lock = NULL;
  t->base_priority = priority;
  list_push_back (&all_list, &t->allelem);
}

/* Выделяет кадр размером SIZE байт вверху стека потока T и
   возвращает указатель на начало кадра. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Данные стека всегда выделяются блоками размером со слово. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Выбирает и возвращает следующий поток для планирования. Должен
   возвращать поток из очереди выполнения, если очередь не пуста.
   (Если выполняющийся поток может продолжать работу, то он будет
   в очереди выполнения.) Если очередь выполнения пуста, возвращает
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Завершает переключение потоков, активируя таблицы страниц нового потока
   и, если предыдущий поток умирает, уничтожая его.

   При вызове этой функции мы только что переключились с потока
   PREV, новый поток уже выполняется, а прерывания все еще отключены.
   Эта функция обычно вызывается thread_schedule() в качестве последнего действия
   перед возвратом, но при первом планировании потока она вызывается
   switch_entry() (см. switch.S).

   Небезопасно вызывать printf() до завершения переключения потоков.
   На практике это означает, что printf() следует добавлять в конце функции.

   После возврата этой функции и ее вызывающей стороны переключение потоков
   завершено. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Помечает нас как выполняющийся. */
  cur->status = THREAD_RUNNING;

  /* Начинает новый квант времени. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Активирует новое адресное пространство. */
  process_activate ();
#endif

  /* Если поток, с которого мы переключились, умирает, уничтожает его структуру thread.
     Это должно происходить поздно, чтобы thread_exit() не "выдернул ковер у себя из-под ног".
     (Мы не освобождаем initial_thread, потому что его память не была получена через
     palloc().) */
  
  if (cur->is_new_alg && cur->cpu_burst > 0 && cur->ticks >= cur->cpu_burst) {
    msg("%s finished: work time = %d", cur->name, cur->ticks);
    thread_exit();
  }    
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Планирует новый процесс. При входе прерывания должны быть отключены, и
   состояние выполняющегося процесса должно было быть изменено с "выполняется"
   на другое состояние. Эта функция находит другой поток для выполнения
   и переключается на него.

   Небезопасно вызывать printf() до завершения thread_schedule_tail(). */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Возвращает tid для использования в новом потоке. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Смещение члена `stack` внутри `struct thread`.
   Используется switch.S, который не может вычислить его самостоятельно. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
