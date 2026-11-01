/* macos: implementations of compat_mac.h. */
#ifdef __APPLE__
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include "compat_mac.h"

/* The guest ranges are zero-fill segments of the executable, so the kernel maps them at exec
 * time, before dyld, Rosetta, Metal or malloc can place anything there. CMake puts them with
 * -Wl,-segaddr,GUEST_EBOOT,0x400000 -Wl,-segaddr,GUEST_LOW,0x700000000
 * -Wl,-segaddr,GUEST_USER,0x7000000000 (plus -no_pie, -pagezero_size 0x4000 and a high
 * -image_base). The runtime then maps guest memory over them with MAP_FIXED, as shadPS4 does. */
__asm__(".zerofill GUEST_EBOOT,GUEST_EBOOT,__guest_eboot,0x7FC00000\n"
        ".no_dead_strip __guest_eboot\n"
        ".zerofill GUEST_LOW,GUEST_LOW,__guest_low,0x8C0000000\n"
        ".no_dead_strip __guest_low\n"
        ".zerofill GUEST_USER,GUEST_USER,__guest_user,0x8C00000000\n"
        ".no_dead_strip __guest_user\n");
/* The segments are found by name: a C reference to the symbols would need a 32-bit
 * rip-relative fixup from the high image base, out of range. */
static void reserve(const char *segment, uint64_t start, uint64_t end) {
    unsigned long size=0;
    const uint8_t *at=getsegmentdata(&_mh_execute_header,segment,&size);
    if ((uintptr_t)at!=start || size!=end-start) {
        fprintf(stderr,"STOP: segment %s is at %p, not %#llx (link with -Wl,-segaddr,%s,%#llx)\n",segment,
                (const void *)at,(unsigned long long)start,segment,(unsigned long long)start);
        exit(21);
    }
    if (mmap((void *)(uintptr_t)start,end-start,PROT_NONE,MAP_PRIVATE|MAP_ANON|MAP_FIXED|MAP_NORESERVE,-1,0)==MAP_FAILED) {
        fprintf(stderr,"STOP: cannot reserve the guest range %#llx-%#llx\n",(unsigned long long)start,(unsigned long long)end);
        exit(21);
    }
}
void compat_mac_reserve_guest(void) {
    static int done;
    if (done) return;
    done=1;
    reserve("GUEST_EBOOT",COMPAT_MAC_EBOOT_MIN,COMPAT_MAC_EBOOT_END);
    reserve("GUEST_LOW",COMPAT_MAC_LOW_MIN,COMPAT_MAC_LOW_END);
    reserve("GUEST_USER",COMPAT_MAC_USER_MIN,COMPAT_MAC_USER_END);
}

#define ALT_STACK_SIZE (512u*1024u)
void compat_mac_altstack(void) {
    unsigned char *p=mmap(NULL,ALT_STACK_SIZE+4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if (p==MAP_FAILED) return;
    mprotect(p,4096,PROT_NONE); /* guard page below the stack */
    stack_t stack={.ss_sp=p+4096,.ss_size=ALT_STACK_SIZE,.ss_flags=0};
    if (sigaltstack(&stack,NULL)) munmap(p,ALT_STACK_SIZE+4096);
}

/* ponytail: timed locks poll trylock (sched_yield, then 50 us naps) until the deadline, so a
 * waiter can lag a release by ~50 us; the game's timed locks are rare. A condvar-based lock if
 * one shows up in a profile. */
static int expired(const struct timespec *deadline) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME,&now);
    return now.tv_sec>deadline->tv_sec || (now.tv_sec==deadline->tv_sec && now.tv_nsec>=deadline->tv_nsec);
}
static void nap(unsigned tries) {
    if (tries<64) { sched_yield(); return; }
    const struct timespec t={0,50000};
    nanosleep(&t,NULL);
}
int compat_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline) {
    for (unsigned tries=0;;++tries) {
        const int e=pthread_mutex_trylock(mutex);
        if (e!=EBUSY) return e;
        if (expired(deadline)) return ETIMEDOUT;
        nap(tries);
    }
}
int compat_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *deadline) {
    for (unsigned tries=0;;++tries) {
        const int e=pthread_rwlock_tryrdlock(lock);
        if (e!=EBUSY) return e;
        if (expired(deadline)) return ETIMEDOUT;
        nap(tries);
    }
}
int compat_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *deadline) {
    for (unsigned tries=0;;++tries) {
        const int e=pthread_rwlock_trywrlock(lock);
        if (e!=EBUSY) return e;
        if (expired(deadline)) return ETIMEDOUT;
        nap(tries);
    }
}
#endif
