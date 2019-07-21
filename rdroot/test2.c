
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <mako.h>

void thread_func(void *num)
{
  uint32_t duration = *((uint32_t *)num);
  while (1) {
    printf("child\n");
    msleep(duration);
  }
}

int main(int argc, char const *argv[])
{
  uint32_t child_sleep = 400;
  thread(thread_func, &child_sleep);

  while (1) {
    printf("parent\n");
    msleep(1000);
  }

  return 0;
}
