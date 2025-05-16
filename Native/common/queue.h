// queue.h
#ifndef QUEUE_H
#define QUEUE_H

typedef struct			QueueNode
{
	void				*data;
	struct QueueNode	*next;
}						QueueNode;

typedef struct
{
	QueueNode	*front;
	QueueNode	*rear;
}				Queue;

void	queue_init(Queue* q);
void	queue_enqueue(Queue* q, void* data);
void	*queue_dequeue(Queue* q);
int		queue_is_empty(Queue* q);
void	*queue_remove(Queue* q, void* data);
void	queue_destroy(Queue* q);

#endif