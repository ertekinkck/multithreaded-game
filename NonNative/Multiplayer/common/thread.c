#include "thread.h"
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>

// Signal handling definitions
#ifndef SA_RESTARTW
#define SA_RESTART 0x10000000
#endif

#define THREAD_STACK_SIZE (8 * 1024 * 1024)

// Priority definitions
#define PRIORITY_HIGH 0
#define PRIORITY_LOW  1

// Structure to pass priority information along with actual argument
typedef struct {
	int desired_priority;
	void* actual_arg;
} InternalThreadCreationArgs;

static Thread			*thread_list = NULL;
static Thread			*current_thread = NULL;
static Thread			*main_thread = NULL;
static Thread           *g_insertion_point_high_prio = NULL; // Track last high priority thread
static volatile bool	preempt = true;
static struct itimerval	timer;

static void	timer_handler(int sig);
static void	thread_init_helper(ThreadFunc func, void* arg);
static void	schedule(void);

Thread	**thread_get_list(void)
{
	return (&thread_list);
}

Thread	**thread_get_current(void)
{
	return (&current_thread);
}

void	thread_set_current(Thread *thread)
{
	current_thread = thread;
}

static void	timer_handler(int sig)
{
	if (preempt)
	{
		thread_yield();
	}
}

static void	thread_init_helper(ThreadFunc func, void* arg)
{
	// TODO: Call the thread function with the provided argument and store the return value.
	// TODO: Mark the current thread as finished after the function completes.
	// TODO: Exit the thread and pass the return value to the thread_exit function.
	void *retval = func(arg);
	current_thread->finished = true;
	thread_exit(retval);
}

static void	schedule(void)
{
	if (!current_thread || !current_thread->next)
		return ;

	Thread	*prev = current_thread;
	current_thread = current_thread->next;

	swapcontext(&prev->context, &current_thread->context);
}

void	thread_schedule(void)
{
	schedule();
}

int	thread_create(Thread **thread, ThreadFunc func, void* arg)
{
	// TODO: Initialize threading system if needed.
	// TODO: Set up thread context and link to main thread.
	// TODO: Mark thread as active and configure context for execution.
		// Use thread_init_helper
	// TODO: Add thread to circular list.
	// TODO: Update list pointers and return the new thread.

	Thread *new_thread = (Thread *)malloc(sizeof(Thread));
	if (!new_thread)
		return (-1);
	
	new_thread->stack = malloc(THREAD_STACK_SIZE);
	if (!new_thread->stack) {
		free(new_thread);
		return (-1);
	}

	// Cast and extract priority information from the argument
	InternalThreadCreationArgs *creation_args = (InternalThreadCreationArgs*)arg;
	int priority_hint = creation_args->desired_priority;
	void* actual_function_arg = creation_args->actual_arg;

	getcontext(&new_thread->context);
	new_thread->context.uc_stack.ss_sp = new_thread->stack;
	new_thread->context.uc_stack.ss_size = THREAD_STACK_SIZE;
	new_thread->context.uc_link = NULL;
	new_thread->finished = false;

	makecontext(&new_thread->context, (void (*)())thread_init_helper, 2, func, actual_function_arg);

	preempt_disable();
	if (!thread_list) {
		thread_list = new_thread;
		new_thread->next = new_thread;
		g_insertion_point_high_prio = new_thread; // First thread is considered high priority
	} else {
		if (priority_hint == PRIORITY_HIGH) {
			// High priority: insert right after main_thread
			if (g_insertion_point_high_prio == main_thread) {
				// First high priority thread after main_thread
				new_thread->next = main_thread->next;
				main_thread->next = new_thread;
			} else {
				// Insert after the last high priority thread
				new_thread->next = g_insertion_point_high_prio->next;
				g_insertion_point_high_prio->next = new_thread;
			}
			g_insertion_point_high_prio = new_thread; // Update last high priority thread
		} else { // PRIORITY_LOW
			// Low priority: add to the end of the list (just before main_thread)
			Thread *tail = thread_list;
			while (tail->next != thread_list) {
				tail = tail->next;
			}
			new_thread->next = thread_list;
			tail->next = new_thread;
		}
	}
	preempt_enable();

	*thread = new_thread;
	return (0);
}

void thread_yield(void)
{
	// Check if preemption is enabled.
	// Call the scheduler to switch to the next thread.
	thread_schedule();
}

void	thread_exit(void *retval)
{
	(void)retval;
	current_thread->finished = true;
	schedule();
}

Thread	*thread_self(void)
{
	return (current_thread);
}

void	thread_join(Thread *thread)
{
	// TODO: Wait for the given thread to finish execution.
	// TODO: Traverse the thread list to find the thread to be removed.
	// TODO: Remove the thread from the list and update the list pointers accordingly.
	if (!thread)
		return;

	while (!thread->finished) {
		thread_yield();
	}

	preempt_disable();
	if (thread_list == thread && thread->next == thread) {
		// Last thread in the list
		thread_list = NULL;
		g_insertion_point_high_prio = NULL;
	} else {
		Thread *prev = thread_list;
		// Find the thread preceding the one to be removed
		while (prev->next != thread && prev->next != thread_list) {
			prev = prev->next;
		}
		
		if (prev->next == thread) {
			prev->next = thread->next;
			if (thread_list == thread) {
				thread_list = thread->next;
			}
			
			// Update g_insertion_point_high_prio if needed
			if (g_insertion_point_high_prio == thread) {
				if (prev == main_thread && thread == main_thread->next) {
					g_insertion_point_high_prio = main_thread;
				} else if (prev != main_thread || thread->next == main_thread) {
					g_insertion_point_high_prio = prev;
				} else {
					g_insertion_point_high_prio = main_thread; // Safe fallback
				}
			}
		}
	}
	preempt_enable();

	if (thread->stack) free(thread->stack);
	free(thread);
}

void thread_init(void)
{
	// TODO: Set up the main thread's context and mark it as not finished.
	main_thread = (Thread *)malloc(sizeof(Thread));
	if (!main_thread) {
		perror("Failed to allocate memory for main thread");
		exit(EXIT_FAILURE);
	}

	// TODO: Link the main thread to itself and set it as the current thread.
	main_thread->stack = NULL; // Main thread uses OS stack
	main_thread->finished = false;
	main_thread->next = main_thread; // Single element circular list
	
	current_thread = main_thread;
	thread_list = main_thread;
	g_insertion_point_high_prio = main_thread; // Initially, main_thread is the only high priority thread

	if (getcontext(&main_thread->context) == -1) {
		perror("getcontext for main_thread failed");
		free(main_thread);
		exit(EXIT_FAILURE);
	}

	// TODO: Configure the signal handler for the timer to handle preemption.
	if (signal(SIGALRM, timer_handler) == SIG_ERR) {
		perror("signal failed");
		free(main_thread);
		exit(EXIT_FAILURE);
	}

	// TODO: Set up the timer to trigger periodically for thread scheduling.
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 10000;  // 10ms
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 10000;
	if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
		perror("setitimer failed");
		free(main_thread);
		exit(EXIT_FAILURE);
	}
}

void preempt_enable(void)
{
	preempt = true;
}

void preempt_disable(void)
{
	preempt = false;
}
