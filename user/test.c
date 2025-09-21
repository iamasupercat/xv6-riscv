#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(){
  printf(">>> Testing getnice/setnice:\n");
  int me = getpid();
  int n0 = getnice(me);
  printf("self pid=%d nice=%d\n", me, n0);

  int r;
  r = setnice(me, 10);
  printf("setnice(self,10) -> %d; now %d\n", r, getnice(me));

  r = setnice(me, 39);
  printf("setnice(self,39) -> %d; now %d\n", r, getnice(me));

  r = setnice(me, -1);
  printf("setnice(self,-1) -> %d (expect -1)\n", r);

  r = setnice(me, 40);
  printf("setnice(self,40) -> %d (expect -1)\n", r);

  r = getnice(999999);
  printf("getnice(999999) -> %d (expect -1)\n", r);

  printf("\n>>> Testing ps (all):\n");
  ps(0);

  printf("\n>>> Testing meminfo:\n");
  uint64 before = meminfo();
  printf("free before: %d bytes\n", (int)before);
  // sbrk로 메모리를 조금 늘려서 free 감소 확인(환경 따라 차이 있을 수 있음)
  sbrk(4096 * 4); // 16KB
  uint64 after = meminfo();
  printf("free after sbrk(16KB): %d bytes\n", (int)after);

  

  exit(0);
}