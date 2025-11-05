#define SBRK_EAGER 1
#define SBRK_LAZY  2

struct file;
struct proc;

struct mmap_area {
  struct file *f;   // null if anonymous
  uint64 addr;      // user va start (MMAPBASE + user-specified offset)
  int length;       // bytes, multiple of PGSIZE
  int offset;       // file offset
  int prot;         // PROT_*
  int flags;        // MAP_*
  struct proc *p;   // owner
};

int freemem(void);
uint64 do_mmap(uint64 addr, int length, int prot, int flags, int fd, int offset);
int do_munmap(uint64 addr);
