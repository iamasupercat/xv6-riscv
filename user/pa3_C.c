#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

void
fail(char *msg)
{
  printf("mmaptest: FAILED. %s\n", msg);
  exit(1);
}

void
ok(char *msg)
{
  printf("mmaptest: OK. %s\n", msg);
}

int
main(int argc, char *argv[])
{
  char *addr;
  int fd;
  int initial_free;
  char buf[1];
  int pid;
  int status;

  printf("mmaptest starting\n");

  // Test 1: Anonymous mapping (lazy allocation)
  initial_free = freemem();
  addr = (char *)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
  if (addr == (char *)-1)
    fail("mmap failed for lazy anonymous");
  if (freemem() != initial_free)
    fail("lazy anonymous mmap allocated memory prematurely");
  
  addr[0] = 'a'; // Trigger page fault
  if (freemem() >= initial_free)
    fail("page fault did not allocate any page");
  if (addr[0] != 'a')
    fail("read/write to mapped memory failed");
  
  munmap((uint64)addr);
  if (freemem() != initial_free)
    fail("munmap did not free memory");
  ok("Anonymous mapping (lazy allocation)");

  // Test 2: Anonymous mapping (populate)
  initial_free = freemem();
  addr = (char *)mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  if (addr == (char *)-1)
    fail("mmap failed for populate anonymous");
  if (freemem() > initial_free - 2)
    fail("populate anonymous mmap did not allocate memory correctly");
  
  addr[0] = 'b';
  addr[4096] = 'c';
  if (addr[0] != 'b' || addr[4096] != 'c')
    fail("read/write to populated mapped memory failed");
  
  munmap((uint64)addr);
  if (freemem() != initial_free)
    fail("munmap did not free memory after populate");
  ok("Anonymous mapping (populate)");

  // Test 3: File mapping (lazy allocation)
  if((fd = open("README", O_RDONLY)) < 0)
    fail("failed to open README");
  initial_free = freemem();
  addr = (char *)mmap(0, 4096, PROT_READ, 0, fd, 0);
  if (addr == (char *)-1)
    fail("mmap failed for lazy file");
  if (freemem() != initial_free)
    fail("lazy file mmap allocated memory prematurely");
  
  char first_char = addr[0]; // Trigger page fault
  if (freemem() >= initial_free)
    fail("page fault did not allocate any page for file map");
  
  lseek(fd, 0, 0); // seek back to start
  read(fd, buf, 1); // Read from file directly
  if (first_char != buf[0])
    fail("file content mismatch in lazy file map");
  
  munmap((uint64)addr);
  if (freemem() != initial_free)
    fail("munmap did not free memory after lazy file map");
  close(fd);
  ok("File mapping (lazy allocation)");

  // Test 4: File mapping (populate)
  if((fd = open("README", O_RDONLY)) < 0)
    fail("failed to open README");
  initial_free = freemem();
  addr = (char *)mmap(0, 4096, PROT_READ, MAP_POPULATE, fd, 0);
  if (addr == (char *)-1)
    fail("mmap failed for populate file");
  if (freemem() > initial_free - 1)
    fail("populate file mmap did not allocate memory correctly");
  
  lseek(fd, 0, 0);
  read(fd, buf, 1);
  if (addr[0] != buf[0])
    fail("file content mismatch in populate file map");
  
  munmap((uint64)addr);
  if (freemem() != initial_free)
    fail("munmap did not free memory after populate file map");
  close(fd);
  ok("File mapping (populate)");

  // Test 5: Fork test
  if((fd = open("README", O_RDONLY)) < 0)
    fail("failed to open README for fork test");
  addr = (char *)mmap(0, 4096, PROT_READ, MAP_POPULATE, fd, 0);
  if (addr == (char *)-1)
    fail("mmap failed for fork test");
  
  if ((pid = fork()) < 0)
    fail("fork failed");
  
  if (pid == 0) { // Child
    lseek(fd, 0, 0);
    read(fd, buf, 1);
    if (addr[0] != buf[0]) {
      printf("mmaptest: FAILED. child: file content mismatch\n");
      exit(1);
    }
    exit(0);
  } else { // Parent
    wait(&status);
    if (status != 0)
      fail("child process failed in fork test");
    munmap((uint64)addr);
    close(fd);
  }
  ok("Fork test");

  // Test 6: Write to read-only mapping test
  if((fd = open("README", O_RDONLY)) < 0)
    fail("failed to open README for write test");
  addr = (char *)mmap(0, 4096, PROT_READ, 0, fd, 0);
  if (addr == (char *)-1)
    fail("mmap failed for write test");
  
  if ((pid = fork()) < 0)
    fail("fork failed for write test");
  
  if (pid == 0) { // Child
    // This should cause a page fault and the kernel should kill the process.
    addr[0] = 'X';
    printf("mmaptest: FAILED. write to read-only memory did not fail!\n");
    exit(1); // Should not reach here
  } else { // Parent
    wait(&status);
    if (status != -1)
      fail("child process was not killed for writing to read-only memory");
    munmap((uint64)addr);
    close(fd);
  }
  ok("Write to read-only mapping");

  printf("mmaptest: ALL TESTS PASSED\n");
  exit(0);
}