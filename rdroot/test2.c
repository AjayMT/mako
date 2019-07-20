
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <mako.h>

void thread_func()
{
  while (1) {
    printf("child\n");
    msleep(400);
  }
}

int main(int argc, char const *argv[])
{
  thread(thread_func);

  while (1) {
    printf("parent\n");
    msleep(1000);
  }

  return 0;
}
