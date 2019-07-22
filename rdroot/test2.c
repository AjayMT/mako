
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
  printf("argc %d argv[0] %s\n", argc, argv[0]);

  return 0;
}
