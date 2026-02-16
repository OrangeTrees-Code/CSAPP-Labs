/*
 * tsh - A tiny shell program with job control
 *
 * <Put your name and login ID here>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "csapp.h"
#include <stdarg.h>
#include <unistd.h>

void sio_put(const char *fmt, ...)
{
    char buf[1024];

    va_list ap;
    va_start(ap, fmt);

    // 注意：vsnprintf 本身不是严格 async-signal-safe
    // 但在 CSAPP tsh 里属于可接受实现
    vsnprintf(buf, sizeof(buf), fmt, ap);

    va_end(ap);

    write(STDOUT_FILENO, buf, strlen(buf));
}

/* Misc manifest constants */
// #define MAXLINE 1024   /* max line size */
#define MAXARGS 128    /* max args on a command line */
#define MAXJOBS 16     /* max jobs at any point in time */
#define MAXJID 1 << 16 /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;   /* defined in libc */
char prompt[] = "tsh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0;         /* if true, print additional output */
int nextjid = 1;         /* next job ID to allocate */
char sbuf[MAXLINE];      /* for composing sprintf messages */

struct job_t
{                          /* The job struct */
    pid_t pid;             /* job PID */
    int jid;               /* job ID [1, 2, ...] */
    int state;             /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE]; /* command line */
};
struct job_t jobs[MAXJOBS];

struct cmdline_tokens
{
    int argc;
    char *argv[MAXARGS];
    char *inFIle;
    char *outFile;
    enum E_builtin_cmd
    {
        BUILTIN_NONE,
        BUILTIN_QUIT,
        BUILTIN_JOBS,
        BUILTIN_BG,
        BUILTIN_FG,
        BUILTIN_KILL,
        BUILTIN_NOHUP
    } builtins;
};

struct STDIO_FILE_DES
{
    int std_input;
    int std_output;
    int inputFile;
    int outputFile;
};
/* The job list */
/* End global variables */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, struct cmdline_tokens *tok);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);
pid_t Fork(void);
struct STDIO_FILE_DES redirectIO(struct cmdline_tokens *tok);
void recoverIO(struct STDIO_FILE_DES *DES, struct cmdline_tokens *tok);
void eval_none(struct cmdline_tokens *tok, char *cmdline, int bg);
void eval_bgOrfg(struct cmdline_tokens *tok, int isBg);
void eval_kill(struct cmdline_tokens *tok);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF)
    {
        switch (c)
        {
        case 'h': /* print help message */
            usage();
            break;
        case 'v': /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':            /* don't print a prompt */
            emit_prompt = 0; /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT, sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1)
    {

        /* Read command line */
        if (emit_prompt)
        {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin))
        { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */
void eval(char *cmdline)
{
    int bg;
    struct cmdline_tokens tok;
    struct STDIO_FILE_DES IO = redirectIO(&tok);

    bg = parseline(cmdline, &tok);

    if (bg == 0)
    {
        return;
    }

    if ((tok.argv)[0] == NULL)
    {
        return;
    }

    switch (tok.builtins)
    {
    case BUILTIN_NONE:
        eval_none(&tok, cmdline, bg);
        break;

    case BUILTIN_QUIT:
        exit(0);
        break;
    case BUILTIN_JOBS:
        listjobs(jobs);
        break;
    case BUILTIN_BG:
        eval_bgOrfg(&tok, 1);
        break;
    case BUILTIN_FG:
        eval_bgOrfg(&tok, 0);
        break;
    case BUILTIN_KILL:
        eval_kill(&tok);
        break;
    case BUILTIN_NOHUP:
        sigset_t prev, mask;
        Sigemptyset(&mask);
        Sigaddset(&mask, SIGHUP);
        Sigprocmask(SIG_BLOCK, &mask, &prev);
        eval(cmdline + 6);
        Sigprocmask(SIG_SETMASK, &prev, NULL);
        break;
    default:
        break;
    }
    recoverIO(&IO, &tok);
    return;
}

void eval_none(struct cmdline_tokens *tok, char *cmdline, int bg)
{
    pid_t pid;

    sigset_t prev, block;
    Sigaddset(&block, SIGCHLD);
    Sigaddset(&block, SIGINT);
    Sigaddset(&block, SIGTSTP);

    Sigprocmask(SIG_BLOCK, &block, &prev);
    if ((pid = fork()) == 0)
    {
        Setpgid(0, 0);
        Sigprocmask(SIG_SETMASK, &prev, NULL);
        if (execve(tok->argv[0], tok->argv, environ) < 0)
        {
            printf("%s: Command not found.\n", tok->argv[0]);
            exit(0);
        }
    }

    else
    {
        addjob(jobs, pid, bg ? BG : FG, cmdline);
        Sigprocmask(SIG_SETMASK, &prev, NULL);
        if (!bg)
        {
            // int status;
            while (pid == fgpid(jobs))
            {
                sigsuspend(&prev);
            }
        }
        else
        {
            printf("[%d] (%d) %s\n", pid2jid(pid), pid, cmdline);
        }
    }
}

void eval_bgOrfg(struct cmdline_tokens *tok, int isBg)
{
    int jid;
    pid_t pid;
    struct job_t *job;

    if (tok->argv[1][0] == '%')
    {
        jid = atoi(tok->argv[1] + 1);
        job = getjobjid(jobs, jid);
    }
    else
    {
        pid = atoi(tok->argv[1]);
        job = getjobpid(jobs, pid);
    }

    pid = job->pid;
    Kill(pid, SIGCONT);
    if (isBg)
    {
        job->state = BG;
        printf("[%d] (%d) %s\n", job->jid, job->pid, job->cmdline);
    }
    else
    {
        job->state = FG;
        sigset_t block;
        Sigemptyset(&block);
        while (pid == fgpid(jobs))
        {
            sigsuspend(&block);
        }
    }
}

void eval_kill(struct cmdline_tokens *tok)
{
    struct job_t *job;
    pid_t pid;
    int jid;

    if (tok->argv[1][0] == '%')
    {
        jid = atoi(tok->argv[1] + 1);
        jid = jid > 0 ? jid : -jid;
        job = getjobjid(jobs, jid);
        if (job == NULL)
        {
            printf("%%%d: No such job\n", jid);
            return;
        }
    }
    else
    {
        pid = atoi(tok->argv[1]);
        pid = pid > 0 ? pid : -pid;
        job = getjobpid(jobs, pid);
    }
    pid = job->pid;

    Kill(pid, SIGTERM);
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, struct cmdline_tokens *tok)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' ';   /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'')
    {
        buf++;
        delim = strchr(buf, '\'');
    }
    else
    {
        delim = strchr(buf, ' ');
    }

    while (delim)
    {
        tok->argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'')
        {
            buf++;
            delim = strchr(buf, '\'');
        }
        else
        {
            delim = strchr(buf, ' ');
        }
    }
    tok->argv[argc] = NULL;

    if (argc == 0) /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*tok->argv[argc - 1] == '&')) != 0)
    {
        tok->argv[--argc] = NULL;
    }

    if (strcmp(tok->argv[0], "jobs"))
    {
        tok->builtins = BUILTIN_JOBS;
    }
    else if (strcmp(tok->argv[0], "quit"))
    {
        tok->builtins = BUILTIN_QUIT;
    }
    else if (strcmp(tok->argv[0], "bg"))
    {
        tok->builtins = BUILTIN_BG;
    }
    else if (strcmp(tok->argv[0], "fg"))
    {
        tok->builtins = BUILTIN_FG;
    }
    else if (strcmp(tok->argv[0], "kill"))
    {
        tok->builtins = BUILTIN_KILL;
    }
    else if (strcmp(tok->argv[0], "nohup"))
    {
        tok->builtins = BUILTIN_NOHUP;
    }
    else if (strcmp(tok->argv[0], "quit"))
    {
        tok->builtins = BUILTIN_QUIT;
    }
    else
    {
        tok->builtins = BUILTIN_NONE;
    }
    return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv)
{
    return 0; /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
    return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    return;
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig)
{
    pid_t pid;
    int status;
    sigset_t mask, prev;
    struct job_t *job;
    int oldErrorno = errno;
    Sigfillset(&mask);

    while ((pid = Waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
    {
        job = getjobpid(jobs, pid);
        if (WIFEXITED(status))
        {
            Sigprocmask(SIG_SETMASK, &mask, &prev);
            deletejob(jobs, job->pid);
            Sigprocmask(SIG_SETMASK, &prev, NULL);
        }
        if (WIFSIGNALED(status))
        {
            sio_put("Job [%d] (%d) terminated by signal %d\n", job->jid, pid, WTERMSIG(status));
            Sigprocmask(SIG_SETMASK, &mask, &prev);
            deletejob(jobs, job->pid);
            Sigprocmask(SIG_SETMASK, &prev, NULL);
        }
        if (WIFSTOPPED(status))
        {
            sio_put("Job [%d] (%d) terminated by signal %d\n", job->jid, pid, WTERMSIG(status));
            Sigprocmask(SIG_SETMASK, &mask, &prev);
            job->state = ST;
            Sigprocmask(SIG_SETMASK, &prev, NULL);
        }
        if (WIFCONTINUED(status) && job->state == ST)
        {
            Sigprocmask(SIG_SETMASK, &mask, &prev);
            job->state = BG;
            Sigprocmask(SIG_SETMASK, &prev, NULL);
        }
    }

    if (pid < 0 && oldErrorno == ECHILD)
    {
        unix_error("waitpid error");
    }

    errno = oldErrorno;

    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    int oldErrorno = errno;
    pid_t pid = fgpid(jobs);
    if (pid)
    {
        Kill(pid, SIGINT);
    }
    errno = oldErrorno;
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    int oldErrorno = errno;
    pid_t pid = fgpid(jobs);
    if (pid)
    {
        Kill(pid, SIGTSTP);
    }
    errno = oldErrorno;
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job)
{
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max = 0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid == 0)
        {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose)
            {
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid == pid)
        {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs) + 1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
        {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid != 0)
        {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state)
            {
            case BG:
                printf("Running ");
                break;
            case FG:
                printf("Foreground ");
                break;
            case ST:
                printf("Stopped ");
                break;
            default:
                printf("listjobs: Internal error: job[%d].state=%d ",
                       i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/

/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
// void unix_error(char *msg)
// {
//     fprintf(stdout, "%s: %s\n", msg, strerror(errno));
//     exit(1);
// }

/*
 * app_error - application-style error routine
 */
// void app_error(char *msg)
// {
//     fprintf(stdout, "%s\n", msg);
//     exit(1);
// }

/*
 * Signal - wrapper for the sigaction function
 */
// handler_t *Signal(int signum, handler_t *handler)
// {
//     struct sigaction action, old_action;

//     action.sa_handler = handler;
//     sigemptyset(&action.sa_mask); /* block sigs of type being handled */
//     action.sa_flags = SA_RESTART; /* restart syscalls if possible */

//     if (sigaction(signum, &action, &old_action) < 0)
//         unix_error("Signal error");
//     return (old_action.sa_handler);
// }

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

// pid_t Fork(void)
// {
//     pid_t pid;
//     if ((pid = fork()) < 0)
//     {
//         unix_error("Fork error");
//     }
//     return pid;
// }

struct STDIO_FILE_DES redirectIO(struct cmdline_tokens *tok)
{
    int inputFile = STDIN_FILENO;
    int outputFile = STDOUT_FILENO;

    if (tok->inFIle)
    {
        inputFile = Open(tok->inFIle, O_RDONLY, 0);
    }
    if (tok->outFile)
    {
        outputFile = Open(tok->outFile, O_WRONLY, 0);
    }

    struct STDIO_FILE_DES DES;
    DES.std_input = dup(STDIN_FILENO);
    DES.std_output = dup(STDOUT_FILENO);
    DES.inputFile = inputFile;
    DES.outputFile = outputFile;

    Dup2(inputFile, STDIN_FILENO);
    Dup2(outputFile, STDOUT_FILENO);

    return DES;
}

void recoverIO(struct STDIO_FILE_DES *DES, struct cmdline_tokens *tok)
{
    if (tok->inFIle)
    {
        Close(DES->inputFile);
        Dup2(DES->std_input, STDIN_FILENO);
    }
    if (tok->outFile)
    {
        Close(DES->outputFile);
        Dup2(DES->std_output, STDOUT_FILENO);
    }
}
