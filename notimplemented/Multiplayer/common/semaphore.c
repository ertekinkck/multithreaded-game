#include "semaphore.h"
#include "thread.h"

void	sem_init(Semaphore* sem, int value)
{
	// TODO: Initialize the semaphore with the given value.
	sem->count = value;
	queue_init(&sem->waiting_queue);
}

void	sem_wait(Semaphore* sem)
{
	// TODO: Implement the semaphore wait (P) operation. 
	// Check if count is already at or below zero, block if needed
	while (sem->count <= 0) {
		thread_yield();
	}
	// Only decrement once
	sem->count--;
}

void	sem_post(Semaphore* sem)
{
	// TODO: Implement the semaphore signal (V) operation.
	sem->count++;
	thread_schedule();
}

void	sem_destroy(Semaphore* sem)
{
	// TODO: Implement the semaphore destroy operation.
	queue_destroy(&sem->waiting_queue);
}
