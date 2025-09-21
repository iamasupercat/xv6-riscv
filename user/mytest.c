#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int pid;
  int nice_val;

  printf("\n--- waitpid 기능 테스트 ---\n");
  int child_pid = fork();

  if (child_pid < 0) {
    printf("fork() 실패!\n");
    exit(1);
  } else if (child_pid == 0) {
    // 자식 프로세스
    printf("자식 프로세스 (%d) 시작\n", getpid());
    printf("자식 프로세스 (%d) 종료\n", getpid());
    exit(0);
  } else {
    // 부모 프로세스
    printf("부모 프로세스 (%d) 시작. 자식 (%d) 기다림\n", getpid(), child_pid);
    if (waitpid(child_pid) == 0) {
      printf("자식 프로세스 (%d) 종료 확인\n", child_pid);
    } else {
      printf("waitpid 오류 발생\n");
    }
    
    // 존재하지 않는 pid에 대한 waitpid 테스트
    printf("\n--- 존재하지 않는 PID에 대한 waitpid 테스트 ---\n");
    if (waitpid(9999) == -1) {
        printf("성공: 존재하지 않는 PID에 대해 -1 반환\n");
    } else {
        printf("실패: 존재하지 않는 PID에 대해 -1을 반환하지 않음\n");
    }
  }


  printf("\n--- ps, getnice, setnice, meminfo 기능 테스트 ---\n");
  
  // 1. ps 기능 테스트
  printf("1. ps(0) - 모든 프로세스 정보 출력\n");
  ps(0);

  // 2. getnice & setnice 기능 테스트
  pid = getpid();
  printf("2. getnice & setnice 테스트 (내 PID: %d)\n", pid);
  
  nice_val = getnice(pid);
  printf("    - 초기 nice 값: %d\n", nice_val);
  
  if (setnice(pid, 10) == 0) {
    printf("    - nice 값을 10으로 변경 성공\n");
  } else {
    printf("    - nice 값 변경 실패\n");
  }

  nice_val = getnice(pid);
  printf("    - 변경 후 nice 값: %d\n", nice_val);

  // 3. meminfo 기능 테스트
  printf("\n3. meminfo - 사용 가능한 메모리 출력\n");
  meminfo();

  exit(0);
}