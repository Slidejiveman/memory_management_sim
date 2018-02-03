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
node *split_node();
void merge_nodes();

/* global variables */
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
doubly_linked_queue *AVAILABLE_MEMORY = NULL;
doubly_linked_queue *ALLOCATED_MEMORY = NULL;
int i, rc;                                       // i is the number of nodes. rc is return code for errors.

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

// Creates a doubly-linked queue with initialized nodes for available memory
// allocated memory starts out empty, but this method creates both queues
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

// Initialize a new node during program startup for available memory
void init_node(node *new_node, int i)
{
    new_node->ptid = i;
    new_node->nBase = i * BLOCK_SIZE - 1; // 0 -> BLOCKSIZE - 1 for each initial node
    if (new_node->nBase == -1) new_node->nBase = 0;
    new_node->nStay = 0;                  // 0 since the memory hasn't been allocated at all yet.
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
void requeue(doubly_linked_queue *target, doubly_linked_queue *source, node *a_node)
{
    if (source->head == a_node) 
        source->head = a_node->next;
    else if (source->tail == a_node)
        source->tail = a_node->prev; 
    else // standard case
    {
        node *previous = a_node->prev;
        node *next = a_node->next;
        previous->next = next;
        next->prev = previous;
    }
    enqueue(target, a_node);
}

// removes a node from a memory queue and returns it
node *dequeue(doubly_linked_queue *source, node *a_node)
{
    if (source->head == a_node)
        source->head = a_node->next;
    else if (source->tail == a_node)
        source->tail = a_node->prev;
    else // standard case
    {
        node *previous = a_node->prev;
        node *next = a_node->next;
        previous->next = next;
        next->prev = previous;
    }
    return a_node;
}

// splits a node to allocate a smaller amount of memory
// thus minimizing waste.
node *split_node(node *a_node, int blocks)
{
    a_node->nBlocks   = a_node->nBlocks - blocks;
    node *new_node    = (node*) malloc(sizeof(node));
    new_node->ptid    = ++i;                           // i is global, never reset to 0
    new_node->nBase   = a_node->nBlocks - blocks;      // base starts at the last index of original node's
    new_node->nStay   = 0;                             // 0 because only just now allocated
    new_node->nBlocks = blocks;                        // blocks to allocate was passed into this function
    return new_node;
}

// merge adjacent nodes that have been returned to available memory:
// if a node is found that doesn't have a maximum number of blocks,
// find some nodes that can return their blocks to it without exceeding
// the maximum number of blocks
void merge_nodes()
{
    doubly_linked_queue *temp = (doubly_linked_queue*) malloc(sizeof(doubly_linked_queue));
    node *temp_node;

    // find all eligible nodes in available memory for deallocation
    // the ones that are found will all be in a single block of memory
    // All should be stored in the high range first.
    // Ignore the head. If it is still in the AVAILABLE_MEMORY pool, then it
    // is still large enough to be subdivided into other nodes. Just give blocks
    // back to it.
    pthread_mutex_lock(&mutex);
    AVAILABLE_MEMORY->current = AVAILABLE_MEMORY->head->next;
    pthread_mutex_unlock(&mutex);
    while (AVAILABLE_MEMORY->current != AVAILABLE_MEMORY->tail)
    {
        if (AVAILABLE_MEMORY->current->nBlocks < BLOCK_SIZE)
        {
            temp_node = AVAILABLE_MEMORY->current;
            pthread_mutex_lock(&mutex);
            AVAILABLE_MEMORY->current = AVAILABLE_MEMORY->current->next;
            AVAILABLE_MEMORY->head->nBlocks += temp_node->nBlocks;
            requeue(temp, AVAILABLE_MEMORY, temp_node);
            pthread_mutex_unlock(&mutex);
        }
        if (AVAILABLE_MEMORY->tail->nBlocks < BLOCK_SIZE) // handle tail separately
        {
            temp_node = AVAILABLE_MEMORY->tail;
            pthread_mutex_lock(&mutex);
            AVAILABLE_MEMORY->head->nBlocks += temp_node->nBlocks;
            requeue(temp, AVAILABLE_MEMORY, temp_node);
            pthread_mutex_unlock(&mutex);
        }
    }
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
#ifdef DEBUG
    printf("******** IN ALLOCATION ROUTINE ******** \n\n");
#endif
    while(true) 
    {
        if (!is_empty(AVAILABLE_MEMORY))
        {
#ifdef DEBUG
            printf("\n==== Allocating Memory ====\n");
#endif
            int blocks_to_allocate = rand() % 41 + 10; // 10 - 50 blocks
            pthread_mutex_lock(&mutex);
            AVAILABLE_MEMORY->current = AVAILABLE_MEMORY->head;
            pthread_mutex_unlock(&mutex);
            while (AVAILABLE_MEMORY->current != AVAILABLE_MEMORY->tail) // looking via "First Fit" method
            {
                if (AVAILABLE_MEMORY->current->nBlocks > blocks_to_allocate)
                {
                    // we can allocate blocks from this node, but is the node too large?
                    if (AVAILABLE_MEMORY->current->nBlocks > blocks_to_allocate * 2)
                    {
                        // split the node by decrementing its nBlocks and creating a new node
                        // with the difference. Add the new node to the allocated queue
                        pthread_mutex_lock(&mutex);
                        node* new_node = split_node(AVAILABLE_MEMORY->current, blocks_to_allocate); 
                        enqueue(ALLOCATED_MEMORY, new_node);
                        pthread_mutex_unlock(&mutex);
                    } 
                    else
                    {    // allocate the current node by moving it to the allocated queue
                         pthread_mutex_lock(&mutex);
                         requeue(ALLOCATED_MEMORY, AVAILABLE_MEMORY,AVAILABLE_MEMORY->current);
                         pthread_mutex_unlock(&mutex);
                    }
                    // exit the loop after a fit is found.
                    pthread_mutex_lock(&mutex);
                    AVAILABLE_MEMORY->current = AVAILABLE_MEMORY->tail;    
                    pthread_mutex_unlock(&mutex);
                }
                else // we cannot allocate blocks from this node 
                {
                    pthread_mutex_lock(&mutex);
                    AVAILABLE_MEMORY->current = AVAILABLE_MEMORY->current->next;
                    pthread_mutex_unlock(&mutex);
                }
            }       
            sleep(1);
        }
    }
    return EXIT_SUCCESS;
}

// Garbage collects an allocated node and move is back to the available queue
// Chooses whichever node has the highest stay value. This should be the head.
// Bonus: try to combine adjacent freed nodes back together and if one has
// nBlocks of than some value, free the memory
void *collect()
{
#ifdef DEBUG
    printf("******** IN COLLECTION ROUTINE ******** \n\n");
#endif
    while(true)
    {
        if (!is_empty(ALLOCATED_MEMORY))
        {
#ifdef DEBUG
            printf("\n==== Garbage Collecting ====\n");
#endif
            pthread_mutex_lock(&mutex);
            ALLOCATED_MEMORY->head->nStay = 0;                                   // deallocate and clear timer
            requeue(AVAILABLE_MEMORY, ALLOCATED_MEMORY, ALLOCATED_MEMORY->head); // the head should have highest stay
            pthread_mutex_unlock(&mutex);
            merge_nodes();                                                       // merge free nodes
        }
        sleep(2);
    }
    return EXIT_SUCCESS;
}

// traverses through the queues starting at the head. Print out info.
void *traverse()
{
#ifdef DEBUG
    printf("******** IN TRAVERSE ROUTINE ********\n\n");
#endif
    while(true) 
    {
        if (!is_empty(AVAILABLE_MEMORY))
        {
#ifdef DEBUG
            printf("\n==== Traversing Available Memory ====\n");
#endif
            pthread_mutex_lock(&mutex);
            AVAILABLE_MEMORY->current = AVAILABLE_MEMORY->head;
            while(AVAILABLE_MEMORY->current != AVAILABLE_MEMORY->tail)
            {
                printf("Current node: %d nBase: %d nStay %d nBlocks %d\n", AVAILABLE_MEMORY->current->ptid, AVAILABLE_MEMORY->current->nBase, AVAILABLE_MEMORY->current->nStay, AVAILABLE_MEMORY->current->nBlocks);
                AVAILABLE_MEMORY->current = AVAILABLE_MEMORY->current->next;
            }
            printf("Current node: %d nBase: %d nStay %d nBlocks %d\n", AVAILABLE_MEMORY->tail->ptid, AVAILABLE_MEMORY->tail->nBase, AVAILABLE_MEMORY->tail->nStay, AVAILABLE_MEMORY->tail->nBlocks); // must print the tail too 
            pthread_mutex_unlock(&mutex);
        }

        // Releasing lock before checking allocated memory because no memory may be allocated.
        // This also gives other threads the chance to go when checking to see if there is 
        // allocated memory.
        if (ALLOCATED_MEMORY != NULL && !is_empty(ALLOCATED_MEMORY))
        {
#ifdef DEBUG
            printf("\n==== Traversing Allocated Memory ====\n");
#endif
            pthread_mutex_lock(&mutex);
            ALLOCATED_MEMORY->current = ALLOCATED_MEMORY->head;
            while(ALLOCATED_MEMORY->current != ALLOCATED_MEMORY->tail)
            {
                printf("Current node: %d nBase: %d nStay: %d nBlocks: %d\n", ALLOCATED_MEMORY->current->ptid, ALLOCATED_MEMORY->current->nBase, ALLOCATED_MEMORY->current->nStay, ALLOCATED_MEMORY->current->nBlocks);
                ALLOCATED_MEMORY->current = ALLOCATED_MEMORY->current->next;
            }
            printf("Current node: %d nBase: %d nStay: %d nBlocks: %d\n", ALLOCATED_MEMORY->tail->ptid, ALLOCATED_MEMORY->tail->nBase, ALLOCATED_MEMORY->tail->nStay, ALLOCATED_MEMORY->tail->nBlocks); // must print the tail too 
            pthread_mutex_unlock(&mutex);
        } 
        sleep(5);
    }
    return EXIT_SUCCESS;
}

// increments the nStay value of each node in the allocated queue
void *increment_times()
{
#ifdef DEBUG
    printf("******** IN INCREMENT TIMES ROUTINE ********\n\n");
#endif
    while(true)
    {
        // if the allocated_memory isn't empty, increment all of the
        // nodes' stay value
        if (!is_empty(ALLOCATED_MEMORY))
        {
#ifdef DEBUG
            printf("\n==== Incrementing Allocated Memory Stay Values  ====\n");
#endif
            pthread_mutex_lock(&mutex);
            ALLOCATED_MEMORY->current = ALLOCATED_MEMORY->head;
            while (ALLOCATED_MEMORY->current != ALLOCATED_MEMORY->tail)
            {
                ALLOCATED_MEMORY->current->nStay++;
                ALLOCATED_MEMORY->current = ALLOCATED_MEMORY->current->next;
            }
            ALLOCATED_MEMORY->tail->nStay++; // handle the tail
            pthread_mutex_unlock(&mutex);
        }
        sleep(1);
    }
    return EXIT_SUCCESS;
}

