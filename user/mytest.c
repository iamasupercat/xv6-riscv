#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int pid;
  int nice_val;

  printf("\n=== waitpid function test ===\n");
  int child_pid = fork();

  if (child_pid < 0) {
    printf("fork() failed!\n");
    exit(1);
  } else if (child_pid == 0) {
    // child process
    printf("child process (%d) started\n", getpid());

    printf("child process (%d) finished\n", getpid());
    exit(0);
  } else {
    // parent process
    printf("parent process (%d) started", getpid());
    printf("waiting for child (%d)\n", child_pid);
    if (waitpid(child_pid) == 0) {
      printf("child process (%d) finished successfully\n", child_pid);
    } else {
      printf("waitpid error occurred\n");
    }
    
    // test waitpid with non-existent pid
    printf("\n=== waitpid test with non-existent PID ===\n");
    if (waitpid(9999) == -1) {
        printf("SUCCESS: waitpid returned -1 for non-existent PID\n");
    } else {
        printf("FAIL: waitpid did not return -1 for non-existent PID\n");
    }
  }

  printf("\n=== ps, getnice, setnice, meminfo function test ===\n");
  
  // 1. ps function test
  printf("1. ps(0) - print all process information\n");
  ps(0);

  // 2. getnice & setnice function test
  pid = getpid();
  printf("\n2. getnice & setnice test (my PID: %d)\n", pid);
  
  nice_val = getnice(pid);
  printf("    - initial nice value: %d\n", nice_val);
  
  if (setnice(pid, 10) == 0) {
    printf("    - successfully changed nice value to 10\n");
  } else {
    printf("    - failed to change nice value\n");
  }

  nice_val = getnice(pid);
  printf("    - nice value after change: %d\n", nice_val);

  // 3. meminfo function test
  printf("\n3. meminfo - print available memory\n");
  printf("%d\n", meminfo());

  printf("\n=== All tests completed ===\n");
  exit(0);
}