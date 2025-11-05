#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/memlayout.h"
#include "kernel/param.h"
#include "kernel/riscv.h"

int test_num = 1;
int fail_count = 0;

// 테스트 성공/실패를 출력하는 헬퍼 함수
void
check(int condition, const char *msg)
{
  if(condition) {
    printf("[Test %d PASSED] %s\n", test_num, msg);
  } else {
    printf("[Test %d FAILED] %s\n", test_num, msg);
    fail_count++;
  }
  test_num++;
}
// 1. 익명 + 즉시 할당 (성공)
void
test_anonymous_populate()                                               {                                                                         printf("\n--- 1. Test: Anonymous Mapping (POPULATE) ---\n");            int start_pages = freemem();                                            uint64 addr = mmap(0, 2 * PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);                                                 check(addr == MMAPBASE, "mmap returned correct base address");                                                                                  int pages_after_mmap = freemem();                                       check(pages_after_mmap <= start_pages - 2, "freemem decreased (data pages)");                                                                                                                                           char *ptr = (char*)addr;                                                ptr[0] = 'a';                                                           ptr[PGSIZE] = 'b';                                                      check(ptr[0] == 'a' && ptr[PGSIZE] == 'b', "Memory read/write successful");

  check(munmap(addr) == 1, "munmap successful");
  int pages_after_munmap = freemem();
  check(pages_after_munmap >= start_pages - 2, "munmap returned data pages");
  // 참고: 페이지 테이블 페이지는 회수되지 않을 수 있음 (정상)
}
// 2. 익명 + 지연 할당 (성공)
void
test_anonymous_lazy()
{
  printf("\n--- 2. Test: Anonymous Mapping (Lazy Page Fault) ---\n");
  int start_pages = freemem();
  uint64 addr = mmap(PGSIZE * 10, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0); // 다른 주소 사용
  check(addr == MMAPBASE + (PGSIZE * 10), "mmap returned correct address");

  int pages_after_mmap = freemem();
  check(pages_after_mmap == start_pages, "freemem unchanged after lazy mmap");

  printf("Triggering page fault...\n");
  char *ptr = (char*)addr;
  ptr[100] = 'z';
  check(ptr[100] == 'z', "Memory read/write successful (page fault handled)");
                                                                          int pages_after_fault = freemem();
  check(pages_after_fault < start_pages, "freemem decreased after fault");

  check(munmap(addr) == 1, "munmap successful");
}
// 3. 파일 + 즉시 할당 (성공)
void
test_file_populate()
{
  printf("\n--- 3. Test: File Mapping (POPULATE) ---\n");
  int fd = open("README", O_RDONLY);
  check(fd >= 0, "Opened README file");

  uint64 addr = mmap(0, PGSIZE, PROT_READ, MAP_POPULATE, fd, 0);
  check(addr == MMAPBASE, "mmap returned correct address");

  char *ptr = (char*)addr;
  printf("File content [0-2]: %c%c%c\n", ptr[0], ptr[1], ptr[2]);
  check(ptr[0] != 0, "File content was loaded (not zero)");

  check(munmap(addr) == 1, "munmap successful");
  close(fd);
}
// 4. fork + 지연 할당 (성공)
void
test_fork_happy()
{
  printf("\n--- 4. Test: Fork() and Freeproc() (Happy Path) ---\n");
  int start_pages = freemem();
  int fd = open("README", O_RDONLY);
  mmap(0, PGSIZE, PROT_READ, 0, fd, 0); // 지연 매핑

  int pid = fork();
  check(pid >= 0, "fork successful");

  if (pid == 0) { // 자식
    int child_pages_start = freemem();
    check(child_pages_start < start_pages, "Child freemem DECREASED after fork (fork cost OK)");
                                                                            char *ptr = (char*)MMAPBASE;
    printf("Child: Accessing memory (triggers fault)...\n");
    (void)ptr[0]; // 폴트 발생

    int child_pages_after_fault = freemem();
    check(child_pages_after_fault < child_pages_start, "Child freemem decreased after page fault");
    close(fd);
    exit(0);
  } else { // 부모
    wait(0);
    printf("Parent: Child finished.\n");

    int parent_pages_after_wait = freemem();
    check(parent_pages_after_wait == start_pages, "Parent freemem back to start (freeproc OK)");

    check(munmap(MMAPBASE) == 1, "Parent munmap successful");
    close(fd);
  }
}
// 5. mmap 실패: 주소 정렬 위반
void
test_failure_alignment()
{
  printf("\n--- 5. Test: mmap Failure (Non-aligned Address) ---\n");
  // [cite: 608]
  uint64 addr = mmap(1, PGSIZE, PROT_READ, MAP_ANONYMOUS, -1, 0);
  check(addr == 0, "mmap failed for non-aligned address (addr=1)");
}

// 6. mmap 실패: 권한 불일치
void
test_failure_protection_mismatch()
{
  printf("\n--- 6. Test: mmap Failure (Protection Mismatch) ---\n");
  int fd = open("README", O_RDONLY);

  // [cite: 226]
  uint64 addr = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_POPULATE, fd, 0);
  check(addr == 0, "mmap failed for PROT_WRITE on O_RDONLY file");

  close(fd);
}
// 7. 페이지 폴트 실패: 쓰기 권한 위반 (가장 중요)
void
test_failure_write_violation()
{
  printf("\n--- 7. Test: Page Fault Failure (Write Violation) ---\n");

  int pid = fork();
  if (pid == 0) {
    // 자식: 읽기 전용으로 익명 매핑
    uint64 addr = mmap(0, PGSIZE, PROT_READ, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if(addr != MMAPBASE) exit(1);

    char *ptr = (char*)addr;
    printf("Child: Attempting to write to PROT_READ area (should be killed)...\n");

    // [cite: 531]
    ptr[0] = 'X'; // <-- 여기서 페이지 폴트 (scause=15) -> kill

    printf("Child: [ERROR] Write did not kill process!\n");
    exit(0); // 정상 종료되면 테스트 실패
  } else {
    // 부모: 자식이 비정상 종료(-1)되기를 기다림
    int status = 0;
    wait(&status);

    // [cite: 524]
    check(status == -1, "Child process was killed (protection fault handled)");
  }
}
// 8. munmap 실패: 유효하지 않은 주소
void
test_failure_munmap_invalid()
{
  printf("\n--- 8. Test: munmap Failure (Invalid Address) ---\n");

  // [cite: 549]
  int ret = munmap(MMAPBASE + (PGSIZE * 20)); // 매핑되지 않은 영역
  check(ret == -1, "munmap failed for unmapped mmap address");

  ret = munmap(0x1000); // MMAPBASE가 아닌 주소
  check(ret == -1, "munmap failed for non-mmap address");
}
int
main(int argc, char *argv[])
{
  printf("=== MMAP COMPREHENSIVE TEST SUITE STARTING ===\n");

  // --- Happy Path Tests ---
  test_anonymous_populate();
  test_anonymous_lazy();
  test_file_populate();
  test_fork_happy();

  // --- Sad Path (Failure) Tests ---
  test_failure_alignment();
  test_failure_protection_mismatch();
  test_failure_write_violation();
  test_failure_munmap_invalid();

  printf("\n=== MMAP TEST SUITE FINISHED ===\n");
  if(fail_count > 0) {
    printf("Result: %d TEST(S) FAILED.\n", fail_count);
  } else {
    printf("Result: ALL TESTS PASSED.\n");
  }
  exit(0);
}