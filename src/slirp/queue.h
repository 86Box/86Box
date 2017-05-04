/*  
 *          File: queue.h
 *        Author: Robert I. Pitts <rip@cs.bu.edu>
 * Last Modified: March 9, 2000
 *         Topic: Queue - Array Implementation
 * ----------------------------------------------------------------
 */

#ifndef _QUEUE_H
#define _QUEUE_H

/*
 * Constants
 * ---------
 * ERROR_*   These signal error conditions in queue functions
 *           and are used as exit codes for the program.
 */
#define ERROR_QUEUE   2
#define ERROR_MEMORY  3

/*
 * Type: queueElementT
 * -------------------
 * This is the type of objects held in the queue.
 */

/*typedef char queueElementT;
typedef unsigned char *queueElementT;
*/

struct queuepacket{
        int len;
        unsigned char data[2000];
};
typedef struct queuepacket *queueElementT;

/*
 * Type: queueADT
 * --------------
 * The actual implementation of a queue is completely
 * hidden.  Client will work with queueADT which is a
 * pointer to underlying queueCDT.
 */

/*
 * NOTE: need word struct below so that the compiler
 * knows at least that a queueCDT will be some sort
 * of struct.
 */

typedef struct queueCDT *queueADT;	

/*
 * Function: QueueCreate
 * Usage: queue = QueueCreate();
 * -------------------------
 * A new empty queue is created and returned.
 */

queueADT QueueCreate(void);

/* Function: QueueDestroy
 * Usage: QueueDestroy(queue);
 * -----------------------
 * This function frees all memory associated with
 * the queue.  "queue" may not be used again unless
 * queue = QueueCreate() is called first.
 */

void QueueDestroy(queueADT queue);

/*
 * Functions: QueueEnter, QueueDelete
 * Usage: QueueEnter(queue, element);
 *        element = QueueDelete(queue);
 * --------------------------------------------
 * These are the fundamental queue operations that enter
 * elements in and delete elements from the queue.  A call
 * to QueueDelete() on an empty queue or to QueueEnter()
 * on a full queue is an error.  Make use of QueueIsFull()
 * and QueueIsEmpty() (see below) to avoid these errors.
 */

void QueueEnter(queueADT queue, queueElementT element);
queueElementT QueueDelete(queueADT queue);


/*
 * Functions: QueueIsEmpty, QueueIsFull
 * Usage: if (QueueIsEmpty(queue)) ...
 * -----------------------------------
 * These return a true/false value based on whether
 * the queue is empty or full, respectively.
 */

int QueueIsEmpty(queueADT queue);
int QueueIsFull(queueADT queue);

int QueuePeek(queueADT queue);

#endif  /* not defined _QUEUE_H */
