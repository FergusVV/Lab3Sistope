#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <stdint.h>
#include <pthread.h>

#define MAX_SEGMENTS 64
#define DEFAULT_THREADS 1
#define DEFAULT_OPS 1000
#define DEFAULT_SEED 42
#define DEFAULT_SEGMENTS 4
#define DEFAULT_SEG_LIMIT 4096
#define DEFAULT_PAGES 64
#define DEFAULT_FRAMES 32
#define DEFAULT_PAGE_SIZE 4096
#define DEFAULT_TLB_SIZE 16

#define SAFE_LOCK(m, unsafe)   do { if (!(unsafe)) pthread_mutex_lock(m); } while(0)
#define SAFE_UNLOCK(m, unsafe) do { if (!(unsafe)) pthread_mutex_unlock(m); } while(0)

typedef struct {
    char mode[8];              /* "seg" o "page" */
    int threads;
    int ops_per_thread;
    char workload[16];         /* "uniform" o "80-20" */
    unsigned int seed;
    int unsafe;
    int stats;
    /* Segmentacion */
    int segments;
    uint64_t seg_limits[MAX_SEGMENTS];
    /* Paginacion */
    int pages;
    int frames;
    int page_size;
    int tlb_size;
    char tlb_policy[8];        /* "fifo" */
    char evict_policy[8];      /* "fifo" */
} sim_config;

typedef struct {
    uint64_t translations_ok;
    uint64_t segfaults;
    uint64_t tlb_hits;
    uint64_t tlb_misses;
    uint64_t page_faults;
    double total_time_ns;
    uint64_t total_ops;
} thread_metrics;

void print_stats_seg(const sim_config *cfg, const thread_metrics *per_thread,
                     int num_threads, double runtime_sec);
void print_stats_page(const sim_config *cfg, const thread_metrics *per_thread,
                      int num_threads, uint64_t evictions,
                      uint64_t dirty_evictions, double runtime_sec);
void write_json_seg(const sim_config *cfg, const thread_metrics *per_thread,
                    int num_threads, double runtime_sec);
void write_json_page(const sim_config *cfg, const thread_metrics *per_thread,
                     int num_threads, uint64_t evictions,
                     uint64_t dirty_evictions, double runtime_sec);

#endif /* SIMULATOR_H */
