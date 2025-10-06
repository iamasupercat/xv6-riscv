#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"

int main()
{
        int nice_val;

        //getnice and setnice
        printf(">>>Testing getnice and setnice:\n");

        int pid = getpid();

        nice_val = getnice(pid);
        if (nice_val == -1) {
                printf("Error: getnice failed.\n");
        } else {
                printf("initial nice value: %d\n", nice_val);
        }

        if (setnice(pid, 10) == -1) {
                printf("Error: setnice failed.\n");
        }
        else if (setnice(pid, 10) == 0) {
                printf("Good setnice.\n");
        }

        nice_val = getnice(pid);
        if (nice_val == -1) {
                printf("Error: getnice failed after setting.\n");
        } else {
                printf("nice value after setting: %d\n", nice_val);
        }

        printf(">>>Testing ps:\n");
        ps(0);
        ps(2);

        //ps(5);

        uint64 available_memory;

        printf(">>>Testing meminfo:\n");
        available_memory = meminfo();

        printf("available memory: %ld bytes\n", available_memory);

        int pid1, pid2;

        printf(">>>Testing waitpid:\n");
        printf("wait\n");

        pid1 = fork();
        if (pid1 == 0) {
                printf("start1\n");
                pause(10);
                printf("end1\n");
                exit(0);
        } else {
                pid2 = fork();
                if (pid2 == 0) {
                        printf("start2\n");
                        pause(5);
                        printf("end2\n");
                        exit(0);
                } else {
                        int result_pid;
                        result_pid = waitpid(pid1);
                        if (result_pid == 0) {
                                printf("done1 %d %d\n", pid1, 10);
                        }

                        result_pid = waitpid(pid2);
                        if (result_pid == 0) {
                                printf("done2 %d %d\n", pid2, 10);
                        }

                        printf("Mytest finished.\n");
                        exit(0);
                }
        }

        return 0;
}