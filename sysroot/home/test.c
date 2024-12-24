
int write(int fd, char *buf, int len);

int main()
{
  write(1, "hello\n", 6);
  return 0;
}
