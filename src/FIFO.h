#ifndef FIFO_H
#define FIFO_H

#define FIFO_MAX       100

typedef struct {
    int size, count;
    int32_t data[FIFO_MAX];
    int front, back;
}Queue;

extern void queue_init(Queue *Q, int size);
extern int is_queue_empty(Queue *Q);
extern int is_queue_full(Queue *Q);
extern void enqueue(Queue *Q, int32_t data);
extern void dequeue(Queue *Q);
extern double queue_content_sum(Queue *Q);

#endif