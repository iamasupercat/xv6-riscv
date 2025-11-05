// Required headers per spec
#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"
#include "../kernel/fcntl.h"
#include "../kernel/memlayout.h"
#include "../kernel/param.h"

#ifndef PGSZ
#define PGSZ 4096
#endif

static void assert(int cond, const char *msg) {
  if(!cond){
    printf("[FAIL] %s\n", msg);
    exit(1);
  }
}

static void touch_range(char *p, int len) {
  for(int i = 0; i < len; i += PGSZ)
    p[i] = (char)(i & 0x7f);
}

static void fill_pattern(char *p, int len, char base){
  for(int i=0;i<len;i+=PGSZ) p[i] = (char)(base + (i>>12));
}

static void check_pattern(char *p, int len, char base){
  for(int i=0;i<len;i+=PGSZ) assert(p[i] == (char)(base + (i>>12)), "pattern mismatch");
}

static void cmp_with_read(char *m, int fd, int n){
  char buf[PGSZ];
  int r = read(fd, buf, n);
  assert(r == n, "read n bytes");
  for(int i=0;i<n;i++)
    assert(m[i]==buf[i], "mapped content differs from file");
}

static void test_errors(void)
{
  // misaligned addr
  uint64 bad = mmap(1, PGSZ, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
  assert(bad == 0, "misaligned addr should fail");
  // invalid fd with file mapping
  uint64 badfd = mmap(0, PGSZ, PROT_READ, 0, -1, 0);
  assert(badfd == 0, "invalid fd should fail");
  // PROT_WRITE on read-only file
  int fd = open("README", O_RDONLY);
  assert(fd >= 0, "open README");
  uint64 wr = mmap(0, PGSZ, PROT_READ|PROT_WRITE, 0, fd, 0);
  assert(wr == 0, "PROT_WRITE on read-only file should fail");
  close(fd);
  printf("[PASS] error cases\n");
}

static void test_anon(int populate)
{
  int before = freemem();
  int pages = 2;
  uint64 addr = mmap(0, pages*PGSZ, PROT_READ|PROT_WRITE,
                     MAP_ANONYMOUS | (populate? MAP_POPULATE:0), -1, 0);
  assert(addr != 0, "anon mmap failed");
  char *p = (char*)addr;

  int mid = freemem();
  if(populate)
    assert(before - mid == pages, "populate consumes pages immediately");
  else
    assert(before == mid, "non-populate should not allocate yet");

  // touch first page only
  touch_range(p, 1*PGSZ);
  int after_touch = freemem();
  if(populate)
    assert(before - after_touch == pages, "populate: no extra alloc after touch");
  else
    assert(before - after_touch == 1, "lazy alloc one page after touch");

  // fork: child should see same content; modifications isolated
  fill_pattern(p, 1*PGSZ, 0x11);
  int pid = fork();
  if(pid == 0){
    check_pattern(p, 1*PGSZ, 0x11);
    fill_pattern(p, 1*PGSZ, 0x55);
    check_pattern(p, 1*PGSZ, 0x55);
    exit(0);
  }
  wait(0);
  check_pattern(p, 1*PGSZ, 0x11);

  int r = munmap(addr);
  assert(r == 1, "anon munmap failed");
  int after_free = freemem();
  assert(after_free == before, "anon munmap should restore freemem");
  printf("[PASS] anon %s-populate\n", populate? "":"non");
}

static void test_file(int populate)
{
  int fd = open("README", O_RDONLY);
  assert(fd >= 0, "open README");
  int before = freemem();
  uint64 addr = mmap(0, 2*PGSZ, PROT_READ,
                     (populate? MAP_POPULATE:0), fd, 0);
  assert(addr != 0, "file mmap failed");
  char *p = (char*)addr;

  if(!populate){
    int mid = freemem();
    assert(mid == before, "file non-populate should not allocate yet");
  }

  // trigger first page fault and compare
  cmp_with_read(p, fd, 64);
  int after_first = freemem();
  assert(before - after_first == 1, "first page alloc on demand");

  // second page access
  volatile char sink = p[PGSZ]; (void)sink;
  int after_second = freemem();
  assert(before - after_second == 2 || (populate && before - after_second == 2),
         "second page alloc after access");

  // fork: child sees same content
  int pid = fork();
  if(pid == 0){
    cmp_with_read(p, fd, 64);
    exit(0);
  }
  wait(0);

  int r = munmap(addr);
  assert(r == 1, "file munmap failed");
  int after_free = freemem();
  assert(after_free == before, "file munmap restore freemem");
  close(fd);
  printf("[PASS] file %s-populate\n", populate? "":"non");
}

int main(void)
{
  printf("mmap tests start\n");
  test_errors();
  test_anon(0);
  test_anon(1);
  test_file(0);
  test_file(1);
  printf("All mmap tests passed!\n");
  exit(0);
}


