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
    preempt_disable(); // Preemption'ı devre dışı bırak
    while (sem->count <= 0) {
        preempt_enable();  // Diğer thread'lerin çalışmasına izin ver (post yapabilmeleri için)
        thread_yield();    // CPU'yu bırak
        preempt_disable(); // Koşulu tekrar atomik olarak kontrol etmek için preemption'ı kapat
    }
    sem->count--;      // Kaynak sayısını azalt
    preempt_enable();  // Preemption'ı tekrar etkinleştir
}

void	sem_post(Semaphore* sem)
{
	// TODO: Implement the semaphore signal (V) operation.
	preempt_disable();
    sem->count++;
    preempt_enable();

}

void	sem_destroy(Semaphore* sem)
{
	// TODO: Implement the semaphore destroy operation.
	queue_destroy(&sem->waiting_queue);
}
