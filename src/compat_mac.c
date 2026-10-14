/* Implementations for compat_mac.h (macOS only; empty elsewhere). */
#include "compat_mac.h"
#ifdef __APPLE__
#include <string.h>
#include <sys/random.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#undef getrusage /* the real one, for RUSAGE_SELF */

/* Shadow of the Colossus packs pointers to its static data into 31 bits, so the eboot image and
 * its trampolines must live below 2 GiB. The loader is linked with -Wl,-pagezero_size,0x400000 to
 * shrink __PAGEZERO, and this claims the first free 256 MiB above 0x400000 that ends below
 * 0x80000000. main() and probe_main() both call it, so the block is cached: a second call returns
 * the first one instead of reserving a further 256 MiB. */
#define EBOOT_MIN UINT64_C(0x400000)
#define EBOOT_LIMIT UINT64_C(0x80000000)
#define EBOOT_RESERVATION (UINT64_C(256) << 20)
static void *eboot_range;
void *compat_reserve_eboot_range(void) {
    if (eboot_range) return eboot_range;
    for (uintptr_t at=EBOOT_MIN; at+EBOOT_RESERVATION<=EBOOT_LIMIT;) {
        mach_vm_address_t addr=at;
        mach_vm_size_t size=0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t obj=MACH_PORT_NULL;
        kern_return_t kr=mach_vm_region(mach_task_self(),&addr,&size,VM_REGION_BASIC_INFO_64,
                                        (vm_region_info_t)&info,&count,&obj);
        uintptr_t gap_end=(kr==KERN_SUCCESS) ? (uintptr_t)addr : EBOOT_LIMIT;
        if (gap_end>EBOOT_LIMIT) gap_end=EBOOT_LIMIT;
        if (gap_end>at && gap_end-at>=EBOOT_RESERVATION) {
            void *p=mmap((void *)at,EBOOT_RESERVATION,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED|MAP_NORESERVE,-1,0);
            if (p!=MAP_FAILED) { eboot_range=p; break; }
        }
        if (kr!=KERN_SUCCESS) break;
        at=((uintptr_t)addr+(uintptr_t)size+0xffff)&~(uintptr_t)0xffff;
    }
    return eboot_range;
}

ssize_t compat_getrandom(void *buffer, size_t size, unsigned flags) {
    (void)flags;
    unsigned char *at = buffer;
    for (size_t done = 0; done < size;) {
        size_t chunk = size - done > 256 ? 256 : size - done;
        if (getentropy(at + done, chunk)) return -1;
        done += chunk;
    }
    return (ssize_t)size;
}

int compat_getrusage(int who, struct rusage *out) {
    if (who != RUSAGE_THREAD) return getrusage(who, out);
    memset(out, 0, sizeof(*out));
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    mach_port_t self = mach_thread_self();
    kern_return_t result = thread_info(self, THREAD_BASIC_INFO, (thread_info_t)&info, &count);
    mach_port_deallocate(mach_task_self(), self);
    if (result != KERN_SUCCESS) { errno = EINVAL; return -1; }
    out->ru_utime.tv_sec = info.user_time.seconds; out->ru_utime.tv_usec = info.user_time.microseconds;
    out->ru_stime.tv_sec = info.system_time.seconds; out->ru_stime.tv_usec = info.system_time.microseconds;
    return 0;
}

int compat_clock_nanosleep(clockid_t clock, int flags, const struct timespec *deadline, struct timespec *remain) {
    (void)flags; (void)remain;
    struct timespec now;
    if (clock_gettime(clock, &now)) return errno;
    int64_t ns = ((int64_t)deadline->tv_sec - now.tv_sec) * 1000000000 + (deadline->tv_nsec - now.tv_nsec);
    if (ns <= 0) return 0;
    struct timespec wait = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
    return nanosleep(&wait, NULL) ? EINTR : 0;
}

static int past(const struct timespec *deadline) {
    struct timespec now; clock_gettime(CLOCK_REALTIME, &now);
    return now.tv_sec > deadline->tv_sec || (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}
static int poll_lock(int (*try_lock)(void *), void *lock, const struct timespec *deadline) {
    if (deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000L) return EINVAL;
    for (;;) {
        int e = try_lock(lock);
        if (e != EBUSY) return e;
        if (past(deadline)) return ETIMEDOUT;
        struct timespec nap = {0, 50000};
        nanosleep(&nap, NULL);
    }
}
static int try_mutex(void *m) { return pthread_mutex_trylock(m); }
static int try_rd(void *l) { return pthread_rwlock_tryrdlock(l); }
static int try_wr(void *l) { return pthread_rwlock_trywrlock(l); }
int compat_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline) { return poll_lock(try_mutex, mutex, deadline); }
int compat_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *deadline) { return poll_lock(try_rd, lock, deadline); }
int compat_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *deadline) { return poll_lock(try_wr, lock, deadline); }

int compat_barrier_init(compat_barrier_t *barrier, const void *attr, unsigned count) {
    (void)attr;
    if (!count) return EINVAL;
    int e = pthread_mutex_init(&barrier->lock, NULL);
    if (!e) e = pthread_cond_init(&barrier->wake, NULL);
    barrier->count = count; barrier->waiting = 0; barrier->generation = 0;
    return e;
}
int compat_barrier_wait(compat_barrier_t *barrier) {
    pthread_mutex_lock(&barrier->lock);
    unsigned generation = barrier->generation;
    if (++barrier->waiting == barrier->count) {
        barrier->waiting = 0; ++barrier->generation;
        pthread_cond_broadcast(&barrier->wake);
        pthread_mutex_unlock(&barrier->lock);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (generation == barrier->generation) pthread_cond_wait(&barrier->wake, &barrier->lock);
    pthread_mutex_unlock(&barrier->lock);
    return 0;
}
int compat_barrier_destroy(compat_barrier_t *barrier) {
    pthread_cond_destroy(&barrier->wake);
    return pthread_mutex_destroy(&barrier->lock);
}
#endif
