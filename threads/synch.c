/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

void donate_priority(void);
void remove_with_lock(struct lock *lock);
void refresh_priority(void);
bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{						  //세마포어를 이닛해줌
	ASSERT(sema != NULL); //세마포어는 널이 아니어야됨

	sema->value = value;	   // 세마의 value 설정
	list_init(&sema->waiters); // waiters리스트 만들어줌
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down(struct semaphore *sema)
{
	struct thread *curr = thread_current();
	// struct semaphore_elem *curr_elem = list_entry(&curr->elem, struct semaphore_elem, elem);
	// curr_elem->priority = curr->priority;

	enum intr_level old_level;

	ASSERT(sema != NULL);	 // 세마가 널이 아니여야됨
	ASSERT(!intr_context()); // 외부 인터럽트를 받지 않아야됨

	old_level = intr_disable(); // 순간 인터럽트 디스에이블로 만들어줌
	while (sema->value == 0)	// 세마값이 0인 동안 (자원이 없는 상태라면)
	{
		if (list_empty(&sema->waiters))
			list_push_front(&sema->waiters, &curr->elem);
		else
			list_insert_ordered(&sema->waiters, &curr->elem, cmp_priority, NULL);
		thread_block(); // 쓰레드를 블락상태로 만듦
	}
	sema->value--;			   // 세마값 1 깎음
	intr_set_level(old_level); // 다시 인터럽트 레벨 복구
}

// void sema_down(struct semaphore *sema)
// {
// 	enum intr_level old_level;
// 	struct semaphore_elem *curr_elem = list_entry(&thread_current()->elem, struct semaphore_elem, elem);
// 	curr_elem->priority = thread_current()->priority;

// 	ASSERT(sema != NULL);	 // 세마가 널이 아니여야됨
// 	ASSERT(!intr_context()); // 외부 인터럽트를 받지 않아야됨

// 	old_level = intr_disable(); // 순간 인터럽트 디스에이블로 만들어줌
// 	while (sema->value == 0)	// 세마값이 0인 동안 (자원이 없는 상태라면)
// 	{
// 		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_sem_priority, NULL);
// 		thread_block(); // 쓰레드를 블락상태로 만듦
// 	}
// 	sema->value--;			   // 세마값 1 깎음
// 	intr_set_level(old_level); // 다시 인터럽트 레벨 복구
// }

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema) // 세마포어가 있을 때만 세마다운하고 성공/실패 여부 리턴
{
	enum intr_level old_level;
	bool success; // 성공값을 선언 (성공 /실패 )

	ASSERT(sema != NULL); //세마가 0이 아니어야 됨

	old_level = intr_disable(); // 인터럽트에 디스에이블로 만들어줌
	if (sema->value > 0)		// 세마값이 0보다 크면 ( 자원이 있으면 )
	{
		sema->value--;	// 세마값을 1 깎아줌
		success = true; // 성공
	}
	else
		success = false;	   // 실패
	intr_set_level(old_level); // 인터럽트 값 복귀

	return success; // 성공값 리턴
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema) // 세마포어를 풀어주고 (1증가), 웨이터리스트에 있던 놈이 있으면 언블락하여 레디리스트에 넣어줌
{
	enum intr_level old_level;

	ASSERT(sema != NULL); // 세마가 널이 아니어야 됨

	old_level = intr_disable();		 // 인터럽트 디스에이블 시켜놓음
	if (!list_empty(&sema->waiters)) // 웨이터리스트가 비어있지 않다면,
	{
		list_sort(&sema->waiters, cmp_priority, NULL);
		thread_unblock(list_entry(list_pop_front(&sema->waiters),
								  struct thread, elem)); // 스레드를 언블락시켜주고 레디리스트로 넣어줌
	}
	sema->value++; // 세마값 1 증가

	// priority preemption 코드 추가
	test_max_priority();

	intr_set_level(old_level); // 인터럽트 레벨 되돌려놓음
}

bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct semaphore_elem * sa = list_entry(a, struct semaphore_elem, elem); 
	struct semaphore_elem * sb = list_entry(b, struct semaphore_elem, elem);
	
	struct list_elem *sa_e = list_begin(&(sa->semaphore.waiters));
	struct list_elem *sb_e = list_begin(&(sb->semaphore.waiters));

	struct thread *sa_t = list_entry(sa_e, struct thread, elem);
	struct thread *sb_t = list_entry(sb_e, struct thread, elem);

	if (sa_t->priority > sb_t->priority)
		return true;
	return false;
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2]; // len 2 짜리 세마 선언
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0); // 0번째 원소에 value 0으로 이닛
	sema_init(&sema[1], 0); // 1번째 원소에 value 1로 이닛
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock) // 세마값 1짜리 세마포어 이닛 (lock)
{
	ASSERT(lock != NULL); // lock이 널이 아니어야됨

	lock->holder = NULL;			// lock-holder가 널이어야됨
	sema_init(&lock->semaphore, 1); // value를 1로 세마포어를 이닛
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);						//락이 널이 아니어야됨
	ASSERT(!intr_context());					// 인터럽트 레벨 off 아니어야됨
	ASSERT(!lock_held_by_current_thread(lock)); // 락 홀더가 현재쓰레드가 아니어야됨

	struct thread *curr = thread_current();
	if (lock->holder)
	{
		curr->wait_on_lock = lock;
		curr->init_priority = curr->priority;
		if (list_empty(&lock->holder->donations))
			list_push_front(&lock->holder->donations, &curr->donation_elem);
		else
			list_insert_ordered(&lock->holder->donations, &curr->donation_elem, cmp_donation_priority, NULL);
		donate_priority();
	}
	sema_down(&lock->semaphore); /// 락의 세마포어의 다운을 해줌
	thread_current()->wait_on_lock = NULL;

	lock->holder = thread_current(); // 락의 홀더를 현재쓰레드로 해줌
}

bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *t_a = list_entry(a, struct thread, donation_elem);
	struct thread *t_b = list_entry(b, struct thread, donation_elem);

	if (t_a->priority > t_b->priority)
		return true;
	return false;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock) // 자원이 있을 때만, 락을 걸어줌
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock)); // lock의 hold가 현재 스레드가 아니어야됨

	success = sema_try_down(&lock->semaphore); // 세마포어
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	lock->holder = NULL;

	remove_with_lock(lock);
	refresh_priority();

	sema_up(&lock->semaphore);
}

// void donate_priority(void) // 현진
// {
// 	struct thread *lock_holder = thread_current()->wait_on_lock->holder;
// 	struct thread *temp_t = lock_holder;

// 	int i = 1;
// 	while (i < 8 && temp_t->wait_on_lock != NULL)
// 	{
// 		if (thread_current()->priority > temp_t->priority)
// 		{
// 			temp_t->priority = thread_current()->priority;
// 			temp_t = temp_t->wait_on_lock->holder;
// 			i++;
// 		}
// 		else
// 		{
// 			break;
// 		}
// 	}

// 	temp_t->priority = thread_current()->priority;
// }

void donate_priority(void)
{
	int nested_depth = 0;
	struct thread *curr = thread_current();
	struct thread *lock_holder;

	while (nested_depth < 8)
	{
		if (!curr->wait_on_lock)
			break;

		lock_holder = curr->wait_on_lock->holder;

		if (curr->priority > lock_holder->priority)
			lock_holder->priority = curr->priority;

		curr = lock_holder;

		nested_depth++;
	}
}

void remove_with_lock(struct lock *lock)
{
	struct thread *curr = thread_current();
	struct list_elem *e = list_begin(&curr->donations);
	struct thread *t;

	while (e != list_tail(&curr->donations))
	{
		t = list_entry(e, struct thread, donation_elem);
		if (t->wait_on_lock == lock)
			e = list_remove(e);
		else
			e = list_next(e);
	}
}

void refresh_priority(void)
{
	struct thread *curr = thread_current();

	curr->priority = curr->init_priority;

	if (!list_empty(&curr->donations))
	{
		list_sort(&curr->donations, cmp_donation_priority, NULL);
		struct thread *t = list_entry(list_begin(&curr->donations), struct thread, donation_elem);
		if (curr->priority < t->priority)
			curr->priority = t->priority;
	}
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sem_priority, NULL);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, cmp_sem_priority, NULL);
		sema_up(&list_entry(list_pop_front(&cond->waiters),
							struct semaphore_elem, elem)
					 ->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL); // 컨디션이 널이 아니여야됨
	ASSERT(lock != NULL); // 락이 널이 아니여야됨

	while (!list_empty(&cond->waiters)) //컨디션의 웨이터리스트가 비어있지 않으면
		cond_signal(cond, lock);		// 컨디션 시그널발동
}
