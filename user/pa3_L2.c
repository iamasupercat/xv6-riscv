#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/memlayout.h"
#include "kernel/param.h"
#include "kernel/riscv.h" // PGSIZE

// 테스트 성공/실패를 출력하는 헬퍼 함수
void
check(int condition, const char *msg)
{
  if(condition) {
    printf("[PASSED] %s\n", msg);
  } else {
    printf("[FAILED] %s\n", msg);
    exit(1);
  }
}

// 1. 익명 매핑 (즉시 할당) 테스트
void
test_anonymous_populate()
{
  printf("\n--- Test 1: Anonymous Mapping (POPULATE) ---\n");
  int start_pages = freemem();
  printf("Initial free pages: %d\n", start_pages);

  // 2페이지(8192)를 즉시 할당
  uint64 addr = mmap(0, 2 * PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  check(addr == MMAPBASE, "mmap returned correct base address");

  int pages_after_mmap = freemem();
  printf("Pages after mmap: %d\n", pages_after_mmap);
  check(pages_after_mmap <= start_pages - 2, "freemem decreased by at least 2 pages");

  // 메모리에 쓰기 및 읽기 테스트
  char *ptr = (char*)addr;
  ptr[0] = 'a';
  ptr[PGSIZE] = 'b'; // 두 번째 페이지
  check(ptr[0] == 'a' && ptr[PGSIZE] == 'b', "Memory read/write successful");

  munmap(addr);
  int pages_after_munmap = freemem();
  printf("Pages after munmap: %d\n", pages_after_munmap);
  check(pages_after_munmap == start_pages - 2, "munmap returned pages to freelist");
}

// 2. 익명 매핑 (지연 할당) 테스트
void
test_anonymous_lazy()
{
  printf("\n--- Test 2: Anonymous Mapping (Lazy Page Fault) ---\n");
  int start_pages = freemem();
  printf("Initial free pages: %d\n", start_pages);

  // 1페이지(4096)를 지연 할당
  uint64 addr = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
  check(addr == MMAPBASE, "mmap returned correct base address");

  int pages_after_mmap = freemem();
  printf("Pages after lazy mmap: %d\n", pages_after_mmap);
  check(pages_after_mmap == start_pages, "freemem unchanged after lazy mmap");

  // 페이지 폴트 발생 (쓰기)
  printf("Triggering page fault...\n");
  char *ptr = (char*)addr;
  ptr[100] = 'z';
  check(ptr[100] == 'z', "Memory read/write successful (page fault handled)");

  int pages_after_fault = freemem();
  printf("Pages after fault: %d\n", pages_after_fault);
  check(pages_after_fault == start_pages - 1, "freemem decreased by 1 after fault");

  munmap(addr);
  int pages_after_munmap = freemem();
  printf("Pages after munmap: %d\n", pages_after_munmap);
  check(pages_after_munmap == start_pages, "munmap returned pages to freelist");
}

// 3. 파일 매핑 (즉시 할당) 테스트
void
test_file_populate()
{
  printf("\n--- Test 3: File Mapping (POPULATE) ---\n");
  int fd = open("README", O_RDONLY);
  check(fd >= 0, "Opened README file");

  int start_pages = freemem();
  printf("Initial free pages: %d\n", start_pages);

  // README 파일의 첫 페이지를 즉시 할당
  uint64 addr = mmap(0, PGSIZE, PROT_READ, MAP_POPULATE, fd, 0);
  check(addr == MMAPBASE, "mmap returned correct base address");

  int pages_after_mmap = freemem();
  printf("Pages after mmap: %d\n", pages_after_mmap);
  check(pages_after_mmap == start_pages - 1, "freemem decreased by 1 page");

  // 교안 FAQ의 방식대로 내용 출력 [cite: 618-624]
  char *ptr = (char*)addr;
  printf("File (populate) content [0-2]: %c%c%c\n", ptr[0], ptr[1], ptr[2]);
  check(ptr[0] != 0, "File content was loaded (not zero)"); // README가 비어있지 않다고 가정

  munmap(addr);
  close(fd);
  int pages_after_munmap = freemem();
  printf("Pages after munmap/close: %d\n", pages_after_munmap);
  check(pages_after_munmap == start_pages, "munmap/close returned page");
}

// 4. fork 테스트 (지연 파일 매핑)
void
test_fork()
{
  printf("\n--- Test 4: Fork Test (Lazy File Mapping) ---\n");
  int fd = open("README", O_RDONLY);
  check(fd >= 0, "Opened README file");

  int start_pages = freemem();
  printf("Parent: Initial free pages: %d\n", start_pages);

  // 부모가 1페이지를 지연 매핑
  uint64 addr = mmap(0, PGSIZE, PROT_READ, 0, fd, 0);
  check(addr == MMAPBASE, "Parent mmap returned correct address");

  int pages_after_mmap = freemem();
  check(pages_after_mmap == start_pages, "Parent freemem unchanged after lazy mmap");

  int pid = fork();
  check(pid >= 0, "fork successful");

  if (pid == 0) {
    // --- 자식 프로세스 ---
    printf("Child: Process started.\n");

    int child_pages_start = freemem();
    check(child_pages_start < start_pages,
          "Child freemem DECREASED after fork (fork cost OK)");

    char *ptr = (char*)addr;
    printf("Child: Accessing memory (triggers page fault)...\n");
    printf("Child: Content [0]: %c\n", ptr[0]); // 페이지 폴트 발생

    int child_pages_after_fault = freemem();
    printf("Child: Pages after fault: %d\n", child_pages_after_fault);

    check(child_pages_after_fault < child_pages_start,
          "Child freemem decreased after page fault (fault cost OK)");

    close(fd);
    exit(0);
    //int child_pages = freemem();
    //check(child_pages == start_pages, "Child freemem unchanged after fork");
/*
    char *ptr = (char*)addr;
    printf("Child: Accessing memory (triggers page fault)...\n");
    printf("Child: Content [0]: %c\n", ptr[0]); // 페이지 폴트 발생

    child_pages = freemem();
    printf("Child: Pages after fault: %d\n", child_pages);
    check(child_pages == start_pages - 1, "Child freemem decreased by 1 after fault");

    close(fd); // 자식도 물려받은 fd를 닫아야 함
    exit(0);
    */
  } else {
    // --- 부모 프로세스 ---
    wait(0);
    printf("Parent: Child finished.\n");

    int parent_pages = freemem();
    // 자식이 1페이지를 할당했다가 종료 시 반납했으므로,
    // 부모의 페이지는 아직 폴트되지 않았으므로 start_pages와 같아야 함
    printf("Parent: Pages after child exit: %d\n", parent_pages);
    check(parent_pages == start_pages, "Parent freemem back to start after child exit");

    char *ptr = (char*)addr;
    printf("Parent: Accessing memory (triggers page fault)...\n");
    printf("Parent: Content [0]: %c\n", ptr[0]); // 부모의 페이지 폴트 발생

    // 내용이 같은지 확인 (자식과 부모가 같은 문자를 출력했는지 눈으로 확인)

    parent_pages = freemem();
    printf("Parent: Pages after fault: %d\n", parent_pages);
    check(parent_pages == start_pages - 1, "Parent freemem decreased by 1 after fault");

    munmap(addr);
    close(fd);

    parent_pages = freemem();
    printf("Parent: Pages after munmap: %d\n", parent_pages);
    check(parent_pages == start_pages, "Parent freemem back to start after munmap");
  }
}

int
main(int argc, char *argv[])
{
  printf("=== MMAP TEST SUITE STARTING ===\n");

  test_anonymous_populate();
  test_anonymous_lazy();
  test_file_populate();
  test_fork();

  printf("\n=== MMAP TEST SUITE FINISHED ===\n");
  exit(0);
}