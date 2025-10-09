#include "kernel/types.h"
#include "user/user.h"

void cpu_hog(const char* name) {
    printf("Process '%s' starting cpu-intensive work.\n", name);
    long long j = 0;
    for (long long i = 0; i < 2000000000; i++) {
        j += i;
    }
    printf("Process '%s' finished. Result: %d\n", name, (int)(j & 0xFFFFFFFF));
}

int main(int argc, char *argv[]) {
    int nice_values[] = { 25, 29, 35 };
    int num_procs = sizeof(nice_values) / sizeof(int);
    
    printf("EEVDF scheduler test starting...\n");

    for (int i = 0; i < num_procs; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            char child_name[10];
            child_name[0] = 'H'; child_name[1] = 'O'; child_name[2] = 'G';
            child_name[3] = '0' + i; child_name[4] = '\0';

            setnice(getpid(), nice_values[i]);
            cpu_hog(child_name);
            exit(0);
        }
    }

    printf("\n--- Parent starting observation. Will run 'ps' 15 times. ---\n\n");
    for (int i = 0; i < 15; i++) {

        for (volatile int i = 0; i < 10000; i++) { }
        
        int ps_pid = fork();
        if (ps_pid == 0) {
            ps(0);
            exit(1);
        }
        wait(0);
        printf("\n------------------------------------------------\n\n");
    }

    printf("--- Observation finished. Cleaning up hog processes. ---\n");
    for (int i = 0; i < num_procs; i++) {
        wait(0);
    }

    printf("EEVDF scheduler test finished!\n");
    exit(0);
}