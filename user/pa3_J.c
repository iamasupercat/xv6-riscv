// mmap_tests.c — SWE3004 Project 3 test program
// Add to Makefile UPROGS: ... _mmap_tests
// Then run in xv6 shell: mmap_tests

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// ---- Assignment-specified constants (match /kernel/param.h) ----
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_ANONYMOUS 0x1
#define MAP_POPULATE  0x2

#ifndef PGSIZE
#define PGSIZE 4096
#endif

// syscalls that the project adds (ensure user.h has prototypes; if not, declare)
uint64 mmap(uint64 addr, int length, int prot, int flags, int fd, int offset);
int    munmap(uint64 addr);
int    freemem(void);

// ---------- tiny test harness ----------
static int failures = 0;
#define TASSERT(cond, msg) do { \
  if(!(cond)) { \
    printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
    failures++; \
  } else { \
     printf("  [PASS] %s (line %d)\n", msg, __LINE__);  \
  } \
} while(0)

static void print_case(const char *name) {
  printf("\n=== %s ===\n", name);
}

static uint64 checksum_u8(const unsigned char *p, int n) {
  uint64 s = 0;
  for (int i = 0; i < n; i++) s = (s * 1315423911ULL) ^ p[i];
  return s;
}

static void touch_bytes(volatile unsigned char *p, int len) {
  // read 1 byte per page (and also last byte) to trigger PF on demand-paged regions
  for (int off = 0; off < len; off += PGSIZE) {
    (void)p[off];
  }
  if (len > 0) (void)p[len - 1];
}

// --------- 1) Anonymous mapping with MAP_POPULATE ----------
static void test_anon_populate(void) {
  print_case("Anon mapping (MAP_POPULATE)");
  int len = 2 * PGSIZE;

  printf("\nbefore prealloc: %d\n", freemem());
  unsigned char *b = (unsigned char*)mmap(0, len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0); // preallocate for PT
  munmap((uint64)b);

  int before = freemem();
  unsigned char *a = (unsigned char*)mmap(0, len, PROT_READ | PROT_WRITE,
                                          MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  TASSERT(a != 0, "mmap anon+populate returns non-zero");

  // newly-populated anon pages should be zero-filled
  int zeros = 1;
  for (int i = 0; i < len; i++) {
    if (a[i] != 0) { zeros = 0; break; }
  }
  TASSERT(zeros, "anon populated pages are zero-initialized");

  // write something and re-check readable
  for (int i = 0; i < len; i++) a[i] = (unsigned char)(i & 0xFF);
  TASSERT(a[len - 1] == (unsigned char)((len - 1) & 0xFF), "write+read OK");

  int after_map = freemem();
  // Expect free pages decreased by >= #pages (at least 2). Page table pages may affect counts.
  TASSERT(before - after_map >= (len / PGSIZE), "freemem decreased by >= mapped pages");

  TASSERT(munmap((uint64)a) == 1, "munmap anon+populate returns 1");

  int after_unmap = freemem();
  printf("\nfreemem(before): %d\n", before);
  printf("\nfreemem(after_map): %d\n", after_map);
  printf("\nfreemem(after_unmap): %d\n", after_unmap);
  // After unmap, free pages should go back to at least 'before'
  TASSERT(after_unmap >= before, "freemem restored after munmap");
}

// --------- 2) Anonymous mapping without MAP_POPULATE (demand paging) ----------
static void test_anon_nopopulate(void) {
  print_case("Anon mapping (no MAP_POPULATE, demand paging)");
  int before = freemem();
  int len = 2 * PGSIZE;

  unsigned char *a = (unsigned char*)mmap(0, len, PROT_READ | PROT_WRITE,
                                          MAP_ANONYMOUS, -1, 0);
  TASSERT(a != 0, "mmap anon (no populate) returns non-zero");

  int after_map = freemem();
  // Shouldn't have allocated data pages yet
  TASSERT(after_map >= before - 2, "freemem not significantly reduced before touching (no populate)");

  // Touch one byte per page to trigger PF and allocate pages lazily
  touch_bytes(a, len);

  // Now write to confirm RW
  for (int i = 0; i < len; i += 97)
    a[i] = (unsigned char)(i & 0xFF);

  int after_touch = freemem();
  TASSERT(before - after_touch >= (len / PGSIZE), "freemem decreased after page faults");

  TASSERT(munmap((uint64)a+1) == 0, "munmap anon (no populate) returns 1"); //modified
  TASSERT(munmap((uint64)a) == 1, "munmap anon (no populate) returns 1");

  int after_unmap = freemem();
  printf("\nfreemem(before): %d\n", before);
  printf("\nfreemem(after_map): %d\n", after_map);
  printf("\nfreemem(after_touch): %d\n", after_touch);
  printf("\nfreemem(after_unmap): %d\n", after_unmap);
  TASSERT(after_unmap >= before, "freemem restored after munmap (anon, no populate)");
}

// --------- Helper: read README into buffer via read() for comparison ----------
static int read_file_into_buf(const char *path, int offset, unsigned char *buf, int n) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  // simulate offset
  int skipped = 0;
  unsigned char tmp[256];
  while (skipped < offset) {
    int need = offset - skipped;
    int take = (need > (int)sizeof(tmp)) ? (int)sizeof(tmp) : need;
    int r = read(fd, tmp, take);
    if (r <= 0) { close(fd); return -1; }
    skipped += r;
  }
  int got = 0;
  while (got < n) {
    int r = read(fd, buf + got, n - got);
    if (r <= 0) break;
    got += r;
  }
  close(fd);
  return got;
}

// --------- 3) File mapping with MAP_POPULATE ----------
static void test_file_populate(void) {
  print_case("File mapping (MAP_POPULATE, PROT_READ)");
  int before = freemem();

  int fd = open("README", O_RDONLY);
  TASSERT(fd >= 0, "open README");

  int len = 2 * PGSIZE;
  int offset = 0; // could also test non-zero (e.g., 4096) if README is big
  unsigned char *m = (unsigned char*)mmap(0, len, PROT_READ,
                                          MAP_POPULATE, fd, offset);
  TASSERT(m != 0, "mmap file+populate returns non-zero");

  // Compare mapped content with read()
  unsigned char ref[2 * PGSIZE];
  int got = read_file_into_buf("README", offset, ref, len);
  TASSERT(got >= 0, "read README for reference");
  if (got > len) got = len;

  int eq = 1;
  for (int i = 0; i < got; i++) {
    if (m[i] != ref[i]) { eq = 0; break; }
  }
  TASSERT(eq, "mapped bytes equal to file bytes (populate)");

  int after_map = freemem();
  TASSERT(before - after_map >= (len / PGSIZE), "freemem decreased by >= mapped pages (file+populate)");

  TASSERT(munmap((uint64)m) == 1, "munmap file+populate returns 1");
  close(fd);

  int after_unmap = freemem();
  TASSERT(after_unmap >= before, "freemem restored after munmap (file+populate)");
  printf("\nfreemem(before): %d\n", before);
  printf("\nfreemem(after_touch): %d\n", after_map);
  printf("\nfreemem(after_unmap): %d\n", after_unmap);
}

// --------- 4) File mapping without MAP_POPULATE (demand paging) ----------
static void test_file_nopopulate_pf(void) {
  print_case("File mapping (no MAP_POPULATE, demand paging)");
  int before = freemem();

  int fd = open("README", O_RDONLY);
  TASSERT(fd >= 0, "open README");

  int len = 2 * PGSIZE;
  int offset = 0;
  unsigned char *m = (unsigned char*)mmap(0, len, PROT_READ, 0, fd, offset);
  TASSERT(m != 0, "mmap file (no populate) returns non-zero");

  int after_map = freemem();
  TASSERT(after_map >= before - 2, "freemem not significantly reduced before touching (file, no populate)");

  // Access across pages to trigger PFs and fill content
  touch_bytes(m, len);

  unsigned char ref[2 * PGSIZE];
  int got = read_file_into_buf("README", offset, ref, len);
  TASSERT(got >= 0, "read README for reference (no populate)");

  int eq = 1;
  for (int i = 0; i < got; i++) {
    if (m[i] != ref[i]) { eq = 0; break; }
  }
  TASSERT(eq, "mapped bytes equal to file bytes after faults");

  TASSERT(munmap((uint64)m) == 1, "munmap file (no populate) returns 1");
  close(fd);

  int after_unmap = freemem();
  TASSERT(after_unmap >= before, "freemem restored after munmap (file, no populate)");
  printf("\nfreemem(before): %d\n", before);
  printf("\nfreemem(after_touch): %d\n", after_map);
  printf("\nfreemem(after_unmap): %d\n", after_unmap);
}

// --------- 5) PROT violation should kill child on write ----------
static void test_prot_violation_kills_child(void) {
  print_case("PROT violation (write to PROT_READ mapping) kills child");
  int fd = open("README", O_RDONLY);
  TASSERT(fd >= 0, "open README");

  int len = PGSIZE;
  unsigned char *m = (unsigned char*)mmap(0, len, PROT_READ, 0, fd, 0);
  TASSERT(m != 0, "mmap README PROT_READ (no populate)");

  int pid = fork();
  if (pid == 0) {
    // child: read ok, write should cause kill (page fault handler returns -1 => kill)
    volatile unsigned char x = m[0];
    (void)x;
    // attempt to write -> should fault
    ((volatile unsigned char*)m)[0] = 0xAA;
    // if we get here, it's a failure
    printf("  [FAIL] child survived illegal write\n");
    exit(1);
  } else {
    int st = 0;
    wait(&st);
    // On xv6, killed children typically don't report a conventional exit code;
    // We treat "didn't print FAIL" as PASS if parent continues running.
    // If child exited(1), we recorded failure in child.
    TASSERT(st != 1, "child did not exit(1) (we treat kill as PASS)"); //TASSERT(st == 0, "child did not exit normally with status 1");
  }

  TASSERT(munmap((uint64)m) == 1, "munmap after PROT test");
  close(fd);
}

// --------- 6) fork: contents must be same; freemem behavior around fork ----------
static void test_fork_filemap_contents_and_freemem(void) {
  print_case("fork: mapped contents equal; freemem changes around child faults");

  int base = freemem();

  int fd = open("README", O_RDONLY);
  TASSERT(fd >= 0, "open README");
  int len = 2 * PGSIZE;
  unsigned char *m = (unsigned char*)mmap(0, len, PROT_READ, 0, fd, 0);
  TASSERT(m != 0, "parent mmap (file, no populate)");

  // compute parent checksum (touch to fault some pages too)
  touch_bytes(m, len);
  uint64 parent_ck = checksum_u8(m, len);
  int pre_fork = freemem();

  int p[2];
  pipe(p);

  int pid = fork();
  if (pid == 0) {
    // child: should see identical contents
    touch_bytes(m, len); // trigger PFs in child if needed
    uint64 child_ck = checksum_u8(m, len);
    write(p[1], &child_ck, sizeof(child_ck));
    close(p[0]); close(p[1]);
    exit(0);
  } else {
    close(p[1]);
    uint64 child_ck = 0;
    int r = read(p[0], &child_ck, sizeof(child_ck));
    close(p[0]);
    int st = 0;
    wait(&st);
    printf("\n r: %d, child_ck: %ld \n", r, sizeof(child_ck));
    TASSERT(r == sizeof(child_ck), "parent received child checksum");
    printf("\n child_ck: %ld, parent_ck: %ld \n", child_ck, parent_ck);
    TASSERT(child_ck == parent_ck, "parent/child mapped contents equal");

    // After child touched pages, system free pages likely decreased,
    // but after child exit, they should be restored to (about) pre_fork level.
    int post_wait = freemem();
    TASSERT(post_wait >= pre_fork, "freemem restored after child exit");
  }

  TASSERT(munmap((uint64)m) == 1, "parent munmap after fork test");
  close(fd);

  int end = freemem();
  TASSERT(end >= base, "freemem finally restored after fork-test cleanup");
  printf("\nfreemem(base): %d\n", base);
  printf("\nfreemem(pre_fork): %d\n", pre_fork);
//  printf("\nfreemem(post_wait): %d\n", post_wait);
  printf("\nfreemem(end): %d\n", end);
}

int
main(void)
{
  printf("mmap_tests: start\n");

  test_anon_populate();
  test_anon_nopopulate();
  test_file_populate();
  test_file_nopopulate_pf();
  test_prot_violation_kills_child();
  test_fork_filemap_contents_and_freemem();

  if (failures == 0) {
    printf("\nALL TESTS PASS \\o/\n");
    exit(0);
  } else {
    printf("\nTESTS FAILED: %d case(s)\n", failures);
    exit(1);
  }
}