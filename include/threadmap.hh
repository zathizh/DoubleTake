#if !defined(DOUBLETAKE_THREADMAP_H)
#define DOUBLETAKE_THREADMAP_H

/*
 * @file   threadmap.h
 * @brief  Mapping between pthread_t and internal thread information.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#include <new>

#include "hashfuncs.hh"
#include "hashmap.hh"
#include "internalheap.hh"
#include "list.hh"
#include "log.hh"
#include "mm.hh"
#include "recordentries.hh"
#include "semaphore.hh"
#include "spinlock.hh"
#include "threadstruct.hh"
#include "xdefines.hh"

class threadmap {

public:
  threadmap() {}

  static threadmap& getInstance() {
    static char buf[sizeof(threadmap)];
    static threadmap* theOneTrueObject = new (buf) threadmap();
    return *theOneTrueObject;
  }

  void initialize() {
    //    PRINF("xmap initializeNNNNNNNNNNNNNN\n");
    _xmap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, xdefines::THREAD_MAP_SIZE);

    listInit(&_alivethreads);
  }

  // Destroy all semaphores
  void finalize() {
    // PRINF("Destroy all semaphores NOOOOOOOOO!\n");
    destroyAllSemaphores();
  }

  thread_t* getThreadInfo(pthread_t thread) {
    thread_t* info = NULL;
    _xmap.find((void*)thread, sizeof(void*), &info);
    // PRERR("getThreadInfo now, thread info %p\n", info);
    return info;
  }

  void deleteThreadMap(pthread_t thread) { _xmap.erase((void*)thread, sizeof(void*)); }

  void insertAliveThread(thread_t* thread, pthread_t tid) {
    // Malloc
    struct aliveThread* ath =
        (struct aliveThread*)InternalHeap::getInstance().malloc(sizeof(struct aliveThread));

    listInit(&ath->list);
    ath->thread = thread;

    PRINF("Insert alive thread %p\n", thread);
    listInsertTail(&ath->list, &_alivethreads);

    _xmap.insert((void*)tid, sizeof(void*), thread);
  }

  void removeAliveThread(thread_t* thread) {
    // First, remove thread from the threadmap.
    deleteThreadMap(thread->self);

    // Second, remove thread from the alive list.
    struct aliveThread* ath;

    // Now the _alivethreads list should not be empty.
    assert(isListEmpty(&_alivethreads) != true);

    // Search the whole list for given tid.
    ath = (struct aliveThread*)nextEntry(&_alivethreads);
    while(true) {
      PRINF("Traverse thread %p\n", ath->thread);
      // If we found the entry, remove this entry from the list.
      if(ath->thread == thread) {
        listRemoveNode(&ath->list);
        break;
      }
      // It is impossible that we haven't find the node until the end of a list.
      if(isListTail(&ath->list, &_alivethreads) == true) {
        PRWRN("WRong, we can't find alive thread %p in the list.", thread);
        abort();
      }

      ath = (struct aliveThread*)nextEntry(&ath->list);
    }

    InternalHeap::getInstance().free((void*)ath);

    // Setting a thread structure to be "Free" status.
    setFreeThread(thread);
  }

  // Set a threadInfo structure to be free.
  void setFreeThread(thread_t* thread) { thread->available = true; }

  // How to return a thread event from specified entry.
  inline struct syncEvent* getThreadEvent(list_t* entry) {
    // FIXME: if we changed the structure of syncEvent.
    int threadEventOffset = sizeof(list_t);

    return (struct syncEvent*)((intptr_t)entry - threadEventOffset);
  }

  /*
   prepareRollback:
    each thread will update its synchronization entry to the first one on its synchronization list.
   */
  void prepareRollback() {
    struct aliveThread* ath;

    // Search the whole list for given tid.
    ath = (struct aliveThread*)nextEntry(&_alivethreads);
    while(true) {
      thread_t* thread = ath->thread;

      // Initialize the semaphore for this thread.
      initThreadSemaphore(thread);

      // Set the entry of each thread to the first synchronization event.
      thread->syncevents.prepareRollback();

      // Update to the next thread.
      if(isListTail(&ath->list, &_alivethreads)) {
        break;
      } else {
        ath = (struct aliveThread*)nextEntry(&ath->list);
      }
    }
  }

  /*
    destroy all semaphores:
   */
  void destroyAllSemaphores() {
    struct aliveThread* ath;
    /*    thread_t* thread; */
    /* struct syncEventList* eventlist; */

    // Search the whole list for given tid.
    ath = (struct aliveThread*)nextEntry(&_alivethreads);
    while(true) {
      thread_t* thread = ath->thread;

      // If we found the entry, remove this entry from the list.
      destroyThreadSemaphore(thread);

      // Update to the next thread.
      if(isListTail(&ath->list, &_alivethreads)) {
        break;
      } else {
        ath = (struct aliveThread*)nextEntry(&ath->list);
      }
    }
  }

  // Initialize the semaphore for  specified thread
  void destroyThreadSemaphore(thread_t* thread) {
    semaphore* sema = &thread->sema;

    // We initialize the semaphore value to 0.
    sema->destroy();
  }

  // Initialize the semaphore for  specified thread
  void initThreadSemaphore(thread_t* thread) {
    semaphore* sema = &thread->sema;

    PRINF("INITSEMA: THREAD%d at %p sema %p\n", thread->index, thread, sema);
    // We initialize the semaphore value to 0.
    sema->init((unsigned long)thread->self, 1, 0);
  }

public:
  class aliveThreadIterator {
    struct aliveThread* thread;

  public:
    aliveThreadIterator(struct aliveThread* ithread = NULL) { thread = ithread; }

    ~aliveThreadIterator() {}

    aliveThreadIterator& operator++(int) // in postfix ++
    {
      if(!isListTail(&thread->list, &_alivethreads)) {
        thread = (struct aliveThread*)nextEntry(&thread->list);
      } else {
        thread = NULL;
      }

      return *this;
    }

    // aliveThreadIterator& operator -- ();
    // Iterpreted as a = b is treated as a.operator=(b)
    aliveThreadIterator& operator=(const aliveThreadIterator& that) {
      thread = that.thread;
      return *this;
    }

    bool operator==(const aliveThreadIterator& that) const { return thread == that.thread; }

    bool operator!=(const aliveThreadIterator& that) const { return thread != that.thread; }

    thread_t* getThread() { return thread->thread; }
  };

  void traverseAllThreads() {
    struct aliveThread* ath;

    // Search the whole list for given tid.
    ath = (struct aliveThread*)nextEntry(&_alivethreads);
    while(true) {
      thread_t* thread = ath->thread;

      if(thread->status != E_THREAD_WAITFOR_REAPING) {
        PRINF("thread %p self %p status %d\n", thread, (void*)thread->self, thread->status);
      }

      // Update to the next thread.
      if(isListTail(&ath->list, &_alivethreads)) {
        break;
      } else {
        ath = (struct aliveThread*)nextEntry(&ath->list);
      }
    }
  }

  // Acquire the first entry of the hash table
  aliveThreadIterator begin() {
    // Get the first non-null entry
    if(isListEmpty(&_alivethreads) != true) {
      return aliveThreadIterator((struct aliveThread*)nextEntry(&_alivethreads));
    } else {
      return end();
    }
  }

  aliveThreadIterator end() { return aliveThreadIterator(NULL); }

private:
  // We are maintainning a private hash map for each thread.
  typedef HashMap<void*, thread_t*, spinlock, InternalHeapAllocator> threadHashMap;

  // The  variables map shared by all threads
  threadHashMap _xmap;

  static list_t _alivethreads;
};

#endif
