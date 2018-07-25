#pragma once

#include <switch.h>
#include <stdio.h>

#define CTR_PTHREAD_STACK_SIZE 0x10000

static void *(*start_routine_jump)(void *);

static void switch_thread_launcher(void *data)
{
    void *(*start_routine_jump_safe)(void *) = start_routine_jump;
    start_routine_jump_safe(data);
    return;
}


Thread handle;


static inline int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg)
{
    SysPrintf(_("pthread_create"));
    start_routine_jump = start_routine;
    Result rc = threadCreate(&handle.handle, start_routine, arg, 0x10000, 0x25, 1);
    if (R_FAILED(rc))
    {
        printf("pthread_create failed\n");
        return 1;
    }

    printf("XXX pthread_create\n");
    return 0;
}

static inline int pthread_join(pthread_t thread, void **retval)
{
    SysPrintf(_("XXX pthread_join"));
    threadWaitForExit(&handle.handle);
    threadClose(&handle.handle);

    return 0;
}

static inline void pthread_exit(void *retval)
{
    SysPrintf(_("XXX pthread_exit"));
    svcExitThread();
}

