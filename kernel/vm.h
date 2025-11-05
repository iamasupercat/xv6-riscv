#define SBRK_EAGER 1
#define SBRK_LAZY  2

struct file;
struct proc;

struct mmap_area {
  struct file *f;   // null for anonymous
  uint64 addr;      // user virtual start (MMAPBASE + offset)
  int length;       // bytes, multiple of PGSIZE
  int offset;       // file offset
  int prot;         // PROT_* flags
  int flags;        // MAP_* flags
  struct proc *p;   // owner
};

uint64 do_mmap(uint64 addr, int length, int prot, int flags, int fd, int offset);
int do_munmap(uint64 addr);
void fork_mmaps(struct proc *parent, struct proc *child);
