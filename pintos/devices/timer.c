#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
/* thread.c를 고칠 필요없음. */
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* 잠든 스레드들을 모아 두는 리스트 (직접 추가) */
static struct list sleep_list;

/* Number of loops per timer tick.
	 Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops(unsigned loops);
static void busy_wait(int64_t loops);
static void real_time_sleep(int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
	 interrupt PIT_FREQ times per second, and registers the
	 corresponding interrupt. */
void timer_init(void)
{
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
		 nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb(0x43, 0x34); /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb(0x40, count & 0xff);
	outb(0x40, count >> 8);
	/* timer_interrupt가 핸들러로 등록이 되어있다. */
	intr_register_ext(0x20, timer_interrupt, "8254 Timer");
	/* 잠든 스레드 리스트 초기화 (직접 추가) */
	list_init(&sleep_list);
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void timer_calibrate(void)
{
	unsigned high_bit, test_bit;

	ASSERT(intr_get_level() == INTR_ON);
	printf("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
		 still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops(loops_per_tick << 1))
	{
		loops_per_tick <<= 1;
		ASSERT(loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops(high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf("%'" PRIu64 " loops/s.\n", (uint64_t)loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
/* 현재 시각을 돌려주는 함수임. */
int64_t
timer_ticks(void)
{
	/* 타이머 핸들러가 잠깐 실행되지 못하게 막음 (ticks++가 잠시 미뤄짐).
	원래 인터럽트 상태는 old_level에 기억 */
	enum intr_level old_level = intr_disable();
	/* 아무도 ticks를 못 바꾸는 상태에서 값을 t에 복사 */
	int64_t t = ticks;
	/* 인터럽트를 원래 상태로 돌려놓음 (미뤄진 ticks++가 이제 처리될 수 있음) */
	intr_set_level(old_level);
	/* 컴파일러가 위아래 코드 순서를 바꾸지 못하게 함 */
	barrier();
	/* 복사해 둔 값을 돌려줌 */
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
	 should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed(int64_t then)
{
	return timer_ticks() - then;
}

static bool
wakeup_tick_less(const struct list_elem *a,
								 const struct list_elem *b,
								 void *aux UNUSED)
{
	/* a와 b의 스레드 주소 */
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);
	/* bool을 리턴함 */
	return ta->wakeup_tick < tb->wakeup_tick;
}

/* Suspends execution for approximately TICKS timer ticks. */
void timer_sleep(int64_t ticks)
{
	/* alarm-zero.c 예외처리를 위함 : 0이하는 잠들 필요가 없다. */
	if (ticks <= 0)
		return;
	/* 인터럽트가 켜진 일반 스레드 문맥에서만 호출 가능 (핸들러 안 X) */
	ASSERT(intr_get_level() == INTR_ON);
	/* 인터럽트를 끄고(IF 비트 = 0), 끄기 전 상태(ON/OFF)를 old_level에 저장한다.
	 이후 sleep_list 작업과 block이 끝날 때까지 timer_interrupt가 끼어들지 못한다. */
	enum intr_level old_level = intr_disable();
	/* [원본 코드 삭제: 무한루프] while (timer_elapsed(start) < ticks) */
	/* 깨울 시간 = 지금 + 잘 시간 */
	thread_current()->wakeup_tick = timer_ticks() + ticks;

	/* sleep_list가 일찍 일어날 순서로 정렬되도록 하여 wakeup_tick이 작은 쪽을 앞으로 하여 현재 주소의 칸을 넣음 */
	list_insert_ordered(&sleep_list, &thread_current()->elem,
											wakeup_tick_less, NULL);
	/* 잠든 나를 BLOCKED로 만들고 다른 스레드에게 CPU를 넘긴다. */
	thread_block();
	/* 깨어나서 다시 실행되면 인터럽트를 원래 상태로 복구(ON) */
	intr_set_level(old_level);
}

/* Suspends execution for approximately MS milliseconds. */
void timer_msleep(int64_t ms)
{
	real_time_sleep(ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void timer_usleep(int64_t us)
{
	real_time_sleep(us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void timer_nsleep(int64_t ns)
{
	real_time_sleep(ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void timer_print_stats(void)
{
	printf("Timer: %" PRId64 " ticks\n", timer_ticks());
}

/* Timer interrupt handler. */
static void
timer_interrupt(struct intr_frame *args UNUSED)
{
	/* 잠든 리스트의 처음을 e로 정의한다. */
	struct list_elem *e = list_begin(&sleep_list);
	/* 틱 하나씩 늘린다. */
	ticks++;
	/* 현재 시각을 구한다. */
	thread_tick();
	/* 잠든 리스트에서 리스트의 끝이 되지 않을 때까지 반복한다. */
	while (e != list_end(&sleep_list))
	{
		/* elem으로 스레드의 주소를 찾아야 깨울 시각을 알 수 있다. */
		struct thread *t = list_entry(e, struct thread, elem);
		/* 현재 시간(ticks)가 현재 스레드의 깨워야할 시각 이상이면 */
		if (ticks >= t->wakeup_tick)
		{
			/* 현재를 지우고 다음 리스트를 돌려준다. */
			e = list_remove(e);
			/* 위에서 t로 정의를 했기 때문에 안전함. */
			/* 현재 틱이 깨울시각 이상이면 thread_unblock()을 통해 레디리스트에 넣는다 */
			thread_unblock(t);
		}
		else
			/* 리스트의 다음을 알려준다. */
			e = list_next(e);
	}
}

/* Returns true if LOOPS iterations waits for more than one timer
	 tick, otherwise false. */
static bool
too_many_loops(unsigned loops)
{
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait(loops);

	/* If the tick count changed, we iterated too long. */
	barrier();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
	 brief delays.

	 Marked NO_INLINE because code alignment can significantly
	 affect timings, so that if this function was inlined
	 differently in different places the results would be difficult
	 to predict. */
static void NO_INLINE
busy_wait(int64_t loops)
{
	while (loops-- > 0)
		barrier();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep(int64_t num, int32_t denom)
{
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

		 (NUM / DENOM) s
		 ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
		 1 s / TIMER_FREQ ticks
		 */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT(intr_get_level() == INTR_ON);
	if (ticks > 0)
	{
		/* We're waiting for at least one full timer tick.  Use
			 timer_sleep() because it will yield the CPU to other
			 processes. */
		timer_sleep(ticks);
	}
	else
	{
		/* Otherwise, use a busy-wait loop for more accurate
			 sub-tick timing.  We scale the numerator and denominator
			 down by 1000 to avoid the possibility of overflow. */
		ASSERT(denom % 1000 == 0);
		busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
