// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//TODO
//comparation function
static int SJFSort(Thread *x, Thread *y)
{
    if(x->approximate_burst_time < y->approximate_burst_time)
        return -1;
    else if(x->approximate_burst_time > y->approximate_burst_time)
        return 1;
    else 
        return 0;
}
static int PrioritySort(Thread *x, Thread *y)
{
    if(x->priority > y->priority)
        return -1;
    else if(x->priority < y->priority)
        return 1;
    else 
        return 0;
}

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

Scheduler::Scheduler()
{ 
    //readyList = new List<Thread *>; 

    //TODO
    L1 = new SortedList<Thread *>(SJFSort);
    L2 = new SortedList<Thread *>(PrioritySort);
    L3 = new List<Thread *>; 

    preemtive = 0;
    isaging = 0;
    toBeDestroyed = NULL;
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete L1;
    delete L2;
    delete L3;
} 

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    //DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
	//cout << "Putting thread on ready list: " << thread->getName() << endl ;
    thread->setStatus(READY);
    thread->enter_ready_time = kernel->stats->totalTicks;
    if (thread->priority <50){
        if (!kernel->scheduler->isaging)
            DEBUG(dbgMp3, "[A] Tick [" << kernel->stats->totalTicks<< "]: Thread [" << thread->getID() <<"] is inserted into queue L3");
        L3->Append(thread);
    }else if (thread->priority <100){
        if (!kernel->scheduler->isaging)
            DEBUG(dbgMp3, "[A] Tick [" << kernel->stats->totalTicks<< "]: Thread [" << thread->getID() <<"] is inserted into queue L2");
        L2->Insert(thread);
    }else {
        if (!kernel->scheduler->isaging)
            DEBUG(dbgMp3, "[A] Tick [" << kernel->stats->totalTicks<< "]: Thread [" << thread->getID() <<"] is inserted into queue L1");
        L1->Insert(thread);

        if ((thread->approximate_burst_time < kernel->currentThread->approximate_burst_time) || (kernel->currentThread->priority<100)){
            kernel->scheduler->preemtive = 1;
            DEBUG(dbgSelf, "set preemtive to 1");
        }
    }
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    Thread *t;
    if (!L1->IsEmpty()) {
        t = L1->RemoveFront();
        DEBUG(dbgMp3, "[B] Tick ["<<kernel->stats->totalTicks<<"]: Thread [" << t->getID() << "] is removed from queue L1");
		kernel->scheduler->preemtive = 0;
        DEBUG(dbgSelf, "set preemtive to 0");
        return t;
    }else if(!L2->IsEmpty()){
        t = L2->RemoveFront();
        DEBUG(dbgMp3, "[B] Tick ["<<kernel->stats->totalTicks<<"]: Thread [" << t->getID() << "] is removed from queue L2");
        return t;
    }else if(!L3->IsEmpty()){
        t = L3->RemoveFront();
        DEBUG(dbgMp3, "[B] Tick ["<<kernel->stats->totalTicks<<"]: Thread [" << t->getID() << "] is removed from queue L3");
        return t;
    }else {
    	return NULL;
    }
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;
    
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
         ASSERT(toBeDestroyed == NULL);
	 toBeDestroyed = oldThread;
    }
    
    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
	oldThread->space->SaveState();
    }
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running
    
    //TODO
    nextThread->start_cpu_time = kernel->stats->totalTicks;
    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());
    
    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    SWITCH(oldThread, nextThread);

    // we're back, running oldThread
      
    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();		// check if thread we were running
					// before this one has finished
					// and needs to be cleaned up
    
    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
	oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
	toBeDestroyed = NULL;
    }
}
 
//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    //TODO
    //readyList->Apply(ThreadPrint);
}

//TODO
void
Scheduler::aging()
{
    kernel->scheduler->isaging = 1;
    IntStatus oldLevel = kernel->interrupt->SetLevel(IntOff);
    List<Thread *> all;
    Thread *t;
    int now = kernel->stats->totalTicks;
    int old_ready_time;
    while (!L1->IsEmpty()){
        all.Append(L1->RemoveFront());
    }
    while (!L2->IsEmpty()){
        all.Append(L2->RemoveFront());
    }
    while (!L3->IsEmpty()){
        all.Append(L3->RemoveFront());
    }
    while (!all.IsEmpty()){
        t = all.RemoveFront();
        old_ready_time = t->enter_ready_time;
        t->total_ready_time += now - t->enter_ready_time;
        DEBUG(dbgSelf, "is in aging, now time : " << now << " thread : "<<t->getID()<<"'s total_ready_time: "<<t->total_ready_time);
        if (t->total_ready_time > 1500){
            if(t->priority<=139){
                if(t->priority>=40 && t->priority<=49){
                    DEBUG(dbgMp3, "[B] Tick ["<<kernel->stats->totalTicks<<": Thread [" << t->getID() << "] is removed from queue L3");
                    DEBUG(dbgMp3, "[A] Tick [" << kernel->stats->totalTicks<< "]: Thread [" << t->getID()<<"] is inserted into queue L2");
                }else if(t->priority>=90 && t->priority<=99){
                    DEBUG(dbgMp3, "[B] Tick ["<<kernel->stats->totalTicks<<": Thread [" << t->getID() << "] is removed from queue L2");
                    DEBUG(dbgMp3, "[A] Tick [" << kernel->stats->totalTicks<< "]: Thread [" << t->getID()<<"] is inserted into queue L1");
                }
                t->priority+=10;
                DEBUG(dbgMp3, "[C] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<t->getID()<<"] changes its priority from ["<<t->priority-10<<"] to ["<<t->priority<<"]");
            }else if(t->priority>139){
                int old_priority = t->priority;
                t->priority=149;
                if(old_priority!=t->priority)
                    DEBUG(dbgMp3, "[C] Tick ["<<kernel->stats->totalTicks<<"]: Thread ["<<t->getID()<<"] changes its priority from ["<<old_priority<<"] to ["<<t->priority<<"]");
            }
            t->total_ready_time -= 1500;
            ReadyToRun(t);
        }else{
            ReadyToRun(t);
            //t->enter_ready_time = old_ready_time;
        }
    }
    kernel->scheduler->isaging = 0;
    (void) kernel->interrupt->SetLevel(oldLevel);
}