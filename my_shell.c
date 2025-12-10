#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MAX 256
#define MAXARGS 16

// 去空格（简单版）
static char *skip(char *p) {
    while (*p == ' ' || *p=='\t') p++;
    return p;
}

// 把一段字符串拆成 argv
static int split(char *s, char *argv[]) {
    int i = 0;
    s = skip(s);
    while (*s && i < MAXARGS-1) {
        argv[i++] = s;
        while (*s && *s!=' ' && *s!='\t')
            s++;
        if (*s == 0) break;
        *s++ = 0;
        s = skip(s);
    }
    argv[i] = 0;
    return i;
}

// 执行纯命令（无管道）
static void runcmd_simple(char *cmd) {
    char *argv[MAXARGS];
    int n = split(cmd, argv);
    if (n == 0) return;

    if (strcmp(argv[0], "cd") == 0) {
        if (n < 2) {
            fprintf(2, "cd: missing arg\n");
        } else if (chdir(argv[1]) < 0) {
            fprintf(2, "cd: cannot cd %s\n", argv[1]);
        }
        return;
    }

    if (strcmp(argv[0], "exit") == 0)
        exit(0);

    int pid = fork();
    if (pid == 0) {
        exec(argv[0], argv);
        fprintf(2, "exec: %s failed\n", argv[0]);
        exit(1);
    }
    wait(0);
}

// 支持 < >
static void runcmd_redirect(char *cmd) {
    char *inpos = 0, *outpos = 0;
    char *p = cmd;

    // 找 < >
    while (*p) {
        if (*p == '<') inpos = p;
        else if (*p == '>') outpos = p;
        p++;
    }

    if (inpos==0 && outpos==0) {
        runcmd_simple(cmd);
        return;
    }

    int fdin=-1, fdout=-1;

    if (inpos) {
        *inpos = 0;
        char *f = skip(inpos+1);
        char *q = f;
        while (*q && *q!=' ') q++;
        *q = 0;
        fdin = open(f, O_RDONLY);
        if (fdin < 0) {
            fprintf(2, "cannot open %s\n", f);
            return;
        }
    }

    if (outpos) {
        *outpos = 0;
        char *f = skip(outpos+1);
        char *q = f;
        while (*q && *q!=' ') q++;
        *q = 0;
        fdout = open(f, O_WRONLY|O_CREATE|O_TRUNC);
        if (fdout < 0) {
            fprintf(2, "cannot open %s\n", f);
            return;
        }
    }

    int pid = fork();
    if (pid == 0) {
        if (fdin>=0) { close(0); dup(fdin); }
        if (fdout>=0){ close(1); dup(fdout); }
        runcmd_simple(cmd);
        exit(0);
    }

    if (fdin>=0) close(fdin);
    if (fdout>=0) close(fdout);
    wait(0);
}

// 管道，支持多段
static void runcmd_pipe(char *cmd) {
    char *parts[10];
    int i = 0;

    // 手动拆 "|"
    parts[i++] = cmd;
    for (char *p = cmd; *p; p++) {
        if (*p == '|') {
            *p = 0;
            parts[i++] = p+1;
        }
    }
    int n = i;

    if (n == 1) {
        runcmd_redirect(cmd);
        return;
    }

    int fd[10][2];

    for (i = 0; i < n-1; i++)
        pipe(fd[i]);

    for (i = 0; i < n; i++) {
        int pid = fork();
        if (pid == 0) {
            if (i > 0) {
                close(0);
                dup(fd[i-1][0]);
            }
            if (i < n-1) {
                close(1);
                dup(fd[i][1]);
            }

            // 关闭所有 pipe
            for (int k=0; k<n-1; k++) {
                close(fd[k][0]);
                close(fd[k][1]);
            }

            runcmd_redirect(parts[i]);
            exit(0);
        }
    }

    // parent 关闭所有
    for (i=0; i<n-1; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }

    for (i=0; i<n; i++)
        wait(0);
}

// 顺序执行 a ; b ; c
static void runcmd(char *buf) {
    char *p = buf;
    while (*p) {
        char *start = p;
        while (*p && *p!=';') p++;
        if (*p == ';') {
            *p = 0;
            runcmd_pipe(start);
            p++;
        } else {
            runcmd_pipe(start);
            break;
        }
    }
}

int main() {
    static char buf[MAX];

    for (;;) {
        write(2, ">>> ", 4);
        memset(buf, 0, sizeof(buf));

        int n = read(0, buf, sizeof(buf));
        if (n <= 0) break;

        runcmd(buf);
    }

    exit(0);
}
