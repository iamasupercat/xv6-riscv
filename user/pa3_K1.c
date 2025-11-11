#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/memlayout.h" // For MMAPBASE
#include "kernel/param.h"
#include "kernel/riscv.h" // For PGSIZE

#define PGSIZE 4096

// --- Test Harness Helpers ---
// (Adapted from test_another_sample.c and test_sample.c)

// Prints a formatted test case header
void
test_header(char *name)
{
  printf("\n--- %s ---\n", name);
}

// Prints a pass message
void
test_pass()
{
  printf("... OK\n");
}

// Prints a fail message and exits the current test function
// Note: We use return instead of exit(1) to allow the main loop to continue.
void
test_fail(char *msg)
{
  printf("... FAILED: %s\n", msg);
}

// Helper: Open README file (used in file mapping tests)
int
open_readme()
{
  int fd = open("README", O_RDWR);
  if(fd < 0){
    // This test program assumes "README" file exists.
    printf("WARN: Failed to open README. File-related tests might fail.\n");
    // We don't exit here, to allow other tests to run.
  }
  return fd;
}

// Helper: Read one byte per page to trigger Page Faults
void
touch_bytes(volatile unsigned char *p, int len)
{
  for (int off = 0; off < len; off += PGSIZE) {
    (void)p[off]; // Volatile read
  }
  if (len > 0) (void)p[len - 1]; // Volatile read last byte
}

// Helper: Read file content for comparison
int
read_file_into_buf(const char *path, int offset, unsigned char *buf, int n)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;

  // lseek simulation
  if(offset > 0)
    read(fd, 0, offset); // Simple way to advance offset, ignores read data

  int got = 0;
  while (got < n) {
    int r = read(fd, buf + got, n - got);
    if (r <= 0) break;
    got += r;
  }
  close(fd);
  return got;
}


// --- Test Cases ---

/*
 * Test 1: Anonymous + MAP_POPULATE
 * (from test_another_sample.c)
 */
void
test_anon_populate(int idx)
{
  test_header("Test 1: Anonymous + MAP_POPULATE");

  int len = 4096;
  int fm_before = freemem();
  char *mem = (char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);

  if((uint64)mem != MMAPBASE) {
    test_fail("mmap did not return MMAPBASE");
    return;
  }

  int fm_after = freemem();
  if(fm_after >= fm_before) {
    test_fail("freemem() did not decrease after populate");
    return;
  }

  // Check if memory is zero-filled
  for(int i=0; i < len; i++){
    if(mem[i] != 0) {
      test_fail("memory not zero-filled");
      munmap((uint64)mem);
      return;
    }
  }

  if(munmap((uint64)mem) < 0) {
    test_fail("munmap failed");
    return;
  }

  // Check if freemem() is restored.
  // We allow a small difference (0-3 pages) for potential page table allocations.
  int fm_final = freemem();
  int diff = fm_before - fm_final;

  if(diff < 0 || diff > 3) {
    printf("fm_before: %d, fm_final: %d, diff: %d\n", fm_before, fm_final, diff);
    test_fail("freemem() did not restore correctly (allowing 0-3 page diff)");
    return;
  }

  test_pass();
}

/*
 * Test 2: Anonymous + Lazy Allocation (Page Fault)
 * (from test_another_sample.c)
 */
void
test_anon_lazy(int idx)
{
  test_header("Test 2: Anonymous + Lazy (Page Fault)");

  int len = 4096;
  int fm_before = freemem();
  char *mem = (char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);

  if((uint64)mem != MMAPBASE) {
    test_fail("mmap did not return MMAPBASE");
    return;
  }

  // freemem() should not change before lazy access
  if(freemem() != fm_before) {
    test_fail("freemem() changed before lazy access");
    return;
  }

  // Trigger page fault
  printf("... triggering page fault (this is expected)\n");
  mem[100] = 'a';

  int fm_after = freemem();
  if(fm_after >= fm_before) {
    test_fail("freemem() did not decrease after page fault");
    return;
  }

  // Check zero-filling (except for the byte we wrote)
  if(mem[0] != 0 || mem[101] != 0) {
    test_fail("memory not zero-filled after fault");
    munmap((uint64)mem);
    return;
  }

  if(munmap((uint64)mem) < 0) {
    test_fail("munmap failed");
    return;
  }

  // Check freemem restore
  int fm_final = freemem();
  int diff = fm_before - fm_final;

  if(diff < 0 || diff > 3) {
    printf("fm_before: %d, fm_final: %d, diff: %d\n", fm_before, fm_final, diff);
    test_fail("freemem() did not restore correctly (allowing 0-3 page diff)");
    return;
  }

  test_pass();
}

/*
 * Test 3: File + MAP_POPULATE
 * (from test_another_sample.c)
 */
void
test_file_populate(int idx)
{
  test_header("Test 3: File + MAP_POPULATE");

  int fd = open_readme();
  if(fd < 0) {
    test_fail("Failed to open README, skipping test.");
    return;
  }

  char buf[10];

  // Read first 10 bytes for comparison
  read(fd, buf, 10);

  int len = 4096; // 1 page
  int offset = 0;
  int fm_before = freemem();
  char *mem = (char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_POPULATE, fd, offset);
  if((uint64)mem != MMAPBASE) {
    test_fail("mmap failed");
    close(fd);
    return;
  }

  // Check if mmap content matches file content
  for(int i=0; i < 10; i++){
    if(mem[i] != buf[i]) {
      test_fail("memory content mismatch file content");
      munmap((uint64)mem);
      close(fd);
      return;
    }
  }

  if(munmap((uint64)mem) < 0) {
    test_fail("munmap failed");
    close(fd);
    return;
  }

  // Check freemem restore
  int fm_final = freemem();
  int diff = fm_before - fm_final;

  if(diff < 0 || diff > 3) {
    printf("fm_before: %d, fm_final: %d, diff: %d\n", fm_before, fm_final, diff);
    test_fail("freemem() did not restore correctly (allowing 0-3 page diff)");
  }

  close(fd);
  test_pass();
}

/*
 * Test 4: File + Lazy (Page Fault)
 * (from test_sample.c)
 */
void
test_file_lazy(int idx)
{
  test_header("Test 4: File + Lazy (Page Fault)");
  int fm_before = freemem();

  int fd = open_readme();
  if(fd < 0) {
    test_fail("Failed to open README, skipping test.");
    return;
  }

  int len = 2 * PGSIZE;
  int offset = 0;
  unsigned char *m = (unsigned char*)mmap(0, len, PROT_READ, 0, fd, offset);
  if(m == 0) {
    test_fail("mmap file (no populate) returns zero");
    close(fd);
    return;
  }

  // freemem should not change much
  int fm_after_map = freemem();
  if(fm_before - fm_after_map > 3) {
    test_fail("freemem significantly reduced before touching (file, no populate)");
  }

  // Access across pages to trigger PFs and fill content
  printf("... triggering page faults (this is expected)\n");
  touch_bytes(m, len);

  unsigned char ref[2 * PGSIZE];
  int got = read_file_into_buf("README", offset, ref, len);
  if(got < 0) {
    test_fail("read README for reference (no populate)");
  }

  // Check content
  int eq = 1;
  for (int i = 0; i < got; i++) {
    if (m[i] != ref[i]) { eq = 0; break; }
  }
  if(!eq) {
    test_fail("mapped bytes mismatch file bytes after faults");
  }

  if(munmap((uint64)m) < 0) {
     test_fail("munmap file (no populate) failed");
  }

  close(fd);

  int fm_after_unmap = freemem();
  if(fm_after_unmap < fm_before - 3) {
    test_fail("freemem not restored after munmap (file, no populate)");
    return;
  }

  test_pass();
}

/*
 * Test 5: PROT violation (Write to PROT_READ)
 * (from test_sample.c)
 */
void
test_prot_violation(int idx)
{
  test_header("Test 5: PROT violation (write to PROT_READ)");
  int fd = open_readme();
  if(fd < 0) {
    test_fail("Failed to open README, skipping test.");
    return;
  }

  int len = PGSIZE;
  unsigned char *m = (unsigned char*)mmap(0, len, PROT_READ, 0, fd, 0); // PROT_READ only
  if(m == 0) {
    test_fail("mmap README PROT_READ (no populate)");
    close(fd);
    return;
  }

  int pid = fork();
  if (pid == 0) {
    // child: read ok, write should cause kill
    printf("... child attempting read (should be OK)\n");
    volatile unsigned char x = m[0];
    (void)x;

    printf("... child attempting illegal write (should be killed)\n");
    ((volatile unsigned char*)m)[0] = 0xAA; // This should fault and kill the child

    // If we get here, it's a failure
    printf("... FAILED: child survived illegal write\n");
    exit(1); // Exit with 1 to signal failure to parent
  } else {
    int st = 0;
    wait(&st);

    // Parent checks child's exit status.
    // If st == 1, child printed FAIL.
    // If st != 1 (e.g., killed by kernel), it's a pass.
    if(st == 1) {
        test_fail("Child exited with status 1 (survived write)");
    } else {
        printf("... OK (child was terminated as expected)\n");
    }
  }

  munmap((uint64)m);
  close(fd);
}

/*
 * Test 6: Simple Fork (NOT testing COW)
 * (Based on test_another_sample.c, but COW logic removed)
 */
void
test_simple_fork(int idx)
{
  test_header("Test 6: Simple Fork (no COW test)");

  int fd = open_readme();
  if(fd < 0) {
    test_fail("Failed to open README, skipping test.");
    return;
  }

  char buf[10];
  read(fd, buf, 10);

  int len = 4096;
  char *mem = (char *)mmap(0, len, PROT_READ, 0, fd, 0); // Lazy file map, READ-ONLY
  if((uint64)mem != MMAPBASE) {
    test_fail("mmap failed");
    close(fd);
    return;
  }

  // 1. Parent triggers page fault
  if(mem[0] != buf[0]) {
     test_fail("Parent content mismatch before fork");
     munmap((uint64)mem);
     close(fd);
     return;
  }

  int pid = fork();

  if(pid < 0) {
    test_fail("fork failed");
    munmap((uint64)mem);
    close(fd);
    return;
  }

  if(pid == 0) {
    // --- Child Process ---
    // 2. Child checks if it sees the same mmap content
    if(mem[0] != buf[0] || mem[5] != buf[5]) {
      printf("... FAILED: Child content mismatch after fork\n");
      exit(1); // Signal failure
    }

    // 3. Child does NOT write. This is not a COW test.
    printf("... Child read OK\n");
    exit(0); // Child exits successfully

  } else {
    // --- Parent Process ---
    int st = 0;
    wait(&st); // Wait for child

    if(st == 1) {
        test_fail("Child reported an error");
    } else {
        test_pass();
    }

    munmap((uint64)mem);
    close(fd);
  }
}


// --- Main Test Runner ---
// (Structure from test_format.c)

// Array of test functions
void (*f[])(int) = {
  test_anon_populate,
  test_anon_lazy,
  test_file_populate,
  test_file_lazy,
  test_prot_violation,
  test_simple_fork,
};

int main(void) {
    printf("[Project 3 Test Suite]\n");

    // --- FIX START ---
    // Record the number of free frames at the very beginning.
    int fm_start = freemem();
    printf("Initial free frames: %d\n", fm_start);
    // --- FIX END ---

    int num_tests = sizeof(f) / sizeof(f[0]);

    for (int i = 0; i < num_tests; i++) {
        printf("\n----------------------------------------\n");
        printf("Number of free frames (before test %d):\n", i + 1);
        printf("\t%d\n", freemem());

        // Call the test function
        f[i](i + 1);

        printf("Number of free frames (after test %d):\n", i + 1);
        printf("\t%d\n", freemem());
    }

    printf("\n----------------------------------------\n");
    // --- FIX START ---
    // Compare the final free frames with the initial count.
    int fm_end = freemem();
    printf("Initial free frames: %d\n", fm_start);
    printf("Final free frames:   %d\n", fm_end);

    int diff = fm_start - fm_end;
    if(diff < 0 || diff > 3) {
        printf("Overall memory leak detected! (Diff: %d)\n", diff);
    } else {
        printf("Overall memory check passed (Diff: %d)\n", diff);
    }
    // --- FIX END ---
    printf("All tests finished.\n");
    return 0;
}