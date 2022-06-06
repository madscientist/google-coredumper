#include <asm/hwcap.h>
#include <stdio.h>
#include <sys/auxv.h>

int main()
{
#if defined(__ARM_ARCH_3__)
    printf("__ARM_ARCH_3__\n");
#endif
#if defined(__ARM_EABI__)
    printf("__ARM_EABI__\n");
#endif
#if defined(__aarch64__)
    printf("__aarch64__\n");
#endif

    long hwcaps = getauxval(AT_HWCAP);
    printf("AT_HWCAP: 0x%X\n", hwcaps);

    if (hwcaps & HWCAP_FP) {
        printf("HWCAP_FP is available\n");
    }
    if (hwcaps & HWCAP_ASIMD) {
        printf("HWCAP_ASIMD is available\n");
    }
    if (hwcaps & HWCAP_EVTSTRM) {
        printf("HWCAP_EVTSTRM is available\n");
    }
    if (hwcaps & HWCAP_AES) {
        printf("HWCAP_AES is available\n");
    }
    if (hwcaps & HWCAP_PMULL) {
        printf("HWCAP_PMULL is available\n");
    }
    if (hwcaps & HWCAP_SHA1) {
        printf("HWCAP_SHA1 is available\n");
    }
    if (hwcaps & HWCAP_SHA2) {
        printf("HWCAP_SHA2 is available\n");
    }
    if (hwcaps & HWCAP_CRC32) {
        printf("HWCAP_CRC32 is available\n");
    }
    if (hwcaps & HWCAP_ATOMICS) {
        printf("HWCAP_ATOMICS is available\n");
    }
    if (hwcaps & HWCAP_FPHP) {
        printf("HWCAP_FPHP is available\n");
    }
    if (hwcaps & HWCAP_ASIMDHP) {
        printf("HWCAP_ASIMDHP is available\n");
    }
    if (hwcaps & HWCAP_CPUID) {
        printf("HWCAP_CPUID is available\n");
    }
    if (hwcaps & HWCAP_ASIMDRDM) {
        printf("HWCAP_ASIMDRDM is available\n");
    }
    if (hwcaps & HWCAP_JSCVT) {
        printf("HWCAP_JSCVT is available\n");
    }
    if (hwcaps & HWCAP_FCMA) {
        printf("HWCAP_FCMA is available\n");
    }
    if (hwcaps & HWCAP_LRCPC) {
        printf("HWCAP_LRCPC is available\n");
    }
    if (hwcaps & HWCAP_DCPOP) {
        printf("HWCAP_DCPOP is available\n");
    }
    if (hwcaps & HWCAP_SHA3) {
        printf("HWCAP_SHA3 is available\n");
    }
    if (hwcaps & HWCAP_SM3) {
        printf("HWCAP_SM3 is available\n");
    }
    if (hwcaps & HWCAP_SM4) {
        printf("HWCAP_SM4 is available\n");
    }
    if (hwcaps & HWCAP_ASIMDDP) {
        printf("HWCAP_ASIMDDP is available\n");
    }
    if (hwcaps & HWCAP_SHA512) {
        printf("HWCAP_SHA512 is available\n");
    }
    if (hwcaps & HWCAP_SVE) {
        printf("HWCAP_SVE is available\n");
    }
    if (hwcaps & HWCAP_ASIMDFHM) {
        printf("HWCAP_ASIMDFHM is available\n");
    }
    if (hwcaps & HWCAP_DIT) {
        printf("HWCAP_DIT is available\n");
    }
    if (hwcaps & HWCAP_USCAT) {
        printf("HWCAP_USCAT is available\n");
    }
    if (hwcaps & HWCAP_ILRCPC) {
        printf("HWCAP_ILRCPC is available\n");
    }
    if (hwcaps & HWCAP_FLAGM) {
        printf("HWCAP_FLAGM is available\n");
    }
    if (hwcaps & HWCAP_SSBS) {
        printf("HWCAP_SSBS is available\n");
    }
    if (hwcaps & HWCAP_SB) {
        printf("HWCAP_SB is available\n");
    }
    if (hwcaps & HWCAP_PACA) {
        printf("HWCAP_PACA is available\n");
    }
    if (hwcaps & HWCAP_PACG) {
        printf("HWCAP_PACG is available\n");
    }

    hwcaps = getauxval(AT_HWCAP2);
    printf("AT_HWCAP2: 0x%X\n", hwcaps);

    if (hwcaps & HWCAP2_DCPODP) {
        printf("HWCAP2_DCPODP is available\n");
    }
    if (hwcaps & HWCAP2_SVE2) {
        printf("HWCAP2_SVE2 is available\n");
    }
    if (hwcaps & HWCAP2_SVEAES) {
        printf("HWCAP2_SVEAES is available\n");
    }
    if (hwcaps & HWCAP2_SVEPMULL) {
        printf("HWCAP2_SVEPMULL is available\n");
    }
    if (hwcaps & HWCAP2_SVEBITPERM) {
        printf("HWCAP2_SVEBITPERM is available\n");
    }
    if (hwcaps & HWCAP2_SVESHA3) {
        printf("HWCAP2_SVESHA3 is available\n");
    }
    if (hwcaps & HWCAP2_SVESM4) {
        printf("HWCAP2_SVESM4 is available\n");
    }
    if (hwcaps & HWCAP2_FLAGM2) {
        printf("HWCAP2_FLAGM2 is available\n");
    }
    if (hwcaps & HWCAP2_FRINT) {
        printf("HWCAP2_FRINT is available\n");
    }
    if (hwcaps & HWCAP2_SVEI8MM) {
        printf("HWCAP2_SVEI8MM is available\n");
    }
    if (hwcaps & HWCAP2_SVEF32MM) {
        printf("HWCAP2_SVEF32MM is available\n");
    }
    if (hwcaps & HWCAP2_SVEF64MM) {
        printf("HWCAP2_SVEF64MM is available\n");
    }
    if (hwcaps & HWCAP2_SVEBF16) {
        printf("HWCAP2_SVEBF16 is available\n");
    }
    if (hwcaps & HWCAP2_I8MM) {
        printf("HWCAP2_I8MM is available\n");
    }
    if (hwcaps & HWCAP2_BF16) {
        printf("HWCAP2_BF16 is available\n");
    }
    if (hwcaps & HWCAP2_DGH) {
        printf("HWCAP2_DGH is available\n");
    }
    if (hwcaps & HWCAP2_RNG) {
        printf("HWCAP2_RNG is available\n");
    }
    if (hwcaps & HWCAP2_BTI) {
        printf("HWCAP2_BTI is available\n");
    }
    if (hwcaps & HWCAP2_MTE) {
        printf("HWCAP2_MTE is available\n");
    }
    if (hwcaps & HWCAP2_ECV) {
        printf("HWCAP2_ECV is available\n");
    }
    if (hwcaps & HWCAP2_AFP) {
        printf("HWCAP2_AFP is available\n");
    }
    if (hwcaps & HWCAP2_RPRES) {
        printf("HWCAP2_RPRES is available\n");
    }

    return 0;
}
