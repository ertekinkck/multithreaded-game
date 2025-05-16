#ifndef THREAD_H
#define THREAD_H

#include <ucontext.h>
#include <stdbool.h>

typedef void *(*ThreadFunc)(void *);

typedef struct		Thread
{
	ucontext_t		context;
	void			*stack;
	bool			finished;
	struct Thread	*next;
}					Thread;

int		thread_create(Thread **thread, ThreadFunc func, void *arg);
void	thread_join(Thread* thread);
void	thread_exit(void* retval);
Thread	*thread_self(void);
void	thread_yield(void);
void	thread_init(void);

Thread	**thread_get_list(void);
Thread	**thread_get_current(void);
void	thread_set_current(Thread* thread);
void	thread_schedule(void);

void	preempt_enable(void);
void	preempt_disable(void);

#endif