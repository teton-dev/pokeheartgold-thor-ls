// Backtraces for scripted test runs on desktop Linux (port_test_script.h): gdb can't run in the Docker container
// under Rosetta, so a crash, or a run the harness stops at its time limit, prints the game thread's backtrace as
// addresses from the executable's start, which tools/docker/desktop.sh turns into functions and lines.
// From the Platinum fork's src/port/test_crash.cpp, in C.

#define _GNU_SOURCE
#include "port_test_script.h"

#if defined(SDK_BUILD_LINUX) && !defined(SDK_BUILD_ANDROID)

#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

// The game's files are compiled with a forced-in precompiled header, so _GNU_SOURCE may come too late for this
#if defined(__x86_64__) && !defined(REG_RIP)
#define REG_RIP 16
#define REG_RSP 15
#endif

// Where the linker put the executable's start (GNU ld's symbol)
extern char __executable_start;

// The game's thread, which a hang's backtrace is of
static pthread_t sGameThread;

static void WriteLine(const char *line, int length) {
    if (write(STDOUT_FILENO, line, length) < 0) {
        // Nothing to do about it in a signal handler
    }
}

static void PrintBacktrace(const char *what) {
    char line[96];
    const uintptr_t base = (uintptr_t)&__executable_start;
    int length = snprintf(line, sizeof(line), "[test] %s, base %#lx\n", what, (unsigned long)base);
    WriteLine(line, length);
    void *frames[64];
    const int count = backtrace(frames, 64);
    for (int i = 0; i < count; i++) {
        length = snprintf(line, sizeof(line), "[test] frame %#lx\n", (unsigned long)((uintptr_t)frames[i] - base));
        WriteLine(line, length);
    }
}

static void OnCrash(int number, siginfo_t *info, void *context) {
    char what[32];
    snprintf(what, sizeof(what), "CRASH signal %d", number);
#if defined(__x86_64__)
    // Where it crashed first: the unwinder can crash in a corrupt stack, and nothing is printed then
    const ucontext_t *ucontext = (const ucontext_t *)context;
    const uintptr_t base = (uintptr_t)&__executable_start;
    const uintptr_t pc = ucontext->uc_mcontext.gregs[REG_RIP];
    char line[128];
    int length = snprintf(line, sizeof(line), "[test] CRASH signal %d at %#lx, base %#lx, address %p\n", number,
        (unsigned long)(pc - base), (unsigned long)base, info->si_addr);
    WriteLine(line, length);
#endif
    PrintBacktrace(what);
#if defined(__x86_64__)
    // Where it crashed, and the return address on top of the stack: what's left of the backtrace when the crash is a
    // call to a bad address, which the unwinder can't get past
    const uintptr_t top = *(const uintptr_t *)ucontext->uc_mcontext.gregs[REG_RSP];
    length = snprintf(line, sizeof(line), "[test] frame %#lx\n[test] frame %#lx\n", (unsigned long)(pc - base),
        (unsigned long)(top - base));
    WriteLine(line, length);
#else
    (void)info;
    (void)context;
#endif
    _exit(1);
}

// One sample of where the game is: the harness takes a few, then stops the run
static void OnHang(int number) {
    if (!pthread_equal(pthread_self(), sGameThread)) {
        pthread_kill(sGameThread, number);
        return;
    }
    PrintBacktrace("HANG");
}

static BOOL sExitExpected;

void TestScript_ExitExpected(void) {
    sExitExpected = TRUE;
}

// Who quits: a run that ends without the script saying so shows where from
static void OnExit(void) {
    if (!sExitExpected) {
        PrintBacktrace("EXIT");
    }
}

void TestScript_InstallCrashHandlers(void) {
    sGameThread = pthread_self();
    atexit(OnExit);
    // On a stack of their own, so that a stack overflow's backtrace can be taken
    static char signalStack[1 << 16];
    stack_t stack;
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = signalStack;
    stack.ss_size = sizeof(signalStack);
    sigaltstack(&stack, NULL);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_flags = SA_ONSTACK | SA_RESTART;
    action.sa_handler = OnHang;
    sigaction(SIGQUIT, &action, NULL);
    action.sa_flags |= SA_SIGINFO;
    action.sa_sigaction = OnCrash;
    const int crashes[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for (int i = 0; i < (int)(sizeof(crashes) / sizeof(crashes[0])); i++) {
        sigaction(crashes[i], &action, NULL);
    }
}

#else

void TestScript_ExitExpected(void) {
}

void TestScript_InstallCrashHandlers(void) {
}

#endif
