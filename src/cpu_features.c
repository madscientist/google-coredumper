#include <asm/hwcap.h>
#include <stdio.h>
#include <sys/auxv.h>

int main() {
  long hwcaps = getauxval(AT_HWCAP);

  if (hwcaps & HWCAP_AES) {
    printf("AES instructions are available\n");
  }
  if (hwcaps & HWCAP_CRC32) {
    printf("CRC32 instructions are available\n");
  }
  if (hwcaps & HWCAP_PMULL) {
    printf("PMULL/PMULL2 instructions that operate on 64-bit data are "
           "available\n");
  }
  if (hwcaps & HWCAP_SHA1) {
    printf("SHA1 instructions are available\n");
  }
  if (hwcaps & HWCAP_SHA2) {
    printf("SHA2 instructions are available\n");
  }
  return 0;
}
