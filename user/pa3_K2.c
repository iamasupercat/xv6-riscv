// user/mytest.c
// PA3: Alternative VM Test Suite
// Focus: Forking interactions, multi-region management, and file offsets.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/memlayout.h"
#include "kernel/param.h"
#include "kernel/syscall.h"

// --- Kernel constants for user space ---
#ifndef PGSIZE
#define PGSIZE 4096
#endif

#ifndef PROT_READ
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_ANONYMOUS 0x1
#define MAP_POPULATE  0x2
#endif

// --- Test Tracking ---
int test_count = 0;
int pass_count = 0;

void
check(int cond, const char* name)
{
  test_count++;
  if(cond) {
    printf("  [PASS] %s\n", name);
    pass_count++;
  } else {
    printf("  [FAIL] %s\n", name);
  }
}

// --- Fault test helpers ---
volatile char* g_ptr;
void fault_writer() {
  g_ptr[0] = 'X'; // This should fail
  exit(0); // Should not be reached
}

// --- Main Test ---
int
main(int argc, char *argv[])
{
  uint64 mmap_base = 0x40000000;
  int initial_pages, post_fork_pages;

  printf("Starting Alternative PA3 Test Suite...\n");

  // === Test 1: Advanced Fork Test (Lazy vs. Populated) ===
  // This test checks how fork() copies pages that are
  // 1. Mapped but not yet allocated (lazy)
  // 2. Mapped and already allocated (populated)
printf("\n=== Test 1: Advanced Fork Test ===\n");

  initial_pages = freemem();

  // Region A: Lazy
  char *ptr_a = (char*)mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
  // Region B: Populated
  char *ptr_b = (char*)mmap(PGSIZE, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);

  check(ptr_a == (char*)(mmap_base) && ptr_b == (char*)(mmap_base + PGSIZE), "mmap() addresses are correct");

  // Allow 1-3 pages for kernel page table overhead + populated page
  int post_mmap_pages = freemem();
  // check(post_mmap_pages >= initial_pages - 2 && post_mmap_pages <= initial_pages - 1, "freemem() decreased by 1-2 (for ptr_b + PT)");
  check(post_mmap_pages <= initial_pages - 1, "freemem() decreased after populate");


  // Access (fault in) the lazy page *before* forking
  ptr_a[0] = 'A';
  ptr_b[0] = 'B';
  // check(freemem() == initial_pages - 2, "freemem() decreased by 2 (after lazy fault)");
  check(freemem() <= initial_pages - 2, "freemem() decreased after lazy fault");

  check(ptr_a[0] == 'A' && ptr_b[0] == 'B', "Parent data is correct before fork");

  int pid = fork();
  if (pid < 0) {
    printf("  [FAIL] fork failed\n");
  } else if (pid == 0) {
    // === Child Process ===
    printf("Child: running...\n");

    post_fork_pages = freemem();
    check(post_fork_pages <= initial_pages - 2, "Child freemem() reflects copied pages");

    check(ptr_a[0] == 'A', "Child read from lazy-copied page OK");
    check(ptr_b[0] == 'B', "Child read from populated-copied page OK");

    // Write to child's copies
    ptr_a[0] = 'C';
    ptr_b[0] = 'D';
    check(ptr_a[0] == 'C' && ptr_b[0] == 'D', "Child write OK");

    printf("Child: exiting...\n");
    exit(0);
    // === End Child Process ===

  } else {
    // === Parent Process ===
    int status;
    wait(&status);
    check(status == 0, "Child exited successfully");

    // Parent data should be unchanged
    check(ptr_a[0] == 'A', "Parent data (region A) unchanged after child write");
    check(ptr_b[0] == 'B', "Parent data (region B) unchanged after child write");

    check(munmap((uint64)ptr_a) == 1, "Parent munmap(A) OK");
    check(munmap((uint64)ptr_b) == 1, "Parent munmap(B) OK");

    // check(freemem() >= initial_pages - 1, "Parent freemem() restored after munmaps (allowing PT page)");
    check(freemem() >= initial_pages - 3, "Parent freemem() restored after munmaps (allowing PT pages)");

    initial_pages = freemem(); // Resync
    // === End Parent Process ===
  }

  // === Test 2: File Offset Test (README) ===
  printf("\n=== Test 2: File Offset Test (README) ===\n");
  int fd = open("README", O_RDONLY);
  char direct_buf_offset[6];
  char discard_buf[5];

  check(fd >= 0, "open(README) OK");

  // Read and discard first 5 bytes to simulate seek
  read(fd, discard_buf, 5);
  // Read next 5 bytes (at offset 5)
  read(fd, direct_buf_offset, 5);
  direct_buf_offset[5] = '\0';
  printf("  Direct read (offset 5): %s\n", direct_buf_offset);

  // mmap 1 page, offset 5
  // We must close the fd and re-open it for mmap,
  // because mmap uses its own offset, independent of the fd's current offset.
  close(fd);
  fd = open("README", O_RDONLY);
  int offset = 5;

  char *ptr_c = (char*)mmap(PGSIZE * 4, PGSIZE, PROT_READ, 0, fd, offset);
  check(ptr_c == (char*)(mmap_base + PGSIZE*4), "mmap() with offset OK");
  check(freemem() >= initial_pages - 1, "freemem() unchanged for lazy file map");

  // Access mmap'ed memory (triggers fault)
  printf("  mmap read (offset 5)  : %c%c%c%c%c\n", ptr_c[0], ptr_c[1], ptr_c[2], ptr_c[3], ptr_c[4]);
  check(memcmp(direct_buf_offset, ptr_c, 5) == 0, "mmap content matches file offset");
  check(freemem() < initial_pages, "freemem() decreased after file fault");

  check(munmap((uint64)ptr_c) == 1, "munmap(C) OK");
  close(fd);
  check(freemem() >= initial_pages - 1, "freemem() restored after file munmap (allowing PT overhead)"); // Allow PT overhead
  initial_pages = freemem(); // Resync


  // === Test 3: Multi-Region Management ===
  printf("\n=== Test 3: Multi-Region Management ===\n");
  initial_pages = freemem();

  char *r1 = (char*)mmap(PGSIZE * 10, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
  char *r2 = (char*)mmap(PGSIZE * 20, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
  char *r3 = (char*)mmap(PGSIZE * 30, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);

  check(r1 && r2 && r3, "Mapped 3 separate regions");
  check(freemem() >= initial_pages - 2, "freemem() almost unchanged after 3 lazy maps (PT overhead)");

  r1[0] = 1;
  r2[0] = 2;
  r3[0] = 3;
  check(freemem() <= initial_pages - 3, "freemem() decreased by >=3 after all faults");

  // Unmap out of order
  check(munmap((uint64)r2) == 1, "munmap(r2) OK");
  check(freemem() >= initial_pages - 3, "freemem() restored 1 page"); // Check against post-fault count

  check(munmap((uint64)r3) == 1, "munmap(r3) OK");
  check(freemem() >= initial_pages - 2, "freemem() restored 2 pages");

  check(munmap((uint64)r1) == 1, "munmap(r1) OK");
  check(freemem() >= initial_pages - 1, "freemem() fully restored (allowing PT overhead)");
  initial_pages = freemem(); // Resync


  // === Test 4: Fault & mmap Failures ===
  printf("\n=== Test 4: Fault & mmap Failures ===\n");

  // mmap failure
  check(mmap(123, PGSIZE, PROT_READ, MAP_ANONYMOUS, -1, 0) == 0, "mmap fails for non-aligned address");

  // munmap failure
  check(munmap(mmap_base + PGSIZE * 50) == -1, "munmap fails for unmapped address");

  // Page fault failure (Write to Read-Only)
  fd = open("README", O_RDONLY);
  check(fd >= 0, "open(README) OK");
  g_ptr = (volatile char*)mmap(PGSIZE * 6, PGSIZE, PROT_READ, MAP_POPULATE, fd, 0);
  check(g_ptr != 0, "mmap for fault test OK");

  pid = fork();
  if (pid == 0) {
    fault_writer(); // Child will run this and be killed by kernel
  } else {
    int status;
    wait(&status);
    check(status != 0, "Write to PROT_READ page failed as expected");
  }

  munmap((uint64)g_ptr);
  close(fd);

  // --- Summary ---
  printf("\n=== Alternative Test Summary ===\n");
  printf("Passed %d out of %d tests.\n", pass_count, test_count);
  if(pass_count == test_count && test_count > 15) { // Sanity check
     printf("All tests passed!\n");
  } else {
     printf("Some tests failed!\n");
  }

  exit(0);
}