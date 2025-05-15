#include "queue.h"
#include <stdlib.h>
#include <stdbool.h>

void	queue_init(Queue *q)
{
	// TODO: Initialize the queue
	q->front = NULL;
	q->rear  = NULL;
}

void	queue_enqueue(Queue *q, void *data)
{
	// TODO: Create a new node with the given data
	// TODO: Add the new node to the end of the queue
	QueueNode *new_node = (QueueNode *)malloc(sizeof(QueueNode));
	new_node->data = data;
	new_node->next = NULL;
	if (q->rear == NULL)
	{
		q->front = new_node;
		q->rear = new_node;
	}
	else
	{
		q->rear->next = new_node;
		q->rear = new_node;
	}
}

void	*queue_dequeue(Queue *q)
{
	// TODO: Remove the front node from the queue,
	// TODO: Return the data stored in the removed node.
	QueueNode *temp = q->front;
	void *data = temp->data;
	q->front = q->front->next;
	free(temp);
	return (data);

}

int	queue_is_empty(Queue *q)
{
	// TODO: Check if the queue is empty

	return q->front == NULL;
}

void* queue_remove(Queue* q, void* data)
{
	// TODO: Implement a function to remove a specific node from the queue.
	// TODO: Traverse the queue to find the node with the matching data.
	// TODO: Update the pointers to remove the node and free its memory.
	// TODO: Return the data of the removed node, or NULL if not found.
	QueueNode *current = q->front;
	QueueNode *prev = NULL;
	while (current != NULL)
	{
		if (current->data == data)
		{
			if (prev == NULL)
				q->front = current->next;
			else
				prev->next = current->next;
			free(current);
			return (data);
		}
		prev = current;
		current = current->next;
	}
	return (NULL);
}

void	queue_destroy(Queue *q)
{
	// TODO: Continuously dequeue all elements from the queue until it is empty.
	while (!queue_is_empty(q))
	{
		queue_dequeue(q);
	}
}
