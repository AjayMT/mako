
// USTAR "disk image" generator
//
// Removes the leading directory names from all entries
// in a TAR file. For example:
//   sysroot/    -> /
//   sysroot/bin -> /bin

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct ustar_metadata_s {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char type;
  char linked_name[100];
  char ustar_magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char major[8];
  char minor[8];
  char prefix[155];
  char padding[12]; // to round size up to 512
} __attribute__((packed));
typedef struct ustar_metadata_s ustar_metadata_t;

#define BLOCK_SIZE 512

static inline uint32_t block_align_up(uint32_t n)
{
  if (n & (BLOCK_SIZE - 1))
    return n + BLOCK_SIZE - (n & (BLOCK_SIZE - 1));
  return n;
}

static uint32_t parse_oct(char *s, uint32_t size)
{
  uint32_t n = 0;
  for (uint32_t i = 0; i < size && s[i] && s[i] != ' '; ++i) {
    n <<= 3;
    n |= s[i] - '0';
  }
  return n;
}

int main(int argc, char *argv[])
{
  FILE *f = fopen(argv[1], "r+");
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);

  ustar_metadata_t data;
  while (1) {
    long off = ftell(f);
    if (off == len) break;
    size_t r = fread(&data, 1, sizeof(data), f);
    if (r != sizeof(data)) break;
    uint32_t file_size = block_align_up(parse_oct(data.size, sizeof(data.size)));
    char *split_pos = strchr(data.name, '/');
    char *new_name = strdup(split_pos);
    memset(data.name, 0, sizeof(data.name));
    memcpy(data.name, new_name, strlen(new_name) + 1);
    free(new_name);
    fseek(f, off, SEEK_SET);
    r = fwrite(&data, 1, sizeof(data), f);
    fseek(f, file_size, SEEK_CUR);
  }
  fclose(f);
  return 0;
}
