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

static void remove_with_lock (struct lock *lock);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back (&sema->waiters, &thread_current ()->elem);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
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

// thread.c에서 비교하는 함수와는 다르게 정렬 대신 최댓값을 선택하기 위해 부등호를 `<`로 사용
static bool compare_priority(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);
	return a->priority < b->priority;
}

// 세마포어 값을 증가시키고 대기자 중 우선순위가 가장 높은 스레드 깨우기
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	
	// 우선순위가 가장 높은 대기자를 찾아 리스트에서 제거
	if (!list_empty(&sema->waiters))
	{
		struct list_elem *e = list_max(&sema->waiters, compare_priority, NULL);

		list_remove(e);
		thread_unblock(list_entry(e, struct thread, elem));
	}

	sema->value++;
	preempt_if_needed();	// 깨운 스레드가 현재 스레드보다 높으면 양보
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
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

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
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
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void 
lock_acquire(struct lock *lock) {
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));
	/* 인터럽트 끄기 */
	enum intr_level old_level = intr_disable();
	struct thread *cur = thread_current();
	/* 락에 주인이 있으면 나는 곧 잠든다. 
	그 전에, 주인이 락을 빨리 풀 수 있도록 내 우선순위를 기부한다. */
	/* [문서 Advanced Scheduler] MLFQS에서는 priority donation을 하지 않음 */
	if (!thread_mlfqs && lock->holder != NULL)
	{
		/* 내가 이 락을 기다린다고 기록 */
		cur->wait_on_lock = lock;
		/* 락 주인의 명단에 내 고리를 건다. */
		list_push_back(&lock->holder->donations, &cur->donation_elem);
		/* h = 기부받을 스레드 (처음에는 락 주인) */
		struct thread *h = lock->holder;
		for (int depth = 0; depth < 8; depth++)
		{
			/* 내가 h보다 높지 않으면 더 기부할 필요가 없다. */
			if (cur->priority <= h->priority)
					break;
			h->priority = cur->priority;
			/* h가 다른 락을 기다리며 자고 있으면, 그 락의 주인으로 옮겨 계속 기부한다. 
			기다리는 락이 없거나, 그 락의 주인이 이미 풀어서 없으면 멈춘다. */
			if (h->wait_on_lock == NULL || h->wait_on_lock->holder == NULL)
				break;
			h = h->wait_on_lock->holder;
			}
	}
	/* 락이 비어있으면 바로 가져가거나(value 1->0), 주인이 있으면 풀릴 때까지 잠든다. */
	sema_down(&lock->semaphore);
	/* 이제 기다리지 않음. */
	cur->wait_on_lock = NULL;
	/* 내가 락 주인 */
	lock->holder = cur;
	/* 인터럽트 원래대로 */
	intr_set_level(old_level);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 이 락을 기다리며 나에게 기부한 스레드만 donations에서 제거 (다른 락의 기부는 유지)*/
static void remove_with_lock(struct lock *lock) {
	struct thread *cur = thread_current();
	struct list_elem *e = list_begin(&cur->donations);
	while (e != list_end(&cur->donations)) 
	{
		struct thread *t = list_entry(e, struct thread, donation_elem);
		if (t->wait_on_lock == lock)
			e = list_remove(e);
		else
			e = list_next(e);
	}
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */

void 
lock_release(struct lock *lock) {
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));
	
	/* 인터럽트 비활성화 */
	enum intr_level old_level = intr_disable();
	
	/* [문서 Advanced Scheduler] MLFQS에서는 donation이 없으므로 정리도 건너뜀 */
	if (!thread_mlfqs) {
		/* 해당 락 때문에 나한테 우선순위를 준 스레드를 그 목록에서 뺌 */
		remove_with_lock(lock);
		/* 남은 기부자 기준으로 재계산 */
		refresh_priority(thread_current());

	}
	/* 락 주인을 비운다. */
	lock->holder = NULL;
	/* 가장 높은 대기자를 깨우고, 그 스레드가 나보다 높으면 양보한다. */
	sema_up(&lock->semaphore);
	/* 인터럽트 원래대로 */
	intr_set_level (old_level);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
	struct thread *waiter;				// 대기 스레드를 바로 찾기 위한 포인터
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
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
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	waiter.waiter = thread_current();				// 현재 스레드를 대기자로
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

// semaphore_elem의 대기자끼리 비교
static bool compare_cond_priority(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED)
{
	const struct semaphore_elem *a = list_entry(a_, struct semaphore_elem, elem);
	const struct semaphore_elem *b = list_entry(b_, struct semaphore_elem, elem);

	return a->waiter->priority < b->waiter->priority;
}

// 조건 변수의 대기자 중 우선순위가 가장 높은 스레드에 신호 보내기
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	enum intr_level old_level = intr_disable();

	// 우선순위가 가장 높은 대기자를 리스트에서 제거하고 sema_up
	if (!list_empty (&cond->waiters))
	{
		struct list_elem *e = list_max(&cond->waiters, compare_cond_priority, NULL);
		struct semaphore_elem *waiter = list_entry(e, struct semaphore_elem, elem);

		list_remove(e);
		sema_up(&waiter->semaphore);
	}

	intr_set_level(old_level);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
