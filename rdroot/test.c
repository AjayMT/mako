
int main(int argc, char *argv[])
{
  asm volatile ("int $0x80");
  return 0;
}
