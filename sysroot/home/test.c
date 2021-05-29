
int write(int fd, char *buf, int len);

int main()
{
  write(1, "hello", 5);
  return 0;
}
