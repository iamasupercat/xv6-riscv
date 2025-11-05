/**
 * xv6 mmap() Project 3 Test Code
 *
 * 이 테스트는 SWE3004 Project 3 교안에 명시된 모든 요구 사항을 검증합니다.
 * 1. freemem() 시스템 콜
 * 2. mmap() - Anonymous mapping (with/without POPULATE)
 * 3. mmap() - File mapping (with/without POPULATE)
 * 4. Page Fault Handler (no-populate 접근 시)
 * 5. munmap() 시스템 콜 (populated/non-populated 영역)
 * 6. fork() 시 mmap 영역 상속 및 내용 일치
 * 7. PROT_READ 영역에 쓰기 시도 시 오류 (페이지 폴트 핸들러 실패)
 */

 #include "../kernel/types.h"
 #include "../kernel/stat.h"
 #include "user.h"
 #include "../kernel/fcntl.h"     // O_RDWR 등
 #include "../kernel/memlayout.h" // MMAPBASE [cite: 195]
 #include "../kernel/param.h"     // Project 3에서 추가한 플래그들
 #include "../kernel/spinlock.h"
 #include "../kernel/sleeplock.h"
 #include "../kernel/fs.h"
 #include "../kernel/syscall.h"   // SYS_mmap 등 (user.h에 정의됨)
 #include "../kernel/riscv.h"
 
 
 // 테스트 결과 출력을 위한 헬퍼
 void
 check(int condition, char *msg)
 {
     if (condition) {
         printf("PASS: %s\n", msg);
     } else {
         printf("FAIL: %s\n", msg);
         exit(1); // 실패 시 테스트 종료
     }
 }
 
 int
 main(int argc, char *argv[])
 {
     printf("--- mmap test starting ---\n");
 
     int initial_free = freemem();
     printf("Initial free pages: %d\n", initial_free);
 
     // =================================================================
     // 테스트 1: Anonymous + MAP_POPULATE [cite: 572]
     // =================================================================
     printf("\n--- Test 1: Anonymous + POPULATE ---\n");
     int len1 = PGSIZE * 2;
     uint64 addr1 = mmap(0, len1, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
 
     check(addr1 == MMAPBASE + 0, "mmap() returned correct address");
 
     int free1 = freemem();
     check(free1 == initial_free - 2, "freemem() decreased by 2 pages");
 
     char *mem1 = (char*)addr1;
     mem1[0] = 'a'; // 쓰기
     mem1[PGSIZE] = 'b';
     check(mem1[0] == 'a' && mem1[PGSIZE] == 'b', "Read/write populated memory");
     check(mem1[100] == 0, "Anonymous memory is zero-filled");
 
     // =================================================================
  // 테스트 2: Anonymous (No POPULATE) + Page Fault [cite: 572]
     // =================================================================
     printf("\n--- Test 2: Anonymous (No POPULATE) + Page Fault ---\n");
     int len2 = PGSIZE;
     // addr1이 0~2*PGSIZE 사용 중이므로 2*PGSIZE에서 시작
     uint64 addr2 = mmap(len1, len2, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
 
     check(addr2 == MMAPBASE + (uint64)len1, "mmap() returned correct address");
 
     int free2 = freemem();
     check(free2 == free1, "freemem() unchanged after mmap (no-populate)");
 
     // 페이지 폴트 유발
     printf("Triggering page fault...\n");
     char *mem2 = (char*)addr2;
     mem2[0] = 'c'; // 쓰기 (Fault)
 
     int free3 = freemem();
     check(free3 == free2 - 1, "freemem() decreased by 1 page after fault");
     check(mem2[0] == 'c', "Read/write after page fault");
     check(mem2[500] == 0, "Anonymous faulted page is zero-filled");
 
     // =================================================================
     // 테스트 3: File + MAP_POPULATE [cite: 573]
     // =================================================================
     printf("\n--- Test 3: File + POPULATE ---\n");
     int fd = open("README", O_RDONLY);// [cite: 566]
     check(fd >= 0, "Opened README file");
 
     int len3 = PGSIZE;
     uint64 addr3 = mmap(len1 + len2, len3, PROT_READ, MAP_POPULATE, fd, 0);
     check(addr3 != 0, "mmap() for file successful");
 
     int free4 = freemem();
     check(free4 == free3 - 1, "freemem() decreased by 1 page (populate file)");
 
     // 파일 내용 비교 [cite: 577]
     char buf[10];
     read(fd, buf, 10); // 파일에서 직접 읽기
     close(fd);
 
     char *mem3 = (char*)addr3;
     check(mem3[0] == buf[0] && mem3[1] == buf[1], "File content matches (populated)");
     printf("File content via mmap (first 5 chars): %c%c%c%c%c\n", mem3[0], mem3[1], mem3[2], mem3[3], mem3[4]);
 
     // =================================================================
     // 테스트 4: File (No POPULATE) + Page Fault [cite: 573]
     // =================================================================
     printf("\n--- Test 4: File (No POPULATE) + Page Fault ---\n");
     fd = open("README", O_RDONLY);
     check(fd >= 0, "Opened README file again");
 
     int len4 = PGSIZE;
     uint64 addr4 = mmap(len1 + len2 + len3, len4, PROT_READ, 0, fd, 0);
     check(addr4 != 0, "mmap() for file successful");
 
     int free5 = freemem();
     check(free5 == free4, "freemem() unchanged after mmap (no-populate)");
 
     // 페이지 폴트 유발
     printf("Triggering page fault for file map...\n");
     char *mem4 = (char*)addr4;
     char c = mem4[5]; // 읽기 (Fault) [cite: 581]
 
     int free6 = freemem();
     check(free6 == free5 - 1, "freemem() decreased by 1 page after fault");
 
     // 파일 내용 비교 [cite: 577]
     char buf2[10];
     read(fd, buf2, 10); // 파일에서 직접 읽기
     close(fd);
     check(c == buf2[5], "File content matches (after fault)");
 
     // =================================================================
     // 테스트 5: munmap() [cite: 541]
     // =================================================================
     printf("\n--- Test 5: munmap() ---\n");
 
     // 1. Populated 영역 해제 (Test 1)
     check(munmap(0) == 1, "munmap() populated area (addr1) successful");
     int free7 = freemem();
     check(free7 == free6 + 2, "freemem() restored 2 pages"); // Test 1은 2페이지였음
 
     // 2. Faulted 영역 해제 (Test 2)
     check(munmap(len1) == 1, "munmap() faulted area (addr2) successful");
     int free8 = freemem();
     check(free8 == free7 + 1, "freemem() restored 1 page");
 
     // 3. Non-populated 영역 해제
     uint64 addr5 = mmap(0, PGSIZE, PROT_READ, MAP_ANONYMOUS, -1, 0);
     check(addr5 == MMAPBASE + 0, "mmap() non-populated returned correct address");
     int free_before_unmap = freemem();
     check(munmap(0) == 1, "munmap() non-populated area successful");
     int free_after_unmap = freemem();
     check(free_after_unmap == free_before_unmap, "freemem() unchanged for non-populated munmap");
 
     // 4. 잘못된 주소 해제
     check(munmap(0) == -1, "munmap() fails for non-existent mapping");
 
     // 남은 영역 정리
     munmap(len1 + len2);
     munmap(len1 + len2 + len3);
 
     int final_free = freemem();
     check(final_free == initial_free, "All pages freed, freemem() back to initial");
 
     // =================================================================
     // 테스트 6: fork() 테스트 [cite: 578]
     // =================================================================
     printf("\n--- Test 6: fork() Test ---\n");
 
     uint64 f_addr_pop = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
     uint64 f_addr_fault = mmap(PGSIZE, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
 
     char *f_mem_pop = (char*)f_addr_pop;
     char *f_mem_fault = (char*)f_addr_fault;
 
     f_mem_pop[0] = 'P';   // 부모가 씀 (Populated)
     f_mem_fault[0] = 'F'; // 부모가 씀 (Fault)
 
     int free_before_fork = freemem();
     printf("Freemem before fork: %d\n", free_before_fork);
 
     int pid = fork();
     if (pid < 0) {
         printf("FAIL: fork() failed\n");
         exit(1);
     }
 
     if (pid == 0) {
         // --- 자식 프로세스 ---
         printf("  [Child] Running...\n");
         int free_child = freemem();
         printf("  [Child] Freemem: %d\n", free_child);
 
         check(free_child < free_before_fork, "Child freemem is less than parent (uvmcopy)");
 
         char *c_mem_pop = (char*)f_addr_pop;
         char *c_mem_fault = (char*)f_addr_fault;
 
         // 1. 부모 데이터 상속 확인
         check(c_mem_pop[0] == 'P', "[Child] Inherited populated data");
         check(c_mem_fault[0] == 'F', "[Child] Inherited faulted data");
 
         // 2. 자식 쓰기 (독립적인지 확인)
         c_mem_pop[0] = 'C';
         check(c_mem_pop[0] == 'C', "[Child] Wrote 'C' to its own copy");
         printf("  [Child] Exiting.\n");
         exit(0);
 
     } else {
         // --- 부모 프로세스 ---
         wait(0);
         printf("  [Parent] Child finished.\n");
 
         // 3. 자식의 쓰기가 부모에게 영향을 주지 않았는지 확인
         check(f_mem_pop[0] == 'P', "[Parent] Data unaffected by child write");
 
         // 4. fork 후 부모의 freemem 확인
         int free_after_fork = freemem();
         check(free_after_fork == free_before_fork, "[Parent] freemem() unchanged after child exited");
 
         munmap(0);
         munmap(PGSIZE);
         printf("PASS: fork() Test\n");
     }
 
     // =================================================================
  // 테스트 7: 쓰기 금지 페이지 폴트 (Write Protection Fault) [cite: 531]
     // =================================================================
     printf("\n--- Test 7: Write Protection Fault ---\n");
 
     int fault_pid = fork();
     if (fault_pid == 0) {
         // 자식이 PROT_READ 영역에 쓰기를 시도
         uint64 ro_addr = mmap(0, PGSIZE, PROT_READ, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
         if(ro_addr == 0) exit(1);
 
         printf("  [Child] Attempting illegal write to PROT_READ area...\n");
         ((char*)ro_addr)[0] = 'X'; // 불법 쓰기. 페이지 폴트 핸들러가 -1을 반환하고, 프로세스가 종료되어야 함
 
         // 이 메시지가 출력되면 테스트 실패
         printf("  [Child] FAIL: Write was successful! Process was not killed!\n");
         exit(2);
     } else {
         int status = 0;
         wait(&status); // 자식이 종료될 때까지 대기
 
         if (status == 2) {
             printf("FAIL: Write protection test failed. Child was not killed.\n");
         } else if (status == 1) {
             printf("FAIL: mmap failed in child.\n");
         } else {
             // 자식이 0, 1, 2가 아닌 다른 값(커널에 의해 종료됨)으로 종료됨
             printf("PASS: Write protection test. Child was terminated (status: %d).\n", status);
         }
     }
 
     // =================================================================
     printf("\n--- All mmap tests finished ---\n");
     final_free = freemem();
     printf("Final free pages: %d (Should be same as initial)\n", final_free);
     check(final_free == initial_free, "Final freemem() matches initial");
 
     exit(0);
 }