#include <stddef.h>

#include <sys/prctl.h>
#include <sys/syscall.h>

#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

enum { THIS_ARCH =
#ifdef __x86_64__
    AUDIT_ARCH_X86_64
#else
#error unhandled arch
#endif
};

enum 
{
    N_RULES = 7,
    RULE_OK = N_RULES - 3,
    RULE_KO = N_RULES - 2,
    RULE_ER = N_RULES - 1
};

#define J(dst, src) ((dst) - (src) - 1)

static struct sock_filter filter[N_RULES] = 
{
    [0] = BPF_STMT (BPF_LD  | BPF_ABS, offsetof (struct seccomp_data, arch)),
    [1] = BPF_JUMP (BPF_JMP | BPF_JEQ, THIS_ARCH, 0, J(RULE_KO, 1)),

    [2] = BPF_STMT (BPF_LD  | BPF_ABS, offsetof (struct seccomp_data, nr)),
    [3] = BPF_JUMP (BPF_JMP | BPF_JEQ, SYS_rseq,  J(RULE_ER, 3), 0),

    [RULE_OK] = BPF_STMT (BPF_RET, SECCOMP_RET_ALLOW),
    [RULE_KO] = BPF_STMT (BPF_RET, SECCOMP_RET_TRAP),
    [RULE_ER] = BPF_STMT (BPF_RET, SECCOMP_RET_ERRNO | ENOSYS)
};

int seccomp_install(void) // instead of prctl() you may use seccomp()
{
    struct sock_fprog s = { N_RULES, filter };
    prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    return prctl (PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &s, 0, 0);
}

void errorexec (const char* name)
{
    fprintf (stderr, "Executing %s error. ", name);
    perror ("execvp");
    exit (1);
}

int main (int argc, char** argv)
{
    if (seccomp_install())
    {
        perror ("prctl");
        return 1;
    }

    if (argc > 1)
    {
        execvp (argv[1], argv + 1);
        errorexec (argv[1]);
    }

    char* shell_name = getenv ("SHELL");
    if (!shell_name)
        shell_name = "/bin/sh";
    char* shell_argv[] = {shell_name, NULL};
    execvp (shell_name, shell_argv);
    errorexec (shell_name);
}
