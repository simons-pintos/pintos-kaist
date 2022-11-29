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
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

int load_avg;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */

/* Idle thread. */
static struct thread *idle_thread; // 놀고 있는 스레드

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread; // init.c에서 실행되는 쓰레드

/* Lock used by allocate_tid(). */
static struct lock tid_lock; //

/* Thread destruction requests */
static struct list destruction_req; // 죽을 쓰레드의 리스트

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

static long long next_tick_to_awake; /* # of timer ticks in user programs. */

/* Scheduling. */
//각 스레드에 주어진 틱
#define TIME_SLICE 4 /* # of timer ticks to give each thread. */
//마지막 yield 다음 흐른 틱
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);
void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);
void update_next_tick_to_awake(int64_t ticks);
int64_t get_next_tick_to_awake(void);
void test_max_priority(void);
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void thread_set_priority(int new_priority);
void mlfqs_recalc_recent_cpu(void);
void mlfqs_recalc_priority(void);

/* Returns true if T appears to point to a valid thread. */
// 스레드가 널이 아니고, 스레드의 매직 값이 매직을 유지하면 유효하다
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */

// 새로 추가 1
void thread_sleep(int64_t ticks)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	old_level = intr_disable();
	if (curr != idle_thread)
	{
		// curr->status = THREAD_BLOCKED;
		curr->wakeup_tick = ticks;
		update_next_tick_to_awake(ticks); // 추후 볼 것
		list_push_back(&sleep_list, &curr->elem);
		// schedule();
		do_schedule(THREAD_BLOCKED);
	}
	intr_set_level(old_level);
	/* 현재 스레드가 idle 스레드가 아닐경우
thread의 상태를 BLOCKED로 바꾸고 깨어나야 할 ticks을 저장,
슬립 큐에 삽입하고, awake함수가 실행되어야 할 tick값을 update */
	/* 현재 스레드를 슬립 큐에 삽입한 후에 스케줄한다. */ /* 해당 과정중에는 인터럽트를 받아들이지 않는다. */
}

//새로 추가 2
void test_max_priority(void)
{
	if (!list_empty(&ready_list))
	{
		struct thread *ready_max_t = list_entry(list_begin(&ready_list), struct thread, elem);
		if (ready_max_t->priority > thread_current()->priority)
			thread_yield();
	}
}

bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *t_a;
	struct thread *t_b;

	t_a = list_entry(a, struct thread, elem);
	t_b = list_entry(b, struct thread, elem);
	if (t_a->priority > t_b->priority)
		return true;
	return false;
}

void thread_awake(int64_t ticks)
{
	next_tick_to_awake = INT64_MAX;
	struct thread *t = NULL;
	struct list_elem *temp_elem = list_begin(&sleep_list);

	while (temp_elem != list_tail(&sleep_list))
	{
		t = list_entry(temp_elem, struct thread, elem);
		if (t->wakeup_tick <= ticks)
		{
			temp_elem = list_remove(&t->elem);
			thread_unblock(t);
		}
		else
		{
			update_next_tick_to_awake(t->wakeup_tick);
			temp_elem = temp_elem->next;
		}
	}
	/* sleep list의 모든 entry 를 순회하며 다음과 같은 작업을 수행한다. 현재 tick이 깨워야 할 tick 보다 크거나 같다면 슬립 큐에서 제거하고
unblock 한다.
작다면 update_next_tick_to_awake() 를 호출한다.
*/
}

void update_next_tick_to_awake(int64_t ticks)
{
	if (next_tick_to_awake > ticks)
		next_tick_to_awake = ticks;
}

int64_t get_next_tick_to_awake(void)
{
	return next_tick_to_awake;
}

void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&destruction_req);
	list_init(&sleep_list);
	list_init(&all_list);
	// list_init(&all_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	load_avg = LOAD_AVG_DEFAULT;

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current(); // 현재 스레드 (running) 정의

	/* Update statistics. */
	if (t == idle_thread) // 현재 스레드가 아이들스레드라면, 아이들 틱 ++
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++; // 아니면 커널 틱 ++

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE) // 스레드틱이 1 증가되었을때, 타임슬라이스보다 크면 (경과되면)
		intr_yield_on_return();		  // yield_on_return을 1로 설정 (타임슬라이스 초과 시, 스케쥴링 가동)
}

/* Prints thread statistics. */
// 스레드 셧다운 될 때, 스레드 통계 출력
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */

// 새로 생성된 스레드가 실행 중인 스레드보다 우선순위가 높을 경우 CPU를 선점하도록 하기 위해
// 수정해야됨
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t; // t 선언
	tid_t tid;		  // tid 선언

	ASSERT(function != NULL); // 인자 function이 null 이 아닌지 체크

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO); // zere-page 콘텐츠로 얼록
	if (t == NULL)				   // t가 null이라면, 에러출력
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority); // thread 이닛
	tid = t->tid = allocate_tid();	// thread의 tid를

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	t->file_descriptor_table = palloc_get_multiple(PAL_ZERO, FDT_PAGES);
	if (t->file_descriptor_table == NULL)
		return TID_ERROR;
	t->fd_number = 2;

	t->file_descriptor_table[0] = 1;
	t->file_descriptor_table[1] = 2;

	t->stdin_count = 1;
	t->stdout_count = 1;

	/* Add to run queue. */
	thread_unblock(t);
	struct thread *curr = thread_current();
	if (t->priority > curr->priority)
		thread_yield();

	list_push_back(&curr->child_list, &t->child_elem);

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */

// running 상태의 스레드를 block으로 바꾸는 함수
//  검사 : 스레드가 외부인터럽트를 프로세싱하지 않아야함 / 인터럽트 레벨이 off여야됨
//  그 후, 스레드의 상태를 블럭으로 만듦
//  그 후, 스케쥴 함수 실행
void thread_block(void)
{
	ASSERT(!intr_context());				   // 외부 인터럽트를 프로세싱하지 않는지 검사
	ASSERT(intr_get_level() == INTR_OFF);	   // 레벨이 itr_off인지 검사
	thread_current()->status = THREAD_BLOCKED; // status를 블락으로 바꿔줌
	schedule();								   // 스케쥴링 > 문맥전환
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */

// block 상태의 스레드를 Ready 상태로 바꿔주는 함수
// 인터럽트에 off로 바꿔줘야됨
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t)); // 스레드가 존재하는지, 유효한지 검사함

	old_level = intr_disable();			 // itr을 off로 저장
	ASSERT(t->status == THREAD_BLOCKED); // 스테이터스가 블락 상태인지 검사
										 // list_push_back(&ready_list, &t->elem); // 레디리스트에 넣어줌
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);
	t->status = THREAD_READY;  // 스테이터스를 레디 상태로 바꿔줌
	intr_set_level(old_level); // 예전으로 itr 레벨을 바꿔줌 (잠시만 off 였던 것이다)
}

/* Returns the name of the running thread. */
//쓰레드의 네임을 리턴해줌
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */

// running 상태의 쓰레드 리턴
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */

// running 상태의 스레드의 아이디 리턴
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
//

// 현재 running 중인 스레드를 디스케쥴하고 파괴!!
void thread_exit(void)
{
	ASSERT(!intr_context()); // 외부인터럽트에 프로세싱하지 않는지 확인

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable();			   // intr level을 off로 해줌
	do_schedule(THREAD_DYING); // cpu를 점유하고 있는 현재 스레드를 다른 스레드로 교체해주고 현재 스레드를 dying으로 바꿔준다 ㅜㅜ
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
// CPU를 양보하고 ( 물러나고...) 스케쥴러에 cpu 할당
void thread_yield(void)
{
	struct thread *curr = thread_current(); // running 중인 스레드
	enum intr_level old_level;

	ASSERT(!intr_context()); // 외부 인터럽트에 프로세싱 안 하는 것 맞는지 확인

	old_level = intr_disable(); // intr level을 off로 바꿔줌
	if (curr != idle_thread)	// 현재 쓰레드가 아이들쓰레드가 아니라면,
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);
	do_schedule(THREAD_READY); // cpu를 점유하고 있는 현재 스레드를 다른 스레드로 교체해주고 현재 스레드를 ready로 바꿔준다
	intr_set_level(old_level); // itrl level을 on으로로 해줌
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority) // 커렌트쓰레드의 priority를 설정해줌
{
	if (thread_mlfqs)
		return;

	struct thread *curr = thread_current();
	curr->init_priority = new_priority;
	curr->priority = new_priority;

	refresh_priority();
	donate_priority();

	test_max_priority(); //레디리스트 맨앞과 priority 비교해서 바꿔줌
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_current()->priority; // 커렌트스레드의 priority를 리턴해줌
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;
	old_level = intr_disable();

	curr->nice = nice;
	mlfqs_priority(curr);

	test_max_priority();

	intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	enum intr_level old_level;

	struct thread *curr = thread_current();
	old_level = intr_disable();
	int nice_val = curr->nice;

	intr_set_level(old_level);

	return nice_val;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	enum intr_level old_level;
	old_level = intr_disable();

	// int manipulated_load_avg = fp_to_int_round(mult_mixed(load_avg, 100));
	int manipulated_load_avg = fp_to_int_round(mult_mixed(load_avg, 100));
	intr_set_level(old_level);
	return manipulated_load_avg;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
	enum intr_level old_level;
	old_level = intr_disable();

	struct thread *curr = thread_current();
	// int manipulated_recent_cpu = fp_to_int_round(mult_mixed(curr->recent_cpu, 100));
	int manipulated_recent_cpu = fp_to_int(mult_mixed(curr->recent_cpu, 100));

	intr_set_level(old_level);

	return manipulated_recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt"
					 :
					 :
					 : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
// 블락드 스레드로서 네임과, 프라이오리티를 부여하면서 스레드를 초기화함
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);									// 스레드가 널이 아니여야됨
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX); // 프라이오리티값이 민, 맥스 값 사이여야됨
	ASSERT(name != NULL);								// 네임이 널이 아니여야됨

	memset(t, 0, sizeof *t);						   //스레드 만큼 메모리 할당
	t->status = THREAD_BLOCKED;						   // 스테이터스를 블락드로 바꿔줌
	strlcpy(t->name, name, sizeof t->name);			   //스레드의 네임에 네임을 복사해준다
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *); // 스위칭 정보 설정 (???)
	t->priority = priority;							   // 프라이오티 파라미터값을 넣어줌
	t->magic = THREAD_MAGIC;						   // 매직 값 설정
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	t->exit_status = 0;

	list_init(&t->donations);
	list_init(&t->child_list);
	list_push_back(&all_list, &t->all_elem);

	sema_init(&t->fork_sema, 0);
	sema_init(&t->wait_sema, 0);
	sema_init(&t->free_sema, 0);

	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;
	t->running_file = NULL;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */

// 다음으로 러닝될 스레드를 고르고 리턴하는 함수, 런큐가 비어있지 않으면 런큐에서 가져옴
// 만약에 런큐가 비어있으면, 아이들스레드 리턴함
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		:
		: "g"((uint64_t)tf)
		: "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		:
		: "g"(tf_cur), "g"(tf)
		: "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */

// dying리스트에 있는 스레드를 순차적으로 죽이면서 (victim) 현재 running 중인 스레드의 스테이터스를 함수 인자로 바꿔줌
// 스케쥴러에게 cpu 주도권을 넘겨줌
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		list_remove(&victim->all_elem);
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

// 다음 실행될 스레드를 레디리스트에서 뽑거나 없다면 아이들리스트를 하나 가져와서 러닝스레드로 바꿔줌!
// thread tick을 다시 0으로 초기화 (yield한 것과 다름 없기에!)
static void
schedule(void)
{
	struct thread *curr = running_thread();		// curr는 현재 running 중인 스레드
	struct thread *next = next_thread_to_run(); // next는 다음에 실행될 스레드

	ASSERT(intr_get_level() == INTR_OFF);	// intr_off 인지 검사
	ASSERT(curr->status != THREAD_RUNNING); // 현재 스레드가 running 상태가 아닌 것이 맞는지 검사
	ASSERT(is_thread(next));				// 다음 스레드가 있는지, 유효한지 검사
	/* Mark us as running. */
	next->status = THREAD_RUNNING; // 다음 스레드의 status를 running으로 바꿔줌

	/* Start new time slice. */
	thread_ticks = 0; // thread_ticks를 0으로 바꿔줌

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

void mlfqs_priority(struct thread *t)
{
	// ASSERT(t != idle_thread);
	if (t != idle_thread)
		// t->priority = PRI_MAX - (t->recent_cpu / 4) - (t->nice * 2);
		// t->priority = PRI_MAX - div_mixed(t->recent_cpu, 4) - (t->nice * 2);
		// t->priority = PRI_MAX - (t->nice * 2) + div_mixed(t->recent_cpu, -4);
		// t->priority = div_mixed(t->recent_cpu, -4) + PRI_MAX - (t->nice * 2);
		t->priority = fp_to_int_round(add_mixed(div_mixed(t->recent_cpu, -4), PRI_MAX - t->nice * 2));
}

void mlfqs_recent_cpu(struct thread *t)
{
	if (t != idle_thread)
		// t->recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * t->recent_cpu + t->nice;
		// t->recent_cpu = add_mixed(div_fp(mult_mixed(load_avg, 2), mult_fp((add_mixed(mult_mixed(load_avg, 2), 1)), t->recent_cpu)), t->nice);
		t->recent_cpu = add_mixed(mult_fp(div_fp(mult_mixed(load_avg, 2), add_mixed(mult_mixed(load_avg, 2), 1)), t->recent_cpu), t->nice);
}

void mlfqs_load_avg(void)
{
	int ready_threads;

	if (thread_current() == idle_thread)
		ready_threads = list_size(&ready_list);
	else
		ready_threads = list_size(&ready_list) + 1;

	// load_avg = (59 / 60) * load_avg + (1 / 60) * ready_threads;
	load_avg = add_fp(mult_fp(div_fp(int_to_fp(59), int_to_fp(60)), load_avg), mult_mixed(div_fp(int_to_fp(1), int_to_fp(60)), ready_threads));
}

void mlfqs_increment(void) // recent_cpu 1 증가
{
	struct thread *curr = thread_current();

	// ASSERT(curr != idle_thread);
	if (curr != idle_thread)
		// curr->recent_cpu++;
		thread_current()->recent_cpu = add_mixed(curr->recent_cpu, 1);
}

void mlfqs_recalc(void) // 모든 스레드의 recent_cpu와 priority 값을 재계산한다.
{
	struct list_elem *e = list_begin(&all_list);
	struct thread *t;

	while (e != list_tail(&all_list))
	{
		t = list_entry(e, struct thread, all_elem);

		mlfqs_recent_cpu(t);
		mlfqs_priority(t);

		e = list_next(e);
	}
}

void mlfqs_recalc_recent_cpu(void) // 모든 스레드의 recent_cpu와 priority 값을 재계산한다.
{
	struct list_elem *e = list_begin(&all_list);
	struct thread *t;

	while (e != list_tail(&all_list))
	{
		t = list_entry(e, struct thread, all_elem);
		mlfqs_recent_cpu(t);

		e = list_next(e);
	}
}

void mlfqs_recalc_priority(void) // 모든 스레드의 recent_cpu와 priority 값을 재계산한다.
{
	struct list_elem *e = list_begin(&all_list);
	struct thread *t;

	while (e != list_tail(&all_list))
	{
		t = list_entry(e, struct thread, all_elem);
		mlfqs_priority(t);

		e = list_next(e);
	}
}
