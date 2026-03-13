#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include "segmentacion.h"
#include "paginacion.h"
#include "tlb.h"
#include "frame_allocator.h"
#include "workloads.h"

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name) do { tests_total++; printf("  [TEST] %s... ", name); } while(0)
#define PASS() do { tests_passed++; printf("OK\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* --- Test data para segmentacion --- */
typedef struct {
    int thread_id;
    unsigned int seed;
    segment_table *seg_table;
    uint64_t max_limit;
    int num_segments;
    int ops;
    uint64_t translations_ok;
    uint64_t segfaults;
} seg_thread_data;

static void *seg_worker(void *arg)
{
    seg_thread_data *td = (seg_thread_data *)arg;
    td->translations_ok = 0;
    td->segfaults = 0;

    for (int i = 0; i < td->ops; i++) {
        int seg_id;
        uint64_t offset;
        gen_va_seg_uniform(&td->seed, td->num_segments, td->max_limit,
                           &seg_id, &offset);
        int64_t pa = translate_segment(td->seg_table, seg_id, offset);
        if (pa >= 0) td->translations_ok++;
        else td->segfaults++;
    }
    return NULL;
}

static void test_seg_multithread_reproducible(void)
{
    TEST("Segmentacion multi-thread SAFE es reproducible");
    uint64_t limits[] = {1024, 2048, 4096, 8192};
    int num_threads = 4;
    int ops = 2000;
    uint64_t max_limit = 8192;

    uint64_t total_ok[2] = {0, 0};
    uint64_t total_sf[2] = {0, 0};

    for (int run = 0; run < 2; run++) {
        seg_thread_data tdata[4];
        pthread_t threads[4];

        for (int t = 0; t < num_threads; t++) {
            tdata[t].thread_id = t;
            tdata[t].seed = (unsigned int)(42 + t);
            tdata[t].seg_table = seg_table_create(4, limits, t);
            tdata[t].max_limit = max_limit;
            tdata[t].num_segments = 4;
            tdata[t].ops = ops;
        }

        for (int t = 0; t < num_threads; t++)
            pthread_create(&threads[t], NULL, seg_worker, &tdata[t]);
        for (int t = 0; t < num_threads; t++)
            pthread_join(threads[t], NULL);

        for (int t = 0; t < num_threads; t++) {
            total_ok[run] += tdata[t].translations_ok;
            total_sf[run] += tdata[t].segfaults;
            seg_table_free(tdata[t].seg_table);
        }
    }

    if (total_ok[0] != total_ok[1] || total_sf[0] != total_sf[1]) {
        FAIL("Resultados distintos en 2 ejecuciones");
    } else {
        PASS();
    }
}

/* --- Test data para paginacion concurrente UNSAFE --- */
typedef struct {
    int thread_id;
    unsigned int seed;
    frame_allocator *fa;
    page_table *pt;
    tlb_t *tlb;
    int num_pages;
    int ops;
} page_thread_data;

static void *page_unsafe_worker(void *arg)
{
    page_thread_data *td = (page_thread_data *)arg;

    for (int i = 0; i < td->ops; i++) {
        uint64_t vpn = (uint64_t)(rand_r(&td->seed) % td->num_pages);

        /* Intentar lookup en TLB */
        int64_t frame = tlb_lookup(td->tlb, vpn);
        if (frame >= 0) continue;

        /* TLB miss: consultar page table */
        if (td->pt->entries[vpn].valid) {
            frame = (int64_t)td->pt->entries[vpn].frame_number;
            tlb_insert(td->tlb, vpn, (uint64_t)frame);
            continue;
        }

        /* Page fault: asignar frame (sin locks en UNSAFE) */
        int allocated = fa_allocate(td->fa, td->thread_id, vpn);
        if (allocated >= 0) {
            td->pt->entries[vpn].frame_number = (uint64_t)allocated;
            td->pt->entries[vpn].valid = 1;
            tlb_insert(td->tlb, vpn, (uint64_t)allocated);
        }
    }
    return NULL;
}

static void test_unsafe_no_crash(void)
{
    TEST("Modo UNSAFE no crashea (paginacion multi-thread)");
    int num_frames = 8;
    int num_pages = 32;
    int num_threads = 4;
    int ops = 500;

    frame_allocator *fa = fa_create(num_frames, 1); /* unsafe=1 */
    page_table *pts[4];
    tlb_t *tlbs[4];
    page_thread_data tdata[4];
    pthread_t threads[4];

    for (int t = 0; t < num_threads; t++) {
        pts[t] = page_table_create(num_pages);
        tlbs[t] = tlb_init(8);
    }
    fa_set_globals(fa, tlbs, pts, num_threads);

    /* Lanzar threads reales que compiten por frames sin locks */
    for (int t = 0; t < num_threads; t++) {
        tdata[t].thread_id = t;
        tdata[t].seed = (unsigned int)(42 + t);
        tdata[t].fa = fa;
        tdata[t].pt = pts[t];
        tdata[t].tlb = tlbs[t];
        tdata[t].num_pages = num_pages;
        tdata[t].ops = ops;
        pthread_create(&threads[t], NULL, page_unsafe_worker, &tdata[t]);
    }

    for (int t = 0; t < num_threads; t++)
        pthread_join(threads[t], NULL);

    /* Si llegamos aqui sin crash, paso el test */
    for (int t = 0; t < num_threads; t++) {
        page_table_free(pts[t]);
        tlb_free(tlbs[t]);
    }
    fa_free(fa);
    PASS();
}

int main(void)
{
    printf("=== Tests Concurrencia ===\n");
    test_seg_multithread_reproducible();
    test_unsafe_no_crash();
    printf("\nResultado: %d/%d tests pasaron\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
