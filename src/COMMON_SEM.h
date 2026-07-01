#ifndef COMMON_SEM_H
#define COMMON_SEM_H

#include <zephyr/kernel.h>

extern struct k_sem rec_semaphore;
extern struct k_sem fs_sem;

#endif