/* macOS compat polyfills (src/compat_mac.h): a timed lock times out, a barrier releases, a
 * pthread key slot is readable through GS the way the patched guest TLS load reads it, and the
 * eboot reservation below 2 GiB is claimed once per process. */
#include "runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

static pthread_barrier_t gate;
static void *arrive(void *arg) { (void)arg; int e = pthread_barrier_wait(&gate); assert(e == 0 || e == PTHREAD_BARRIER_SERIAL_THREAD); return NULL; }

int main(void) {
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    assert(pthread_mutex_lock(&m) == 0);
    struct timespec deadline; clock_gettime(CLOCK_REALTIME, &deadline); deadline.tv_nsec += 50 * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec += 1; deadline.tv_nsec -= 1000000000L; }
    struct timespec before, after; clock_gettime(CLOCK_MONOTONIC, &before);
    assert(pthread_mutex_timedlock(&m, &deadline) == ETIMEDOUT);
    clock_gettime(CLOCK_MONOTONIC, &after);
    long waited_ms = (after.tv_sec - before.tv_sec) * 1000 + (after.tv_nsec - before.tv_nsec) / 1000000;
    assert(waited_ms >= 45 && waited_ms < 500);
    assert(pthread_mutex_unlock(&m) == 0);
    assert(pthread_mutex_timedlock(&m, &deadline) == 0); /* free lock: taken even past the deadline */

    assert(pthread_barrier_init(&gate, NULL, 2) == 0);
    pthread_t t; assert(pthread_create(&t, NULL, arrive, NULL) == 0);
    int e = pthread_barrier_wait(&gate); assert(e == 0 || e == PTHREAD_BARRIER_SERIAL_THREAD);
    pthread_join(t, NULL);
    assert(pthread_barrier_destroy(&gate) == 0);

    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    t1 = t0; t1.tv_nsec += 20 * 1000000L; if (t1.tv_nsec >= 1000000000L) { t1.tv_sec += 1; t1.tv_nsec -= 1000000000L; }
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t1, NULL)) {}
    struct timespec t2; clock_gettime(CLOCK_MONOTONIC, &t2);
    assert(t2.tv_sec > t1.tv_sec || (t2.tv_sec == t1.tv_sec && t2.tv_nsec >= t1.tv_nsec));

    unsigned char random[300] = {0}; assert(getrandom(random, sizeof(random), 0) == (ssize_t)sizeof(random));
    int zeros = 0; for (size_t i = 0; i < sizeof(random); ++i) zeros += !random[i]; assert(zeros < 100);

    struct rusage usage; assert(getrusage(RUSAGE_THREAD, &usage) == 0);

#ifdef __APPLE__
    pthread_key_t key; assert(pthread_key_create(&key, NULL) == 0);
    assert(pthread_setspecific(key, (void *)(uintptr_t)0x1234abcd) == 0);
    void *through_gs = NULL;
    __asm__ volatile("movq %%gs:(%1), %0" : "=r"(through_gs) : "r"((uintptr_t)key * 8));
    assert((uintptr_t)through_gs == 0x1234abcd);
    /* The loader's macOS version gate (probe.c macos_version_ok): 15.4 and later pass. */
    {
        int major=0, minor=0;
        const char *cases[]={"15.3.1","15.4","15.4.1","16.0","26.1","27.0.1","14.7"};
        const int expected[]={0,1,1,1,1,1,0};
        for (size_t i=0;i<sizeof(cases)/sizeof(*cases);++i) {
            major=minor=0; sscanf(cases[i],"%d.%d",&major,&minor);
            int ok = major>15 || (major==15 && minor>=4);
            assert(ok==expected[i]);
        }
    }
    /* Sub-2 GiB eboot reservation: 256 MiB below 0x80000000, claimed once per process.
     * probe.c calls reserve_eboot_range() from main() and again from probe_main(), so the
     * second call must return the first block, not reserve a further 256 MiB. */
    {
        const uint64_t need = UINT64_C(256) << 20;
        void *first = compat_reserve_eboot_range();
        assert(first != NULL && (uintptr_t)first >= 0x400000 && (uintptr_t)first + need <= 0x80000000ULL);
        assert(compat_reserve_eboot_range() == first);
    }
#endif
    puts("compat_mac: PASS");
    return 0;
}
