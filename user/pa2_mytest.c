// user/eetest.c

#include "kernel/types.h"
#include "user/user.h"

#define NUM_CHILDREN 3

// CPU를 많이 사용하는 작업 (무한 루프)
void busy_work() {
    volatile unsigned long long counter = 0;
    while (1) {
        counter++;
    }
}

int main(int argc, char *argv[]) {
    int pids[NUM_CHILDREN];
    int nice_values[NUM_CHILDREN] = {5, 20, 35}; // High, Normal, Low priority

    printf("EEVDF Scheduler Test starting...\n");
    printf("Creating %d child processes with nice values: %d, %d, %d\n",
           NUM_CHILDREN, nice_values[0], nice_values[1], nice_values[2]);

    for (int i = 0; i < NUM_CHILDREN; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(-1);
        }

        if (pid == 0) {
            // --- 자식 프로세스 코드 ---
            int current_pid = getpid();
            // setnice 시스템 콜을 사용하여 자신의 우선순위를 설정
            if (setnice(current_pid, nice_values[i]) < 0) {
                printf("setnice failed for pid %d\n", current_pid);
            }
            // CPU를 사용하는 무한 루프 시작
            busy_work();
            exit(0); // 이 코드는 실행되지 않음 (kill 전까지)
        } else {
            // --- 부모 프로세스 코드 ---
            pids[i] = pid;
        }
    }

    // 자식 프로세스들이 CPU를 점유하고 경쟁할 시간을 줍니다.
    // 100 ticks = 약 10초
    printf("\nParent waiting for 100 ticks (about 10 seconds)...\n");
    sleep(100);

    // ps 시스템 콜을 호출하여 스케줄러 상태를 확인합니다.
    printf("\n--- Running ps to check scheduler status ---\n");
    ps(0);
    printf("--- End of ps output ---\n\n");


    // 테스트 종료를 위해 자식 프로세스들을 모두 kill 합니다.
    printf("Killing child processes...\n");
    for (int i = 0; i < NUM_CHILDREN; i++) {
        kill(pids[i]);
    }

    // 자식 프로세스들이 좀비 상태로 남지 않도록 wait 해줍니다.
    for (int i = 0; i < NUM_CHILDREN; i++) {
        wait(0);
    }

    printf("\nEEVDF Scheduler Test finished.\n");
    exit(0);
}