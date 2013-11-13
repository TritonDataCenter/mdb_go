#ifndef	_MDB_GO_TYPES_H_
#define	_MDB_GO_TYPES_H_

typedef struct P P;
typedef struct G G;
typedef struct M M;
typedef struct Eface Eface;
typedef struct Defer Defer;
typedef struct Gobuf Gobuf;
typedef struct Lock Lock;
typedef struct Note Note;
typedef struct Timer Timer;
typedef struct Timers Timers;
typedef struct DeferChunk DeferChunk;
typedef struct Panic Panic;
typedef struct LibCall LibCall;
typedef struct SEH SEH;
typedef struct GCStats GCStats;

enum {
        GS_Gidle,
        GS_Grunnable,
        GS_Grunning,
        GS_Gsyscall,
        GS_Gwaiting,
        GS_Gmoribund_unused,
        GS_Gdead,
};

enum {
        PS_Pidle,
        PS_Prunning,
        PS_Psyscall,
        PS_Pgcstop,
        PS_Pdead,
};

enum {
        // Per-M stack segment cache size.
        StackCacheSize = 32,
        // Global <-> per-M stack segment cache transfer batch size.
        StackCacheBatch = 16,
};

struct GCStats {
        uint64_t nhandoff;
        uint64_t nhandoffcnt;
        uint64_t nprocyield;
        uint64_t nosyield;
        uint64_t nsleep;
};

struct SEH {
        void* prev;
        void* handler;
};

struct Gobuf {
        // The offsets of sp, pc, and g are known to (hard-coded in) libmach.
        uintptr_t sp;
        uintptr_t pc;
        G* g;
        uintptr_t ret;
        void* ctxt;
        uintptr_t lr;
};

struct LibCall {
        void (*fn)(void*);
        uintptr_t n;      // number of parameters
        void* args;   // parameters
        uintptr_t r1;     // return values
        uintptr_t r2;
        uintptr_t err;    // error number
};

struct DeferChunk {
        DeferChunk *prev;
        uintptr_t off;
};

struct Eface {
        void*   type; /*XXX Type*/
        void*   data;
};

struct Defer {
        int32_t   siz;
        uint8_t    special;        // not part of defer frame
        uint8_t    free;           // if special, free when done
        uint8_t*   argp;           // where args were copied from
        uint8_t*   pc;
        void*        fn; /*XXX FuncVal*/
        Defer*  link;
        void*   args[1];        // padded to actual size
};

struct Panic {
        Eface   arg;            // argument to panic
        uintptr_t stackbase;      // g->stackbase in panic
        Panic*  link;           // link to earlier panic
        uint8_t    recovered;      // whether this panic is over
};

struct G {
        // stackguard0 can be set to StackPreempt as opposed to stackguard
        uintptr_t stackguard0;    // cannot move - also known to linker, libmach, runtime/cgo
        uintptr_t stackbase;      // cannot move - also known to libmach, runtime/cgo
        uint32_t  panicwrap;      // cannot move - also known to linker
        uint32_t  selgen;         // valid sudog pointer
        Defer*  defer;
        Panic*  panic;
        Gobuf   sched;
        uintptr_t syscallstack;   // if status==Gsyscall, syscallstack = stackbase to use during gc
        uintptr_t syscallsp;      // if status==Gsyscall, syscallsp = sched.sp to use during gc
        uintptr_t syscallpc;      // if status==Gsyscall, syscallpc = sched.pc to use during gc
        uintptr_t syscallguard;   // if status==Gsyscall, syscallguard = stackguard to use during gc
        uintptr_t stackguard;     // same as stackguard0, but not set to StackPreempt
        uintptr_t stack0;
        uintptr_t stacksize;
        G*      alllink;        // on allg
        void*   param;          // passed parameter on wakeup
        int16_t   status;
        int64_t   goid;
        int8_t*   waitreason;     // if status==Gwaiting
        G*      schedlink;
        uint8_t    ispanic;
        uint8_t    issystem;       // do not output in stack dump
        uint8_t    isbackground;   // ignore in deadlock detector
        uint8_t    preempt;        // preemption signal, duplicates stackguard0 = StackPreempt
        int8_t    raceignore;     // ignore race detection events
        M*      m;              // for debuggers, but offset not hard-coded
        M*      lockedm;
        int32_t   sig;
        int32_t   writenbuf;
        uint8_t*   writebuf;
        DeferChunk*     dchunk;
        DeferChunk*     dchunknext;
        uintptr_t sigcode0;
        uintptr_t sigcode1;
        uintptr_t sigpc;
        uintptr_t gopc;           // pc of go statement that created this goroutine
        uintptr_t racectx;
        uintptr_t end[];
};

struct Lock {
        // Futex-based impl treats it as uint32 key,
        // while sema-based impl as M* waitm.
        // Used to be a union, but unions break precise GC.
        uintptr_t key;
};

struct Note {
        // Futex-based impl treats it as uint32 key,
        // while sema-based impl as M* waitm.
        // Used to be a union, but unions break precise GC.
        uintptr_t key;
};

struct P {
        Lock __lock;

        int32_t   id;
        uint32_t  status;         // one of Pidle/Prunning/...
        P*      link;
        uint32_t  schedtick;      // incremented on every scheduler call
        uint32_t  syscalltick;    // incremented on every system call
        M*      m;              // back-link to associated M (nil if idle)
        void* mcache; /* XXX MCache */

        // Queue of runnable goroutines.
        G**     runq;
        int32_t   runqhead;
        int32_t   runqtail;
        int32_t   runqsize;

        // Available G's (status == Gdead)
        G*      gfree;
        int32_t   gfreecnt;

        uint8_t    pad[64];
};

struct M {
        G*      g0;             // goroutine with scheduling stack
        void*   moreargp;       // argument pointer for more stack
        Gobuf   morebuf;        // gobuf arg to morestack

        // Fields not known to debuggers.
        uint32_t  moreframesize;  // size arguments to morestack
        uint32_t  moreargsize;
        uintptr_t cret;           // return value from C
        uint64_t  procid;         // for debuggers, but offset not hard-coded
        G*      gsignal;        // signal-handling G
        uintptr_t tls[4];         // thread-local storage (for x86 extern register)
        void    (*mstartfn)(void);
        G*      curg;           // current running goroutine
        G*      caughtsig;      // goroutine running during fatal signal
        P*      p;              // attached P for executing Go code (nil if not executing Go code)
        P*      nextp;
        int32_t   id;
        int32_t   mallocing;
        int32_t   throwing;
        int32_t   gcing;
        int32_t   locks;
        int32_t   dying;
        int32_t   profilehz;
        int32_t   helpgc;
        uint8_t    spinning;
        uint32_t  fastrand;
        uint64_t  ncgocall;       // number of cgo calls in total
        int32_t   ncgo;           // number of cgo calls currently in progress
        void* cgomal; /* XXX CgoMal */
        Note    park;
        M*      alllink;        // on allm
        M*      schedlink;
        uint32_t  machport;       // Return address for Mach IPC (OS X)
        void* mcache; /* XXX MCache */
        int32_t   stackinuse;
        uint32_t  stackcachepos;
        uint32_t  stackcachecnt;
        void*   stackcache[StackCacheSize];
        G*      lockedg;
        uintptr_t createstack[32];// Stack that created this thread.
        uint32_t  freglo[16];     // D[i] lsb and F[i]
        uint32_t  freghi[16];     // D[i] msb and F[i+16]
        uint32_t  fflag;          // floating point compare flags
        uint32_t  locked;         // tracking for LockOSThread
        M*      nextwaitm;      // next M waiting for lock
        uintptr_t waitsema;       // semaphore for parking on locks
        uint32_t  waitsemacount;
        uint32_t  waitsemalock;
        GCStats gcstats;
        uint8_t    racecall;
        uint8_t    needextram;
        void    (*waitunlockf)(Lock*);
        void*   waitlock;

        uintptr_t settype_buf[1024];
        uintptr_t settype_bufsize;

        void*   perrno;         // pointer to TLS errno
        LibCall libcall;        // put here to avoid large runtime·sysvicall stack
        SEH*    seh;
        uintptr_t end[];
};

// Package time knows the layout of this structure.
// If this struct changes, adjust ../time/sleep.go:/runtimeTimer.
struct Timer {
        int32_t   i;      // heap index

        // Timer wakes up at when, and then at when+period, ... (period > 0 only)
        // each time calling f(now, arg) in the timer goroutine, so f must be
        // a well-behaved function and not block.
        int64_t   when;
        int64_t   period;
        void *fv; /* XXX FuncVal */
        Eface   arg;
};

struct Timers {
        Lock __lock;
        G       *timerproc;
        uint8_t            sleeping;
        uint8_t            rescheduling;
        Note    waitnote;
        Timer   **t;
        int32_t   len;
        int32_t   cap;
};

#endif	/* !_MDB_GO_TYPES_H_ */
