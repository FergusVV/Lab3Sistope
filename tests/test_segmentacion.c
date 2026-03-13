#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include "segmentacion.h"
#include "workloads.h"

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name) do { tests_total++; printf("  [TEST] %s... ", name); } while(0)
#define PASS() do { tests_passed++; printf("OK\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

static void test_pa_correcta(void)
{
    TEST("PA = base + offset");
    uint64_t limits[] = {1024, 2048, 4096, 8192};
    segment_table *t = seg_table_create(4, limits, 0);

    /* Thread 0: base[0]=0, base[1]=1024, base[2]=3072, base[3]=7168 */
    int64_t pa = translate_segment(t, 0, 500);
    if (pa != 500) { FAIL("base[0]+500 != 500"); seg_table_free(t); return; }

    pa = translate_segment(t, 1, 100);
    if (pa != 1124) { FAIL("base[1]+100 != 1124"); seg_table_free(t); return; }

    seg_table_free(t);
    PASS();
}

static void test_segfault_offset_excede_limit(void)
{
    TEST("Segfault cuando offset >= limit");
    uint64_t limits[] = {1024, 2048};
    segment_table *t = seg_table_create(2, limits, 0);

    /* Offset 1024 >= limit 1024 → segfault */
    int64_t pa = translate_segment(t, 0, 1024);
    if (pa != -1) { FAIL("Deberia ser segfault"); seg_table_free(t); return; }

    /* Offset 2047 < limit 2048 → ok */
    pa = translate_segment(t, 1, 2047);
    if (pa < 0) { FAIL("No deberia ser segfault"); seg_table_free(t); return; }

    seg_table_free(t);
    PASS();
}

static void test_translations_ok_plus_segfaults_eq_ops(void)
{
    TEST("translations_ok + segfaults == ops");
    uint64_t limits[] = {1024, 2048, 4096, 8192};
    segment_table *t = seg_table_create(4, limits, 0);

    uint64_t max_limit = 8192;
    unsigned int seed = 42;
    int ops = 1000;
    uint64_t ok = 0, sf = 0;

    for (int i = 0; i < ops; i++) {
        int seg_id;
        uint64_t offset;
        gen_va_seg_uniform(&seed, 4, max_limit, &seg_id, &offset);
        int64_t pa = translate_segment(t, seg_id, offset);
        if (pa >= 0) ok++;
        else sf++;
    }

    if (ok + sf != (uint64_t)ops) {
        FAIL("ok + sf != ops");
    } else if (sf == 0) {
        FAIL("Deberia haber segfaults con limites distintos");
    } else {
        PASS();
    }

    seg_table_free(t);
}

static void test_reproducibilidad(void)
{
    TEST("Reproducibilidad con misma semilla");
    uint64_t limits[] = {1024, 2048, 4096, 8192};
    uint64_t max_limit = 8192;

    uint64_t ok1 = 0, sf1 = 0, ok2 = 0, sf2 = 0;

    for (int run = 0; run < 2; run++) {
        segment_table *t = seg_table_create(4, limits, 0);
        unsigned int seed = 42;
        uint64_t ok = 0, sf = 0;
        for (int i = 0; i < 500; i++) {
            int seg_id;
            uint64_t offset;
            gen_va_seg_uniform(&seed, 4, max_limit, &seg_id, &offset);
            int64_t pa = translate_segment(t, seg_id, offset);
            if (pa >= 0) ok++;
            else sf++;
        }
        if (run == 0) { ok1 = ok; sf1 = sf; }
        else          { ok2 = ok; sf2 = sf; }
        seg_table_free(t);
    }

    if (ok1 != ok2 || sf1 != sf2) {
        FAIL("Resultados distintos con misma semilla");
    } else {
        PASS();
    }
}

int main(void)
{
    printf("=== Tests Segmentacion ===\n");
    test_pa_correcta();
    test_segfault_offset_excede_limit();
    test_translations_ok_plus_segfaults_eq_ops();
    test_reproducibilidad();
    printf("\nResultado: %d/%d tests pasaron\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
