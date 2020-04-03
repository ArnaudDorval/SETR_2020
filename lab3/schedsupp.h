#include <linux/kernel.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>
#include <linux/types.h>
#include <sched.h>
#include <linux/sched.h>
#include <sys/types.h>
#include <cstdio>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SCHED_DEADLINE  6


struct sched_attr {
    __u32 size;

    __u32 sched_policy;
    __u64 sched_flags;

    /* SCHED_NORMAL, SCHED_BATCH */
    __s32 sched_nice;

    /* SCHED_FIFO, SCHED_RR */
    __u32 sched_priority;

    /* SCHED_DEADLINE */
    __u64 sched_runtime;
    __u64 sched_deadline;
    __u64 sched_period;
};

struct sched_param schedParams;
struct sched_attr schedAttr;

int sched_setattr(pid_t pid,
              const struct sched_attr *attr,
              unsigned int flags)
{
    return syscall(380, pid, attr, flags);
}

int sched_getattr(pid_t pid,
              struct sched_attr *attr,
              unsigned int size,
              unsigned int flags)
{
    return syscall(381, pid, attr, size, flags);
}

void setScheduling(int schedType, char *schedArgs)
{

    memset(&schedAttr, 0, sizeof(struct sched_attr));
    schedAttr.size = sizeof(struct sched_attr);
    schedAttr.sched_flags = SCHED_FLAG_RESET_ON_FORK;

    schedAttr.sched_policy = schedType;

    switch (schedType) {
        case SCHED_RR:
            schedAttr.sched_priority = 99;
        break;
        case SCHED_FIFO:
            schedAttr.sched_priority = sched_get_priority_min(SCHED_FIFO);
            break;
        case SCHED_DEADLINE:
            if (schedArgs[0] != 0)
            {
                unsigned long long param[3] = {1, 1, 1};
                int i = 0;

                char *token = strtok(schedArgs, ",");
                while (token != NULL) { 
                    param[i++] = strtoul(token, NULL, 0);
                    token = strtok(NULL, ","); 
                }
                schedAttr.sched_runtime = param[0];
                schedAttr.sched_deadline = param[1];
                schedAttr.sched_period = param[2];
            }
            else {
                schedAttr.sched_runtime = 9500000;
                schedAttr.sched_deadline = 19500000;
                schedAttr.sched_period = 100000000;
            }
            break;
    }
            if (sched_setattr(0, &schedAttr, 0) != 0)
            {
                printf("Could not set scheduling.\n");
            }
}