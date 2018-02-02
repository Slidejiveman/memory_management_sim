#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>

#define NUM_NODES  3       // Initial number of nodes, chunks of memory
#define BLOCK_SIZE 1024    // Maximum block size
#define DEBUG

/* type definitions */
// these nodes will be strung together in a doubly-linked queue
typedef struct _node 
{
    int ptid;              // node ID
    int nBase;             // the base register memory offset of the node
    int nStay;             // the length of time the node has been allocated
    int nBlocks;           // the limit register of the memory process
    struct _node *next;    // pointer to next node in list
    struct _node *prev;    // pointer to previous node in list
} node;

// used to form the memory allocated and available queues
typedef struct _doubly_linked_queue
{
    node *head, *tail, *current;
    int length;
} doubly_linked_queue;

/* function prototypes */
void init_queues();
void init_node();
void enqueue();
bool is_empty();
void requeue();
node *dequeue();
void *allocate();
void *collect();
void *traverse();
void *increment_times();

/* global variables */
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
doubly_linked_queue *AVAILABLE_MEMORY = NULL;
doubly_linked_queue *ALLOCATED_MEMORY = NULL;
int i, rc;

/* methods */

// initializes data and creates threads
int main(int argc, char *argv[]) 
{
    srand(time(NULL));
    
    // initialize doubly-linked queues
    init_queues();
    
    // create the threads
    pthread_t allocator, collector, traverser, timer;
    if ((rc = pthread_create(&allocator, NULL, allocate, NULL)))
    {
        fprintf(stderr, "error: scheduler thread creation, rc: %d\n", rc);
        return EXIT_FAILURE;
    }
    if ((rc = pthread_create(&collector, NULL, collect, NULL)))
    {
        fprintf(stderr, "error: interrupt thread creation, rc: %d\n", rc);
        return EXIT_FAILURE;
    }
    if ((rc = pthread_create(&traverser, NULL, traverse, NULL)))
    {
        fprintf(stderr, "error: traverser thread creation, rc: %d\n", rc);
        return EXIT_FAILURE;
    }
    if ((rc = pthread_create(&timer, NULL, increment_times, NULL)))
    {
        fprintf(stderr, "error: timer thread creation, rc: %d\n", rc);
    }

    // block for thread completion before exiting
    pthread_join(allocator, NULL);
    pthread_join(collector, NULL);
    pthread_join(traverser, NULL);
    pthread_join(timer, NULL);

    return EXIT_SUCCESS;
}

// Creates a doubly-linked queue with initialized nodes
void init_queues()
{
    AVAILABLE_MEMORY = (doubly_linked_queue*) malloc(sizeof(doubly_linked_queue));
    if (AVAILABLE_MEMORY == NULL) return;
    ALLOCATED_MEMORY = (doubly_linked_queue*) malloc(sizeof(doubly_linked_queue));
    if (ALLOCATED_MEMORY == NULL) return;
    for (i = 0; i < NUM_NODES; ++i)
    {
        node *new_node = (node*) malloc(sizeof(node));
        if (new_node == NULL) return; // check to see if heap is full (malloc failure)
        init_node(new_node, i);    
        enqueue(AVAILABLE_MEMORY, new_node);
#ifdef DEBUG       
        printf("New node ptid: %d nBase: %d nStay: %d nBlocks: %d\n", new_node->ptid, new_node->nBase, new_node->nStay, new_node->nBlocks);
#endif        
    }
}

// Initialize a new node
void init_node(node *new_node, int i)
{
    new_node->ptid = i;
    new_node->nBase = i * BLOCK_SIZE; // not exactly right
    new_node->nStay = 0;
    new_node->nBlocks = BLOCK_SIZE;   
}

// add new node to the end of the passed in queue
void enqueue(doubly_linked_queue *memory, node *new_node)
{
    if (!is_empty(memory)) 
    { 
        memory->tail->next = new_node;
        new_node->prev = memory->tail;
    }
    else 
        memory->head = new_node;
    memory->tail = new_node; 

#ifdef DEBUG
    printf("queue tail ptid: %d queue head ptid: %d\n", memory->tail->ptid, memory->head->ptid);
#endif
  
}

// determines if the doubly-linked queue is empty
bool is_empty(doubly_linked_queue *memory) 
{
    return memory == NULL || memory->head == NULL;
}

// removes the memory node from its location in one queue
// and replaces it at the end of the other queue
void requeue(doubly_linked_queue *memory, node *a_node)
{
    node *previous = a_node->prev;
    node *next = a_node->next;
    previous->next = next;
    next->prev = previous;
    enqueue(memory, a_node);
}

// removes a node from a memory queue and returns it
node *dequeue(doubly_linked_queue *memory, node *a_node)
{
    node *previous = a_node->prev;
    node *next = a_node->next;
    previous->next = next;
    next->prev = previous;
    return a_node;
}

/* thread methods */
// select a node from the available queue to allocate
// based a randomly generated number. The number should
// be compared to the nBlocks value of the node. If the
// nBlocks value is not too large, use that node. If it
// is too large, split the node: Decrement the nBlocks
// value of the existing node by the amount that will be
// allocated. Create a new node with the random number
// as the nBlocks size. Enqueue this node in the allocated
// memory queue.
void *allocate()
{
    while(true) 
    {
#ifdef DEBUG
        printf("\n==== Allocating Memory ====\n");
#endif
        pthread_mutex_lock(&mutex);
        pthread_mutex_unlock(&mutex);       
        sleep(1);
    }
    return EXIT_SUCCESS;
}

// Garbage collects an allocated node and move is back to the available queue
void *collect()
{
    while(true)
    {
#ifdef DEBUG
        printf("\n==== Garbage Collecting ====\n");
#endif
        pthread_mutex_lock(&mutex);
        pthread_mutex_unlock(&mutex);
        sleep(2);
    }
    return EXIT_SUCCESS;
}

// traverses through the queues starting at the head. Print out info.
void *traverse()
{
    while(true) 
    {
        if (AVAILABLE_MEMORY != NULL && !is_empty(AVAILABLE_MEMORY))
        {
#ifdef DEBUG
            printf("\n==== Traversing Available Memory ====\n");
#endif
            pthread_mutex_lock(&mutex);
            AVAILABLE_MEMORY->current = AVAILABLE_MEMORY->head;
            while(AVAILABLE_MEMORY->current != AVAILABLE_MEMORY->tail)
            {
                printf("Current node: %d\n", AVAILABLE_MEMORY->current->ptid);
                AVAILABLE_MEMORY->current = AVAILABLE_MEMORY->current->next;
            }
            printf("Current node: %d\n", AVAILABLE_MEMORY->tail->ptid); // must print the tail too 
            pthread_mutex_unlock(&mutex);
        }
        else
            printf("AVAILABLE_MEMORY IS EMPTY\n");

        if (ALLOCATED_MEMORY != NULL && !is_empty(ALLOCATED_MEMORY))
        {
#ifdef DEBUG
            printf("\n==== Traversing Allocated Memory ====\n");
#endif
            pthread_mutex_lock(&mutex);
            ALLOCATED_MEMORY->current = ALLOCATED_MEMORY->head;
            while(ALLOCATED_MEMORY->current != ALLOCATED_MEMORY->tail)
            {
                printf("Current node: %d\n", ALLOCATED_MEMORY->current->ptid);
                ALLOCATED_MEMORY->current = ALLOCATED_MEMORY->current->next;
            }
            printf("Current node: %d\n", ALLOCATED_MEMORY->tail->ptid); // must print the tail too 
            pthread_mutex_unlock(&mutex);
        } 
        else
            printf("ALLOCATED MEMORY IS EMPTY.\n");
        sleep(5);
    }
    return EXIT_SUCCESS;
}

// increments the nStay value of each node in the allocated queue
void *increment_times()
{
    while(true)
    {
        // if the allocated_memory isn't empty, increment all of the
        // nodes' stay value
    }
    return EXIT_SUCCESS;
}

