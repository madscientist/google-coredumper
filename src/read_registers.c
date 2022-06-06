#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <errno.h>

typedef struct arm64_regs { /* General purpose registers                 */
#define BP uregs[29]        /* Frame pointer                             */
#define LR uregs[30]        /* Link register                             */
#define SP sp               /* Stack pointer                             */
    unsigned long long uregs[31];
    unsigned long long sp;
    unsigned long long pc;
    unsigned long long pstate;
} arm64_regs;

  typedef struct Frame {
    struct arm64_regs arm;
    int               errno_;
    pid_t             tid;
  } Frame;
  #define FRAME(f) Frame f;                                           \
                   do {                                               \
                     long cpsr;                                       \
                     f.errno_ = errno;                                \
                     f.tid    = syscall(SYS_gettid);                  \
                     __asm__ volatile(                                \
                       "stp x0, x1, [%0]\n"                           \
                       : : "r"(&f.arm) : "memory");                   \
                     f.arm.uregs[16] = 0;                             \
                     __asm__ volatile(                                \
                       "mrs %0, cpsr\n"       /* Condition code reg */\
                       : "=r"(cpsr));                                 \
                     f.arm.uregs[17] = cpsr;                          \
                   } while (0)
  #define SET_FRAME(f,r)                                              \
                     do {                                             \
                       /* Don't override the FPU status register.   */\
                       /* Use the value obtained from ptrace(). This*/\
                       /* works, because our code does not perform  */\
                       /* any FPU operations, itself.               */\
                       long fps      = (f).arm.uregs[16];             \
                       errno         = (f).errno_;                    \
                       (r)           = (f).arm;                       \
                       (r).uregs[16] = fps;                           \
                     } while (0)


int ListAllProcessThreads(void *frame, const char* file_name)
{
    int main_pid = ((Frame *)frame)->tid;

}

int WriteCoreDump(const char *file_name)
{
    FRAME(frame);
}

int main()
{
    return 0;
}
