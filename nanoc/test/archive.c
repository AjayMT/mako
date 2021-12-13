
extern int foo;

void _start(char *c);

int main(int a, int b, int c)
{
  foo = 1;
  _start("hello");
  return (int)_start;
}
