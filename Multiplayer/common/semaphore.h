#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "thread.h"
#include "queue.h"

typedef struct
{
	int		count;
	Queue	waiting_queue;
}			Semaphore;

void	sem_init(Semaphore* sem, int value);
void	sem_wait(Semaphore* sem);
void	sem_post(Semaphore* sem);
void	sem_destroy(Semaphore* sem);

// Mutex as binary semaphore
typedef Semaphore Mutex;
#define mutex_init(sem)		sem_init(sem, 1)
#define mutex_lock(sem)		sem_wait(sem)
#define mutex_unlock(sem)	sem_post(sem)
#define mutex_destroy(sem)	sem_destroy(sem)

#endif