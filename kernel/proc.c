#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern uint ticks;
extern struct spinlock tickslock;

static const int nice_to_weight[40] = {
  88761, 71755, 56483, 46273, 36291, // 0-4
  29154, 23254, 18705, 14949, 11916, // 5-9
  9548, 7620, 6100, 4904, 3906,      // 10-14
  3121, 2501, 1991, 1586, 1277,      // 15-19
  1024, 820, 655, 526, 423,          // 20-24
  335, 272, 215, 172, 137,          // 25-29
  110, 87, 70, 56, 45,               // 30-34
  36, 29, 23, 18, 15                // 35-39
};

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.

struct eevdf_data {
  uint64 min_vruntime;
  uint sum_weight;
  uint64 sum_weighted_diff;
};

// EEVDF 데이터 수집 함수
// 시스템의 현재 상태에 대한 '스냅샷'을 만듦
static void
collect_eevdf_data(struct eevdf_data *data)
{
  struct proc *p;
  
  // 데이터 초기화
  data->min_vruntime = (uint64)-1; // uint64의 최대값으로 초기화
  data->sum_weight = 0;
  data->sum_weighted_diff = 0;

  // 첫 번째 순회: 모든 RUNNABLE/RUNNING 프로세스를 대상으로
  // 가장 작은 vruntime과 모든 weight의 합을 찾음
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == RUNNABLE || p->state == RUNNING) {
      data->sum_weight += p->weight;
      if(p->vruntime < data->min_vruntime)
        data->min_vruntime = p->vruntime;
    }
    release(&p->lock);
  }

  // 실행 가능한 프로세스가 하나도 없으면 min_vruntime을 0으로 설정
  if(data->min_vruntime == (uint64)-1)
    data->min_vruntime = 0;

  // 두 번째 순회: 위에서 찾은 min_vruntime을 사용하여
  // 가중치가 적용된 vruntime 차이의 총합을 계산
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == RUNNABLE || p->state == RUNNING) {
      // (v_j - v_0) * w_j 의 총합
      data->sum_weighted_diff += (p->vruntime - data->min_vruntime) * p->weight;
    }
    release(&p->lock);
  }
}

int
getnice(int pid)
{
 struct proc *p;
 int nice = -1;

 for(p=proc; p<&proc[NPROC]; p++){
  acquire(&p->lock);
  if(p->pid == pid){
   nice = p->nice;
   release(&p->lock);
   return nice;
  }
  release(&p->lock);
 }
  return nice;
}

int
setnice(int pid, int n)
{
 struct proc *p;
 
 for(p=proc; p<&proc[NPROC]; p++){
  acquire(&p->lock);
  if(p->pid == pid && n>=0 && n<=39){
   p->nice = n;
   release(&p->lock);
   return 0;
  }
  release(&p->lock);
 }
  return -1;
}

void
ps(int pid)
{
  static char *states[] = {
    [UNUSED]    "unused",
    [USED]      "used",
    [SLEEPING]  "sleep ",
    [RUNNABLE]  "runble",
    [RUNNING]   "run   ",
    [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  if (pid == 0) {
    uint total_militicks;
    acquire(&tickslock);
    total_militicks = ticks * 1000;
    release(&tickslock);
    printf("\ntick: %d\n", total_militicks);

    printf("\nname\tpid\tstate\tpriority\truntime/weight\truntime\tvruntime\t  vdeadline\t    is_eligible\ttick %d\n", total_militicks);
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == UNUSED) {
        release(&p->lock);
        continue;
      }
      if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
        state = states[p->state];
      else
        state = "???";
      printf("%s\t%d\t%s\t%d\t%d\t%d\t%d\t  %d\t    %s\n",
          p->name,
          p->pid,
          state,
          p->nice,
          (p->weight > 0 ? p->runtime / p->weight : 0), // 0으로 나누는 것 방지
          p->runtime * 1000,
          p->vruntime * 1000,
          p->vdeadline * 1000,
          (p->is_eligible ? "true" : "false"));

      release(&p->lock);
    }
  }
  else {
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->pid == pid && p->state != UNUSED) {
        printf("\n%s %s %s %s\n", "NAME", "PID", "STATE", "PRIORITY");

        if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
          state = states[p->state];
        else
          state = "???";

        printf("%s %d %s %d\n", p->name, p->pid, state, p->nice);

        release(&p->lock);
        return;
      }
      release(&p->lock);
    }
  }
} 

int
waitpid(int pid)
{
  struct proc *pp;
  struct proc *p = myproc();
  
  acquire(&wait_lock); 

  for(;;){
    int found_child = 0;
    
    for(pp = proc; pp < &proc[NPROC]; pp++){
      acquire(&pp->lock);

      if(pp->pid == pid && pp->parent == p){
        found_child = 1;

        if(pp->state == ZOMBIE){
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return 0;
        }

        release(&pp->lock);
        break;
      }
      release(&pp->lock);
    }

    if(!found_child || killed(p)){
        release(&wait_lock);
        return -1;
    }
    
    sleep(p, &wait_lock);
  }
}


void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");

  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  p->nice = NICE_DEFAULT;
  p->weight = nice_to_weight[NICE_DEFAULT]; // init을 위한 초기화
  p->vruntime = 0; // init을 위한 초기화
  p->vdeadline = 0;
  p->is_eligible = 0;
  p->timeslice = TIME_SLICE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  np->parent = p;
  *np->trapframe = *p->trapframe;

  np->nice = p->nice;
  np->weight = p->weight;
  np->vruntime = p->vruntime;
  np->vdeadline = 0;


  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  struct proc *best = 0; // 가장 실행하기 좋은 프로세스를 담을 변수
  struct eevdf_data data;

  c->proc = 0;

  for(;;){
    intr_on();

    // 스케줄링 판단에 필요한 데이터 스냅샷을 수집
    collect_eevdf_data(&data);
    best = 0;

    // 모든 프로세스를 순회하며 가장 좋은 후보(best) 찾기
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Eligibility 계산
        int eligible = 1; // 기본값은 '자격 있음'
        if(data.sum_weight > 0) {
          uint64 p_diff = (p->vruntime - data.min_vruntime) * data.sum_weight;
          uint64 avg_diff = data.sum_weighted_diff;
          eligible = (avg_diff >= p_diff);
        }
        
        if(eligible) {
          // 실행 자격이 있다면, 현재까지 찾은 best 후보와 vdeadline을 비교
          if(best == 0 || p->vdeadline < best->vdeadline) {
            // p가 더 좋은 후보라면, 기존 best 후보의 lock은 풀어주고
            if(best)
              release(&best->lock);
            // p를 새로운 best 후보로 삼음 (p의 lock은 계속 잡고 있음)
            best = p;
          } else {
            release(&p->lock);
          }
        } else {
          release(&p->lock);
        }
      } else {
        release(&p->lock);
      }
    }

    // 가장 좋은 후보를 찾았다면 실행
    if(best) {
      p = best; // 이제 p는 실행될 프로세스
      
      // p의 lock은 이미 잡혀있는 상태
      p->state = RUNNING;
      c->proc = p;

      // context switch
      swtch(&c->context, &p->context);

      // --- 프로세스가 실행을 멈추고 제어권이 여기로 돌아온 후 ---
      c->proc = 0;
      
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);

    if(p->state == SLEEPING && p->chan == chan) {
      p->is_eligible = 0; 
      p->timeslice = 0; 
      p->vdeadline = p->vruntime + (TIME_SLICE * nice_to_weight[NICE_DEFAULT]) / p->weight;
      
      p->state = RUNNABLE;
    }
    
    release(&p->lock);
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
