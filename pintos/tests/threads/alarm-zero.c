/* Tests timer_sleep(0), which should return immediately. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

void test_alarm_zero(void)
{
  /* 0이나 음수가 들어왔을때 예외처리를 해서 안전하게 넘어가라.  */
  timer_sleep(0);
  /* 여기까지 무사히 오면 PASS 출력. */
  pass();
}
