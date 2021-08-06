#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "queue.h"
  

  
// create a new node
Node* newNode(int k) {
    Node* curr = (Node*) malloc(sizeof(Node));
    if (!curr) {
        fprintf(stderr,"couldn't create Node: %s\n", strerror(errno));
        exit(1);
    }
    curr->val = k;
    curr->next = NULL;
    return curr;
}
  
// create an empty queue
Queue* initQueue() {
    Queue* q = (Queue*) malloc(sizeof(Queue));
    q->front = q->rear = NULL;
    q->size = 0;
    return q;
}

// return the poiner to the first element;
Node* frontQueue(Queue* q) {
    return q->front;
}

// return the size of the queue
int sizeQueue(Queue* q) {
    return q->size;
}
  
// add a value k to q
void pushQueue(Queue* q, int k) {
    
    Node* curr = newNode(k);
    q->size += 1;

    // queue is empty
    if (q->rear == NULL) {
        q->front = q->rear = curr;
        return;
    }
  
    // add the new node at the end of queue
    q->rear->next = curr;
    q->rear = curr;

    return;
}
  
// remove a the first element from queue q
void popQueue(Queue* q) {
    // If queue is empty, return.
    if (q->front == NULL)
        return;
  
    // Store previous front and move front one node ahead
    Node* temp = q->front;
  
    q->front = q->front->next;
  
    // change rear o NULL if it was pointing to the same thing as front
    if (q->front == NULL)
        q->rear = NULL;

    q->size -= 1;
  
    free(temp);
    return;
}