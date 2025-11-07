#include <stdio.h>
#include "FIFO.h"

void queue_init(Queue *Q, int size) {
    Q->size = size;
    Q->count = 0;
    Q->back = Q->front = 0;
}

int is_queue_empty(Queue *Q) {
    return Q->count == 0;
}

int is_queue_full(Queue *Q) {
    return Q->count == Q->size;
}

void enqueue(Queue *Q, double data) {
    if (is_queue_full(Q)) ; // Queue Full
    else {
        Q->back = (Q->back + 1) % Q->size;
        Q->data[Q->back] = data;
        Q->count++;
    }
}

void dequeue(Queue *Q) {
    if (is_queue_empty(Q)) ;
    else {
        Q->front = (Q->front + 1) % Q->size;
        Q->count--;
    }
}

double queue_content_sum(Queue *Q) {
    double sum = 0;
    int index = Q->front;
    for (int i = 0; i < Q->count; i++) {
        index = (index + 1) % Q->size;
        sum += Q->data[index];
    }

    return sum;
}