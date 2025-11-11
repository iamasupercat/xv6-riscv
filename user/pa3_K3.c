#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"
#include "../kernel/fcntl.h"
#include "../kernel/memlayout.h"
#include "../kernel/param.h"
#include "../kernel/syscall.h"

// Define mmap flags and protections based on Project 3 PDF (Page 20)
// #define PROT_READ       0x1
// #define PROT_WRITE      0x2
// #define MAP_ANONYMOUS   0x1
// #define MAP_POPULATE    0x2

// MMAPBASE as defined in Project 3 PDF (Page 8)
// #define MMAPBASE 0x40000000
#define PGSIZE 4096

// Helper function to print test results
void test_result(int passed, char* name) {
    if (passed) {
        printf("Test %s: PASSED\n", name);
    } else {
        printf("Test %s: FAILED\n", name);
    }
}

// Helper function to check freemem changes
// Returns 1 if the test passes, 0 otherwise
int check_freemem(int mem_before, int mem_after, int expected_diff, char* test_name) {
    int diff = mem_before - mem_after;
    if (diff == expected_diff) {
        printf("freemem %s: OK (Before: %d, After: %d, Diff: %d)\n", test_name, mem_before, mem_after, diff);
        return 1;
    } else {
        printf("freemem %s: FAILED (Before: %d, After: %d, Expected Diff: %d, Actual Diff: %d)\n", test_name, mem_before, mem_after, expected_diff, diff);
        return 0;
    }
}

// Test 1: Anonymous mapping with MAP_POPULATE
void test_anon_populate() {
    int passed = 1;
    int mem_before = freemem();
    int map_len = PGSIZE * 2; // Map 2 pages

    // mmap anonymous, populated, read/write memory
    char *mem = (char*)mmap(0, map_len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);

    if ((uint64)mem < MMAPBASE) {
        printf("mmap failed\n");
        passed = 0;
    }

    int mem_after_mmap = freemem();
    // Memory should be allocated immediately
    if (passed) {
        passed = check_freemem(mem_before, mem_after_mmap, 2, "anon_populate mmap");
    }

    if (passed) {
        // Write to memory
        mem[0] = 'A';
        mem[PGSIZE] = 'B';

        // Verify memory
        if (mem[0] != 'A' || mem[PGSIZE] != 'B') {
            printf("Memory content verification failed\n");
            passed = 0;
        }
    }

    // Unmap the memory
    if (munmap((uint64)mem) == -1) {
        printf("munmap failed\n");
        passed = 0;
    }

    int mem_after_unmap = freemem();
    // Memory should be freed
    if (passed) {
        passed = check_freemem(mem_after_mmap, mem_after_unmap, -2, "anon_populate munmap");
    }

    test_result(passed, "anonymous populate");
}

// Test 2: Anonymous mapping without MAP_POPULATE (lazy allocation)
void test_anon_lazy() {
    int passed = 1;
    int mem_before = freemem();
    int map_len = PGSIZE * 2; // Map 2 pages

    // mmap anonymous, lazy, read/write memory
    char *mem = (char*)mmap(0, map_len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);

    if ((uint64)mem < MMAPBASE) {
        printf("mmap failed\n");
        passed = 0;
    }

    int mem_after_mmap = freemem();
    // Memory should NOT be allocated yet
    if (passed) {
        passed = check_freemem(mem_before, mem_after_mmap, 0, "anon_lazy mmap");
    }

    if (passed) {
        // Access page 1, triggering a page fault
        mem[0] = 'X';
        if (mem[0] != 'X') {
            printf("Memory content verification failed (Page 1)\n");
            passed = 0;
        }
        int mem_after_fault1 = freemem();
        // 1 page should be allocated
        if (passed) {
            passed = check_freemem(mem_after_mmap, mem_after_fault1, 1, "anon_lazy fault 1");
        }

        // Access page 2, triggering another page fault
        mem[PGSIZE] = 'Y';
        if (mem[PGSIZE] != 'Y') {
            printf("Memory content verification failed (Page 2)\n");
            passed = 0;
        }
        int mem_after_fault2 = freemem();
        // 1 more page should be allocated
        if (passed) {
            passed = check_freemem(mem_after_fault1, mem_after_fault2, 1, "anon_lazy fault 2");
        }

        // Unmap the memory
        if (munmap((uint64)mem) == -1) {
            printf("munmap failed\n");
            passed = 0;
        }

        int mem_after_unmap = freemem();
        // Both pages should be freed
        if (passed) {
            passed = check_freemem(mem_after_fault2, mem_after_unmap, -2, "anon_lazy munmap");
        }
    }

    test_result(passed, "anonymous lazy (page fault)");
}

// Test 3: File mapping with MAP_POPULATE
void test_file_populate() {
    int passed = 1;
    int fd = open("README", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open README\n");
        test_result(0, "file populate");
        return;
    }

    int mem_before = freemem();
    int map_len = PGSIZE * 2; // Map 2 pages

    // mmap file, populated, read-only
    char *mem = (char*)mmap(0, map_len, PROT_READ, MAP_POPULATE, fd, 0);

    if ((uint64)mem < MMAPBASE) {
        printf("mmap failed\n");
        passed = 0;
    }

    int mem_after_mmap = freemem();
    // Memory should be allocated immediately
    if (passed) {
        passed = check_freemem(mem_before, mem_after_mmap, 2, "file_populate mmap");
    }

    if (passed) {
        // Read from memory (should not fault)
        // Print first 3 chars as per PDF hint (Page 32)
        printf("file_populate data: %c%c%c\n", mem[0], mem[1], mem[2]);
        if (mem[0] == 0) { // Basic check
            printf("File content seems empty\n");
            passed = 0;
        }
    }

    close(fd);

    // Unmap the memory
    if (munmap((uint64)mem) == -1) {
        printf("munmap failed\n");
        passed = 0;
    }

    int mem_after_unmap = freemem();
    // Memory should be freed
    if (passed) {
        passed = check_freemem(mem_after_mmap, mem_after_unmap, -2, "file_populate munmap");
    }

    test_result(passed, "file populate");
}

// Test 4: File mapping without MAP_POPULATE (lazy loading)
void test_file_lazy() {
    int passed = 1;
    int fd = open("README", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open README\n");
        test_result(0, "file lazy");
        return;
    }

    int mem_before = freemem();
    int map_len = PGSIZE * 2; // Map 2 pages

    // mmap file, lazy, read-only
    char *mem = (char*)mmap(0, map_len, PROT_READ, 0, fd, 0);

    if ((uint64)mem < MMAPBASE) {
        printf("mmap failed\n");
        passed = 0;
    }

    int mem_after_mmap = freemem();
    int mem_after_fault2 = mem_after_mmap; // Declare here, initialize to a known value

    // Memory should NOT be allocated yet
    if (passed) {
        passed = check_freemem(mem_before, mem_after_mmap, 0, "file_lazy mmap");
    }

    if (passed) {
        // Access page 1, triggering a page fault
        printf("file_lazy data (Page 1): %c\n", mem[0]);
        if (mem[0] == 0) { // Basic check
            printf("File content seems empty (Page 1)\n");
            passed = 0;
        }
        int mem_after_fault1 = freemem();
        // 1 page should be allocated
        if(passed) {
            passed = check_freemem(mem_after_mmap, mem_after_fault1, 1, "file_lazy fault 1");
        }

        // Access page 2, triggering another page fault
        printf("file_lazy data (Page 2): %c\n", mem[PGSIZE]);
        // Note: This might be 0 if README is smaller than 1 page

        mem_after_fault2 = freemem(); // Assign here (remove 'int')
        // 1 more page should be allocated
        if(passed) {
            passed = check_freemem(mem_after_fault1, mem_after_fault2, 1, "file_lazy fault 2");
        }
    }

    close(fd);

    // Unmap the memory
    if (munmap((uint64)mem) == -1) {
        printf("munmap failed\n");
        passed = 0;
    }

    int mem_after_unmap = freemem();
    // Both pages should be freed
    if (passed) {
        passed = check_freemem(mem_after_fault2, mem_after_unmap, -2, "file_lazy munmap");
    }

    test_result(passed, "file lazy (page fault)");
}

// Test 5: Fork test (no COW)
void test_fork() {
    printf("Starting fork test...\n");
    int passed = 1;
    int map_len = PGSIZE; // 1 page
    int mem_before_all = freemem();

    // 1. Parent maps an anonymous page
    char *anon_mem = (char*)mmap(0, map_len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if ((uint64)anon_mem < MMAPBASE) {
        printf("fork_test: parent anon mmap failed\n");
        test_result(0, "fork");
        return;
    }

    // 2. Parent maps a file page
    int fd = open("README", O_RDONLY);
    if (fd < 0) {
        printf("fork_test: Failed to open README\n");
        test_result(0, "fork");
        return;
    }
    char *file_mem = (char*)mmap(map_len, map_len, PROT_READ, 0, fd, 0); // Map at offset 4096
    if ((uint64)file_mem < MMAPBASE) {
        printf("fork_test: parent file mmap failed\n");
        close(fd);
        test_result(0, "fork");
        return;
    }

    // 3. Parent triggers faults for both pages
    anon_mem[0] = 'P'; // 'P' for Parent
    char file_char = file_mem[0];
    printf("fork_test: Parent read '%c' from file map\n", file_char);

    int mem_before_fork = freemem();
    // We expect 2 pages to be allocated (1 anon, 1 file)
    if (!check_freemem(mem_before_all, mem_before_fork, 2, "fork parent faults")) {
        passed = 0;
    }

    // 4. Parent forks
    int pid = fork();

    if (pid < 0) {
        printf("fork_test: fork failed\n");
        passed = 0;
    } else if (pid == 0) {
        // --- Child Process ---
        int child_passed = 1;
        printf("fork_test: Child process started\n");
        int mem_child_start = freemem();

        // Per uvmcopy (page 6), fork should kalloc new pages for the child.
        // So, 2 new pages should be allocated for the child's copies.
        if (!check_freemem(mem_before_fork, mem_child_start, 2, "fork child copy")) {
            child_passed = 0;
        }

        // Verify child has parent's data
        if (anon_mem[0] != 'P') {
            printf("fork_test child: Anon data mismatch! Expected 'P', got '%c'\n", anon_mem[0]);
            child_passed = 0;
        }
        if (file_mem[0] != file_char) {
            printf("fork_test child: File data mismatch! Expected '%c', got '%c'\n", file_char, file_mem[0]);
            child_passed = 0;
        }

        // Child writes to its *own* copy
        anon_mem[0] = 'C'; // 'C' for Child
        if (anon_mem[0] != 'C') {
            printf("fork_test child: Write failed\n");
            child_passed = 0;
        }

        printf("fork_test: Child process exiting with status %d\n", child_passed);
        exit(child_passed); // Use exit code to signal pass/fail
    } else {
        // --- Parent Process ---
        int child_status;
        wait(&child_status);

        if (child_status != 1) { // Check child's exit status
            printf("fork_test: Child process reported FAILED\n");
            passed = 0;
        }

        int mem_after_fork = freemem();
        // Parent's freemem should be unchanged since fork
        if (!check_freemem(mem_before_fork, mem_after_fork, 0, "fork parent post-wait")) {
            passed = 0;
        }

        // Verify parent's data is UNCHANGED (proving no COW, but a true copy)
        if (anon_mem[0] != 'P') {
            printf("fork_test parent: Data corrupted by child! Expected 'P', got '%c'\n", anon_mem[0]);
            passed = 0;
        } else {
            printf("fork_test parent: Data ('P') is intact. Good.\n");
        }

        // Cleanup
        close(fd);
        munmap((uint64)anon_mem);
        munmap((uint64)file_mem);

        int mem_after_cleanup = freemem();
        // Parent frees its 2 pages. Child's pages were freed on exit.
        // System should be back to the state before the test.
        if (!check_freemem(mem_after_fork, mem_after_cleanup, -2, "fork parent cleanup")) {
            passed = 0;
        }
    }

    test_result(passed, "fork (no-COW copy)");
}


int main(int argc, char *argv[]) {
    printf("Starting mmap tests...\n");

    int initial_mem = freemem();
    printf("Initial free pages: %d\n", initial_mem);

    test_anon_populate();
    test_anon_lazy();
    test_file_populate();
    test_file_lazy();
    test_fork();

    int final_mem = freemem();
    printf("Final free pages: %d\n", final_mem);

    if (initial_mem != final_mem) {
        printf("MEMORY LEAK DETECTED! Initial: %d, Final: %d\n", initial_mem, final_mem);
    } else {
        printf("All tests finished. Memory successfully returned.\n");
    }

    printf("mmap tests complete.\n");
    exit(0);
}