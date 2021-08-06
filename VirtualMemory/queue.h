

// Node struct
typedef struct Node {
    int val;
    struct Node* next;
} Node;
  
// Queue struct
typedef struct Queue {
    Node *front;
    Node *rear;
    int size;
} Queue;


// create a new node
Node* newNode(int k);
  
// create an empty queue
Queue* initQueue();

// return the poiner to the first element;
Node* frontQueue(Queue* q);

// return the size of the queue
int sizeQueue(Queue* q);
  
// add a value k to q
void pushQueue(Queue* q, int k);
  
// remove a the first element from queue q
void popQueue(Queue* q);