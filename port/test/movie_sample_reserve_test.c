#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define NSAMP 3
static int attempt, fail_at, live;
static uint32_t next_id=100;
static uint32_t gheap_calloc(uint32_t n) {
    assert(n); if (++attempt == fail_at) return 0;
    live++; return ++next_id;
}
static void gheap_free(uint32_t p) { if (p) live--; }
#include "../src/dx/movie_sample_reserve.h"
int main(void) {
    uint32_t b[3]={0}, c[3]={0}, before[3];
    assert(movie_samples_reserve(b,c,3,100));
    assert(live==3 && c[0]==100 && c[2]==100);
    for (int i=0;i<3;i++) before[i]=b[i];
    attempt=0; fail_at=2;
    assert(!movie_samples_reserve(b,c,3,200));
    for (int i=0;i<3;i++) assert(b[i]==before[i] && c[i]==100);
    assert(live==3);
    attempt=0; fail_at=0;
    assert(movie_samples_reserve(b,c,3,200));
    assert(live==3 && c[0]==200 && c[2]==200);
    for (int i=0;i<3;i++) before[i]=b[i];
    attempt=0; assert(movie_samples_reserve(b,c,3,50));
    assert(attempt==0 && b[0]==before[0] && b[2]==before[2]);
    assert(!movie_samples_reserve(b,c,3,0));
    puts("movie sample resize preserves existing buffers on failure");
}
