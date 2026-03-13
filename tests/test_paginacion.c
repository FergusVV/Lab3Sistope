#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "paginacion.h"
#include "tlb.h"
#include "frame_allocator.h"
#include "workloads.h"

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name) do { tests_total++; printf("  [TEST] %s... ", name); } while(0)
#define PASS() do { tests_passed++; printf("OK\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

static void test_page_fault_primer_acceso(void)
{
    TEST("Page fault en primer acceso");
    page_table *pt = page_table_create(64);
    frame_allocator *fa = fa_create(32, 0);

    /* Primer acceso a VPN 5 → debe ser invalid */
    if (pt->entries[5].valid != 0) {
        FAIL("VPN 5 deberia ser invalida");
        page_table_free(pt);
        fa_free(fa);
        return;
    }

    /* Asignar frame */
    int frame = fa_allocate(fa, 0, 5);
    if (frame < 0) {
        FAIL("No se pudo asignar frame");
        page_table_free(pt);
        fa_free(fa);
        return;
    }

    pt->entries[5].frame_number = (uint64_t)frame;
    pt->entries[5].valid = 1;

    /* Ahora debe ser valida */
    if (pt->entries[5].valid != 1) {
        FAIL("VPN 5 deberia ser valida despues de asignar");
    } else {
        PASS();
    }

    page_table_free(pt);
    fa_free(fa);
}

static void test_tlb_hit_segundo_acceso(void)
{
    TEST("TLB hit en segundo acceso al mismo VPN");
    tlb_t *tlb = tlb_init(16);

    /* Insertar VPN 10 → frame 3 */
    tlb_insert(tlb, 10, 3);

    /* Lookup VPN 10 */
    int64_t f = tlb_lookup(tlb, 10);
    if (f != 3) {
        FAIL("TLB hit deberia retornar frame 3");
    } else {
        PASS();
    }

    tlb_free(tlb);
}

static void test_tlb_miss(void)
{
    TEST("TLB miss para VPN no insertada");
    tlb_t *tlb = tlb_init(16);

    int64_t f = tlb_lookup(tlb, 99);
    if (f != -1) {
        FAIL("Deberia ser TLB miss (-1)");
    } else {
        PASS();
    }

    tlb_free(tlb);
}

static void test_eviction_frames_llenos(void)
{
    TEST("Eviction cuando frames estan llenos");
    int num_frames = 4;
    frame_allocator *fa = fa_create(num_frames, 0);

    /* Crear page tables y TLBs para 1 thread */
    page_table *pt = page_table_create(64);
    tlb_t *tlb = tlb_init(16);

    page_table *pts[1] = { pt };
    tlb_t *tlbs[1] = { tlb };
    fa_set_globals(fa, tlbs, pts, 1);

    /* Llenar todos los frames */
    for (int i = 0; i < num_frames; i++) {
        int f = fa_allocate(fa, 0, (uint64_t)i);
        pt->entries[i].frame_number = (uint64_t)f;
        pt->entries[i].valid = 1;
        tlb_insert(tlb, (uint64_t)i, (uint64_t)f);
    }

    if (fa->free_count != 0) {
        FAIL("Deberian estar todos los frames ocupados");
        page_table_free(pt);
        tlb_free(tlb);
        fa_free(fa);
        return;
    }

    /* Siguiente asignacion debe causar eviction */
    uint64_t evictions_antes = fa->evictions;
    int f = fa_allocate(fa, 0, 10);

    if (f < 0) {
        FAIL("Deberia obtener frame por eviction");
    } else if (fa->evictions != evictions_antes + 1) {
        FAIL("Evictions deberia incrementar en 1");
    } else {
        /* La primera VPN asignada (VPN 0) deberia ser invalidada */
        if (pt->entries[0].valid != 0) {
            FAIL("VPN victima deberia ser invalidada");
        } else {
            PASS();
        }
    }

    page_table_free(pt);
    tlb_free(tlb);
    fa_free(fa);
}

static void test_pa_correcta(void)
{
    TEST("PA = frame * page_size + offset");
    int page_size = 4096;
    int frame = 5;
    int offset = 100;
    uint64_t pa = (uint64_t)frame * page_size + offset;

    if (pa != 20580) {
        FAIL("5*4096+100 deberia ser 20580");
    } else {
        PASS();
    }
}

static void test_tlb_fifo_reemplazo(void)
{
    TEST("TLB FIFO: reemplaza entrada mas antigua");
    tlb_t *tlb = tlb_init(2); /* Solo 2 entradas */

    tlb_insert(tlb, 1, 10); /* slot 0 */
    tlb_insert(tlb, 2, 20); /* slot 1 */
    tlb_insert(tlb, 3, 30); /* slot 0 (reemplaza VPN 1) */

    /* VPN 1 debe haber sido reemplazada */
    int64_t f = tlb_lookup(tlb, 1);
    if (f != -1) {
        FAIL("VPN 1 deberia haber sido reemplazada por FIFO");
        tlb_free(tlb);
        return;
    }

    /* VPN 3 debe estar */
    f = tlb_lookup(tlb, 3);
    if (f != 30) {
        FAIL("VPN 3 deberia estar con frame 30");
        tlb_free(tlb);
        return;
    }

    /* VPN 2 debe seguir */
    f = tlb_lookup(tlb, 2);
    if (f != 20) {
        FAIL("VPN 2 deberia seguir con frame 20");
        tlb_free(tlb);
        return;
    }

    tlb_free(tlb);
    PASS();
}

static void test_dirty_eviction(void)
{
    TEST("BONUS: Dirty eviction incrementa dirty_evictions");
    int num_frames = 4;
    frame_allocator *fa = fa_create(num_frames, 0);

    page_table *pt = page_table_create(64);
    tlb_t *tlb = tlb_init(16);

    page_table *pts[1] = { pt };
    tlb_t *tlbs[1] = { tlb };
    fa_set_globals(fa, tlbs, pts, 1);

    /* Llenar todos los frames: VPN 0-3 */
    for (int i = 0; i < num_frames; i++) {
        int f = fa_allocate(fa, 0, (uint64_t)i);
        pt->entries[i].frame_number = (uint64_t)f;
        pt->entries[i].valid = 1;
        tlb_insert(tlb, (uint64_t)i, (uint64_t)f);
    }

    /* Marcar VPN 0 como dirty (escritura) */
    pt->entries[0].dirty = 1;
    /* VPN 1 queda limpia (dirty=0) */

    /* Evictar VPN 0 (dirty) al allocar VPN 10 */
    fa_allocate(fa, 0, 10);
    if (fa->dirty_evictions != 1) {
        FAIL("dirty_evictions deberia ser 1 (VPN 0 era dirty)");
        page_table_free(pt);
        tlb_free(tlb);
        fa_free(fa);
        return;
    }

    /* Evictar VPN 1 (no dirty) al allocar VPN 11 */
    fa_allocate(fa, 0, 11);
    if (fa->dirty_evictions != 1) {
        FAIL("dirty_evictions deberia seguir en 1 (VPN 1 no era dirty)");
        page_table_free(pt);
        tlb_free(tlb);
        fa_free(fa);
        return;
    }

    page_table_free(pt);
    tlb_free(tlb);
    fa_free(fa);
    PASS();
}

int main(void)
{
    printf("=== Tests Paginacion ===\n");
    test_page_fault_primer_acceso();
    test_tlb_hit_segundo_acceso();
    test_tlb_miss();
    test_eviction_frames_llenos();
    test_pa_correcta();
    test_tlb_fifo_reemplazo();
    test_dirty_eviction();
    printf("\nResultado: %d/%d tests pasaron\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
