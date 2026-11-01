/* macos: Darwin (x86_64 under Rosetta 2) counterparts of the Linux calls the runtime uses.
 * Included through runtime.h, like compat_win.h, so the macros below only replace calls in
 * this project's own sources. */
#ifndef BB_COMPAT_MAC_H
#define BB_COMPAT_MAC_H
#ifdef __APPLE__
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/resource.h>

/* Guest address ranges reserved at exec time by zero-fill segments (compat_mac.c), each placed
 * by the matching -Wl,-segaddr link flag. */
#define COMPAT_MAC_EBOOT_MIN UINT64_C(0x400000)     /* eboot + CPU patch trampolines (31-bit pointers) */
#define COMPAT_MAC_EBOOT_END UINT64_C(0x80000000)
#define COMPAT_MAC_LOW_MIN   UINT64_C(0x700000000)  /* PS4 system-managed window + host low maps */
#define COMPAT_MAC_LOW_END   UINT64_C(0xFC0000000)  /* Rosetta's commpage starts here */
#define COMPAT_MAC_USER_MIN  UINT64_C(0x7000000000) /* Metal takes 0x1000000000-0x6fffffffff under Rosetta */
#define COMPAT_MAC_USER_END  UINT64_C(0xFC00000000)
/* Remaps the three reservations PROT_NONE (unmapped guest memory faults, like Windows'
 * PAGE_NOACCESS placeholders). Idempotent; exits with a message when the link flags are missing. */
void compat_mac_reserve_guest(void);
/* A 512 KiB signal stack for the calling thread: SotC fibers run on 16-64 KiB stacks without
 * guard pages, too small for the fault handler (probe.c). */
void compat_mac_altstack(void);

#define CLOCK_REALTIME_COARSE CLOCK_REALTIME
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP PTHREAD_RECURSIVE_MUTEX_INITIALIZER
/* ponytail: process CPU time for a thread query; thread_info(THREAD_BASIC_INFO) if a game needs it per thread. */
#define RUSAGE_THREAD RUSAGE_SELF
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec

static inline int compat_gettid(void) {
    uint64_t id=0;
    pthread_threadid_np(NULL,&id);
    return (int)id;
}
#define gettid compat_gettid
static inline ssize_t compat_getrandom(void *buffer, size_t size, unsigned flags) {
    (void)flags;
    arc4random_buf(buffer,size);
    return (ssize_t)size;
}
#define getrandom compat_getrandom
/* Darwin names only the calling thread. */
#define pthread_setname_np(thread, name) ((void)(thread), pthread_setname_np(name))
/* No pthread_condattr_setclock: condition variables wait on CLOCK_REALTIME (as on Windows). */
#define pthread_condattr_setclock(attr, clock) ((void)(attr), (clock)==CLOCK_REALTIME ? 0 : EINVAL)

/* clock_nanosleep: an absolute deadline becomes a relative nanosleep. */
#define TIMER_ABSTIME 1
static inline int compat_clock_nanosleep(clockid_t clock, int flags, const struct timespec *t, struct timespec *rem) {
    struct timespec d=*t;
    if (flags & TIMER_ABSTIME) {
        struct timespec now;
        if (clock_gettime(clock,&now)) return errno;
        d.tv_sec-=now.tv_sec; d.tv_nsec-=now.tv_nsec;
        if (d.tv_nsec<0) { d.tv_nsec+=1000000000; --d.tv_sec; }
        if (d.tv_sec<0) return 0;
        rem=NULL;
    }
    return nanosleep(&d,rem) ? errno : 0;
}
#define clock_nanosleep compat_clock_nanosleep

/* Timed locks Darwin lacks (absolute CLOCK_REALTIME deadlines). */
int compat_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline);
int compat_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *deadline);
int compat_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *deadline);
#define pthread_mutex_timedlock compat_mutex_timedlock
#define pthread_rwlock_timedrdlock compat_rwlock_timedrdlock
#define pthread_rwlock_timedwrlock compat_rwlock_timedwrlock
#endif
#endif
