#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
#define NICE_MIN -20                   /* [문서 B.1] nice 범위 -20~20 */
#define NICE_MAX 20

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
/* alarm: 잠든 스레드는 timer.c의 sleep_list에도 elem으로 들어감 */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	int64_t wake_tick;					// 깨어날 시간

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	
	int init_priority;              /* 기부받기 전 원래 우선순위 */
	struct lock *wait_on_lock;      /* 지금 기다리는 lock (연쇄 기부용) */
	struct list donations;          /* 나에게 기부한 스레드 목록 */
	struct list_elem donation_elem; /* 남의 donations에 들어갈 연결고리 */

	// MLFQS
	int recent_cpu;					/* [문서 B.3] 최근 CPU 사용량. 17.14 고정소수점 [문서 B.6] */
	int nice;								/* [문서 B.1] 양보 정도 (-20~20, 정수). 클수록 priority가 낮아짐 */
	struct list_elem allelem; /* all_list에 걸기 위한 고리, blocked 포함 전체 순회용 */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

void preempt_if_needed(void);			// ready_list의 첫 번째 스레드가 현재 스레드보다 우선순위가 더 높으면 선점

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

void thread_set_wake(int64_t ticks);	// 스레드가 일어날 시간을 설정. (예: 현재 tick + 원하는 시간)

void refresh_priority(struct thread *t);// 우선순위 재계산
int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
/* [문서 B.3] recent_cpu의 100배를 가장 가까운 정수로 반올림해 반환 */
int thread_get_recent_cpu (void);
/* [문서 B.4] load_avg의 100배를 가장 가까운 정수로 반올림해 반환 */
int thread_get_load_avg (void);
/* [문서 B.3] 매 틱, 실행 중인 스레드의 recent_cpu +1 (idle 제외) */
void increment_recent_cpu(void);		
/* [문서 B.2] priority = PRI_MAX - recent_cpu/4 - nice*2
   결과는 버림(rounded down), PRI_MIN~PRI_MAX로 제한 */
void cal_priority(struct thread *t);
/* [문서 B.3] 매초 모든 스레드: recent_cpu = (2*la)/(2*la+1) * recent_cpu + nice
   [문서 B.3] 오버플로 방지를 위해 계수를 먼저 계산 */
void cal_recent_cpu(void);
/* [문서 B.4] 매초: load_avg = (59/60)*load_avg + (1/60)*ready_threads
   ready_threads = 실행 중 + 준비 상태 스레드 수 (idle 제외) */
void cal_load_avg(void);
/* [문서 B.2] 4틱마다 모든 스레드 priority 재계산
   [구현] ready_list 순서가 깨지므로 정렬 후 선점 확인 */
void recal_priority_all(void);
/* [구현] 1초가 아닌 4틱에는 recent_cpu가 바뀐 실행 중 스레드만 재계산
   (다른 스레드는 값이 같으므로 B.2와 결과 동일) */
void recal_priority_current (void);
void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */