#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h>

#include "simulator.h"
#include "segmentacion.h"
#include "paginacion.h"
#include "tlb.h"
#include "frame_allocator.h"
#include "workloads.h"

/* --- Datos de thread --- */
typedef struct {
    int thread_id;
    unsigned int seed;
    sim_config *config;
    /* Segmentacion */
    segment_table *seg_table;
    uint64_t max_limit; /* max de todos los limites */
    /* Paginacion */
    page_table *pt;
    tlb_t *tlb;
    frame_allocator *fa;
    /* Metricas */
    thread_metrics metrics;
} thread_data;

/* --- Prototipos internos --- */
static void parse_args(int argc, char **argv, sim_config *cfg);   /* Parsear 15 flags CLI con getopt_long */
static void parse_seg_limits(const char *csv, sim_config *cfg);   /* Parsear CSV de limites de segmentos */
static void *thread_worker(void *arg);                            /* Funcion principal de cada thread */
static void run_seg_op(thread_data *td);                          /* Ejecutar una traduccion en modo segmentacion */
static void run_page_op(thread_data *td);                         /* Ejecutar una traduccion en modo paginacion */
static double get_time_ns(void);                                  /* Obtener timestamp en nanosegundos (CLOCK_MONOTONIC) */

/* ================================================================ */
/*                         MAIN                                     */
/* ================================================================ */
/* Punto de entrada: parsea CLI, crea estructuras, lanza threads,
   recolecta metricas y genera output (consola + JSON). */
int main(int argc, char **argv)
{
    sim_config cfg;

    /* Defaults */
    memset(&cfg, 0, sizeof(cfg));
    cfg.threads = DEFAULT_THREADS;
    cfg.ops_per_thread = DEFAULT_OPS;
    strcpy(cfg.workload, "uniform");
    cfg.seed = DEFAULT_SEED;
    cfg.unsafe = 0;
    cfg.stats = 0;
    cfg.segments = DEFAULT_SEGMENTS;
    for (int i = 0; i < MAX_SEGMENTS; i++)
        cfg.seg_limits[i] = DEFAULT_SEG_LIMIT;
    cfg.pages = DEFAULT_PAGES;
    cfg.frames = DEFAULT_FRAMES;
    cfg.page_size = DEFAULT_PAGE_SIZE;
    cfg.tlb_size = DEFAULT_TLB_SIZE;
    strcpy(cfg.tlb_policy, "fifo");
    strcpy(cfg.evict_policy, "fifo");

    parse_args(argc, argv, &cfg);

    /* Inicializar generador aleatorio (Sec 3.2 del enunciado).
       Cada thread usa rand_r con semilla propia, pero srand se
       invoca aqui segun lo indicado en el documento. */
    srand(cfg.seed);

    /* Validar mode obligatorio */
    if (cfg.mode[0] == '\0') {
        fprintf(stderr, "Error: --mode es obligatorio (seg|page)\n");
        return 1;
    }

    int is_seg = (strcmp(cfg.mode, "seg") == 0);
    int is_page = (strcmp(cfg.mode, "page") == 0);
    if (!is_seg && !is_page) {
        fprintf(stderr, "Error: mode debe ser 'seg' o 'page'\n");
        return 1;
    }

    /* Crear directorio out/ si no existe */
    mkdir("out", 0755);

    /* --- Preparar estructuras globales --- */
    frame_allocator *fa = NULL;
    tlb_t **all_tlbs = NULL;
    page_table **all_page_tables = NULL;

    if (is_page) {
        fa = fa_create(cfg.frames, cfg.unsafe);
        all_tlbs = calloc(cfg.threads, sizeof(tlb_t *));
        all_page_tables = calloc(cfg.threads, sizeof(page_table *));
    }

    /* --- Crear datos por thread --- */
    thread_data *tdata = calloc(cfg.threads, sizeof(thread_data));
    pthread_t *threads = calloc(cfg.threads, sizeof(pthread_t));

    /* Calcular max_limit para segmentacion */
    uint64_t max_limit = 0;
    if (is_seg) {
        for (int i = 0; i < cfg.segments; i++) {
            if (cfg.seg_limits[i] > max_limit)
                max_limit = cfg.seg_limits[i];
        }
    }

    for (int t = 0; t < cfg.threads; t++) {
        tdata[t].thread_id = t;
        tdata[t].seed = (unsigned int)(cfg.seed + t);
        tdata[t].config = &cfg;
        memset(&tdata[t].metrics, 0, sizeof(thread_metrics));

        if (is_seg) {
            tdata[t].seg_table = seg_table_create(cfg.segments,
                                                   cfg.seg_limits, t);
            tdata[t].max_limit = max_limit;
            tdata[t].pt = NULL;
            tdata[t].tlb = NULL;
            tdata[t].fa = NULL;
        } else {
            tdata[t].seg_table = NULL;
            tdata[t].pt = page_table_create(cfg.pages);
            tdata[t].tlb = (cfg.tlb_size > 0) ? tlb_init(cfg.tlb_size) : NULL;
            tdata[t].fa = fa;
            tdata[t].max_limit = 0;

            all_page_tables[t] = tdata[t].pt;
            all_tlbs[t] = tdata[t].tlb;
        }
    }

    /* Configurar referencias globales en frame allocator */
    if (is_page && fa) {
        fa_set_globals(fa, all_tlbs, all_page_tables, cfg.threads);
    }

    /* --- Medir tiempo total y lanzar threads --- */
    double start_time = get_time_ns();

    for (int t = 0; t < cfg.threads; t++) {
        pthread_create(&threads[t], NULL, thread_worker, &tdata[t]);
    }

    for (int t = 0; t < cfg.threads; t++) {
        pthread_join(threads[t], NULL);
    }

    double end_time = get_time_ns();
    double runtime_sec = (end_time - start_time) / 1e9;

    /* --- Agregar metricas --- */
    thread_metrics *per_thread = calloc(cfg.threads, sizeof(thread_metrics));
    for (int t = 0; t < cfg.threads; t++) {
        per_thread[t] = tdata[t].metrics;
    }

    uint64_t evictions = (fa) ? fa->evictions : 0;
    uint64_t dirty_evictions = (fa) ? fa->dirty_evictions : 0;

    /* --- Output --- */
    if (is_seg) {
        if (cfg.stats)
            print_stats_seg(&cfg, per_thread, cfg.threads, runtime_sec);
        write_json_seg(&cfg, per_thread, cfg.threads, runtime_sec);
    } else {
        if (cfg.stats)
            print_stats_page(&cfg, per_thread, cfg.threads, evictions,
                            dirty_evictions, runtime_sec);
        write_json_page(&cfg, per_thread, cfg.threads, evictions,
                       dirty_evictions, runtime_sec);
    }

    /* --- Liberar memoria --- */
    for (int t = 0; t < cfg.threads; t++) {
        if (tdata[t].seg_table) seg_table_free(tdata[t].seg_table);
        if (tdata[t].pt) page_table_free(tdata[t].pt);
        if (tdata[t].tlb) tlb_free(tdata[t].tlb);
    }
    if (fa) fa_free(fa);
    free(all_tlbs);
    free(all_page_tables);
    free(tdata);
    free(threads);
    free(per_thread);

    return 0;
}

/* ================================================================ */
/*                    THREAD WORKER                                 */
/* ================================================================ */
/* Funcion ejecutada por cada pthread. Realiza ops_per_thread
   traducciones de direcciones virtuales, midiendo el tiempo
   de cada operacion para calcular metricas. */
static void *thread_worker(void *arg)
{
    thread_data *td = (thread_data *)arg;
    sim_config *cfg = td->config;
    int is_seg = (strcmp(cfg->mode, "seg") == 0);

    for (int op = 0; op < cfg->ops_per_thread; op++) {
        double t0 = get_time_ns();

        if (is_seg)
            run_seg_op(td);
        else
            run_page_op(td);

        double t1 = get_time_ns();
        td->metrics.total_time_ns += (t1 - t0);
        td->metrics.total_ops++;
    }

    return NULL;
}

/* --- Operacion de segmentacion ---
   Genera VA=(seg_id, offset) segun workload, traduce con PA=base+offset.
   Registra translations_ok o segfault segun resultado. */
static void run_seg_op(thread_data *td)
{
    sim_config *cfg = td->config;
    int seg_id;
    uint64_t offset;

    if (strcmp(cfg->workload, "80-20") == 0)
        gen_va_seg_8020(&td->seed, cfg->segments, td->max_limit,
                        &seg_id, &offset);
    else
        gen_va_seg_uniform(&td->seed, cfg->segments, td->max_limit,
                           &seg_id, &offset);

    int64_t pa = translate_segment(td->seg_table, seg_id, offset);
    if (pa >= 0)
        td->metrics.translations_ok++;
    else
        td->metrics.segfaults++;
}

/* --- Operacion de paginacion ---
   Flujo: 1) consulta TLB, 2) si miss consulta PT, 3) si invalida
   genera page fault (asigna frame + nanosleep 1-5ms), 4) actualiza
   TLB y PT, 5) marca dirty con 50% probabilidad (bonus). */
static void run_page_op(thread_data *td)
{
    sim_config *cfg = td->config;
    int vpn, offset;

    if (strcmp(cfg->workload, "80-20") == 0)
        gen_va_page_8020(&td->seed, cfg->pages, cfg->page_size,
                         &vpn, &offset);
    else
        gen_va_page_uniform(&td->seed, cfg->pages, cfg->page_size,
                            &vpn, &offset);

    int64_t frame = -1;

    /* Paso 1: Consultar TLB si esta habilitada */
    if (cfg->tlb_size > 0 && td->tlb) {
        frame = tlb_lookup(td->tlb, (uint64_t)vpn);
    }

    if (frame >= 0) {
        /* TLB HIT */
        td->metrics.tlb_hits++;
    } else {
        /* TLB MISS */
        td->metrics.tlb_misses++;

        /* Consultar tabla de paginas */
        if (td->pt->entries[vpn].valid) {
            frame = (int64_t)td->pt->entries[vpn].frame_number;
            /* Actualizar TLB */
            if (cfg->tlb_size > 0 && td->tlb)
                tlb_insert(td->tlb, (uint64_t)vpn, (uint64_t)frame);
        } else {
            /* PAGE FAULT */
            td->metrics.page_faults++;

            int allocated = fa_allocate(td->fa, td->thread_id,
                                        (uint64_t)vpn);
            frame = (int64_t)allocated;

            /* Actualizar tabla de paginas ANTES del nanosleep
               para evitar race condition: otro thread podria evictar
               este frame durante el sleep si la PT no esta actualizada */
            td->pt->entries[vpn].frame_number = (uint64_t)frame;
            td->pt->entries[vpn].valid = 1;

            /* Actualizar TLB */
            if (cfg->tlb_size > 0 && td->tlb)
                tlb_insert(td->tlb, (uint64_t)vpn, (uint64_t)frame);

            /* Simular delay de carga desde disco: 1-5 ms */
            int delay_ms = (rand_r(&td->seed) % 5) + 1;
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = delay_ms * 1000000L;
            nanosleep(&ts, NULL);
        }
    }

    /* PA = frame * page_size + offset (calculado pero no usado) */

    /* BONUS: 50% reads / 50% writes - marcar dirty si es write */
    if (rand_r(&td->seed) % 2 == 0) {
        td->pt->entries[vpn].dirty = 1;
    }

    td->metrics.translations_ok++;
}

/* ================================================================ */
/*                    OUTPUT: CONSOLA                               */
/* ================================================================ */
/* Imprime reporte de segmentacion en consola (solo con --stats).
   Muestra configuracion, metricas globales, metricas por thread,
   tiempo total y throughput. Formato segun Sec 6.4 del enunciado. */
void print_stats_seg(const sim_config *cfg, const thread_metrics *per_thread,
                     int num_threads, double runtime_sec)
{
    /* Agregar metricas globales */
    uint64_t total_ok = 0, total_sf = 0;
    double total_time_ns = 0;
    uint64_t total_ops = 0;

    for (int t = 0; t < num_threads; t++) {
        total_ok += per_thread[t].translations_ok;
        total_sf += per_thread[t].segfaults;
        total_time_ns += per_thread[t].total_time_ns;
        total_ops += per_thread[t].total_ops;
    }

    double avg_ns = (total_ops > 0) ? (total_time_ns / total_ops) : 0;
    double throughput = (runtime_sec > 0) ? (total_ops / runtime_sec) : 0;

    printf("========================================\n");
    printf("  SIMULADOR DE MEMORIA VIRTUAL\n");
    printf("  Modo: SEGMENTACI\xc3\x93N\n");
    printf("========================================\n");
    printf("Configuraci\xc3\xb3n:\n");
    printf("  Threads: %d\n", cfg->threads);
    printf("  Ops por thread: %d\n", cfg->ops_per_thread);
    printf("  Workload: %s\n", cfg->workload);
    printf("  Seed: %u\n", cfg->seed);
    printf("  Segmentos: %d\n", cfg->segments);
    printf("  L\xc3\xadmites: ");
    for (int i = 0; i < cfg->segments; i++) {
        if (i > 0) printf(",");
        printf("%lu", (unsigned long)cfg->seg_limits[i]);
    }
    printf("\n\n");
    printf("M\xc3\xa9tricas Globales:\n");
    printf("  Traducciones exitosas: %lu\n", (unsigned long)total_ok);
    printf("  Segfaults: %lu\n", (unsigned long)total_sf);
    printf("  Tiempo promedio traducci\xc3\xb3n: %.0f ns\n", avg_ns);
    printf("  Throughput: %.0f ops/seg\n\n", throughput);
    printf("M\xc3\xa9tricas por Thread:\n");
    for (int t = 0; t < num_threads; t++) {
        printf("  Thread %d: translations_ok=%lu, segfaults=%lu\n",
               t, (unsigned long)per_thread[t].translations_ok,
               (unsigned long)per_thread[t].segfaults);
    }
    printf("\nTiempo total: %.2f segundos\n", runtime_sec);
    printf("Throughput: %.0f ops/seg\n", throughput);
    printf("========================================\n");
}

/* Imprime reporte de paginacion en consola (solo con --stats).
   Incluye TLB hits/misses, hit rate, page faults, evictions. */
void print_stats_page(const sim_config *cfg, const thread_metrics *per_thread,
                      int num_threads, uint64_t evictions,
                      uint64_t dirty_evictions, double runtime_sec)
{
    (void)dirty_evictions; /* Solo se reporta en JSON (bonus) */
    uint64_t total_hits = 0, total_misses = 0, total_pf = 0;
    double total_time_ns = 0;
    uint64_t total_ops = 0;

    for (int t = 0; t < num_threads; t++) {
        total_hits += per_thread[t].tlb_hits;
        total_misses += per_thread[t].tlb_misses;
        total_pf += per_thread[t].page_faults;
        total_time_ns += per_thread[t].total_time_ns;
        total_ops += per_thread[t].total_ops;
    }

    double hit_rate = 0;
    if (total_hits + total_misses > 0)
        hit_rate = (double)total_hits / (total_hits + total_misses) * 100.0;

    double avg_ns = (total_ops > 0) ? (total_time_ns / total_ops) : 0;
    double throughput = (runtime_sec > 0) ? (total_ops / runtime_sec) : 0;

    printf("========================================\n");
    printf("  SIMULADOR DE MEMORIA VIRTUAL\n");
    printf("  Modo: PAGINACI\xc3\x93N\n");
    printf("========================================\n");
    printf("Configuraci\xc3\xb3n:\n");
    printf("  Threads: %d\n", cfg->threads);
    printf("  Ops por thread: %d\n", cfg->ops_per_thread);
    printf("  Workload: %s\n", cfg->workload);
    printf("  Seed: %u\n", cfg->seed);
    printf("  P\xc3\xa1ginas: %d\n", cfg->pages);
    printf("  Frames: %d\n", cfg->frames);
    printf("  Page size: %d\n", cfg->page_size);
    printf("  TLB size: %d\n", cfg->tlb_size);
    printf("  TLB policy: %s\n", cfg->tlb_policy);
    printf("  Evict policy: %s\n\n", cfg->evict_policy);
    printf("M\xc3\xa9tricas Globales:\n");
    printf("  TLB hits: %lu\n", (unsigned long)total_hits);
    printf("  TLB misses: %lu\n", (unsigned long)total_misses);
    printf("  Hit rate: %.2f%%\n", hit_rate);
    printf("  Page faults: %lu\n", (unsigned long)total_pf);
    printf("  Evictions: %lu\n", (unsigned long)evictions);
    printf("  Tiempo promedio traducci\xc3\xb3n: %.0f ns\n", avg_ns);
    printf("  Throughput: %.0f ops/seg\n\n", throughput);
    printf("M\xc3\xa9tricas por Thread:\n");
    for (int t = 0; t < num_threads; t++) {
        printf("  Thread %d: tlb_hits=%lu, tlb_misses=%lu, page_faults=%lu\n",
               t, (unsigned long)per_thread[t].tlb_hits,
               (unsigned long)per_thread[t].tlb_misses,
               (unsigned long)per_thread[t].page_faults);
    }
    printf("\nTiempo total: %.2f segundos\n", runtime_sec);
    printf("Throughput: %.0f ops/seg\n", throughput);
    printf("========================================\n");
}

/* ================================================================ */
/*                    OUTPUT: JSON                                  */
/* ================================================================ */
/* Genera out/summary.json para modo segmentacion.
   Se ejecuta siempre (no depende de --stats).
   Formato segun Sec 6.5 del enunciado. */
void write_json_seg(const sim_config *cfg, const thread_metrics *per_thread,
                    int num_threads, double runtime_sec)
{
    uint64_t total_ok = 0, total_sf = 0;
    double total_time_ns = 0;
    uint64_t total_ops = 0;

    for (int t = 0; t < num_threads; t++) {
        total_ok += per_thread[t].translations_ok;
        total_sf += per_thread[t].segfaults;
        total_time_ns += per_thread[t].total_time_ns;
        total_ops += per_thread[t].total_ops;
    }

    double avg_ns = (total_ops > 0) ? (total_time_ns / total_ops) : 0;
    double throughput = (runtime_sec > 0) ? (total_ops / runtime_sec) : 0;

    mkdir("out", 0755);
    FILE *f = fopen("out/summary.json", "w");
    if (!f) {
        fprintf(stderr, "Error: no se pudo crear out/summary.json\n");
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"mode\": \"seg\",\n");
    fprintf(f, "  \"config\": {\n");
    fprintf(f, "    \"threads\": %d,\n", cfg->threads);
    fprintf(f, "    \"ops_per_thread\": %d,\n", cfg->ops_per_thread);
    fprintf(f, "    \"workload\": \"%s\",\n", cfg->workload);
    fprintf(f, "    \"seed\": %u,\n", cfg->seed);
    fprintf(f, "    \"unsafe\": %s,\n", cfg->unsafe ? "true" : "false");
    fprintf(f, "    \"segments\": %d,\n", cfg->segments);
    fprintf(f, "    \"seg_limits\": [");
    for (int i = 0; i < cfg->segments; i++) {
        if (i > 0) fprintf(f, ", ");
        fprintf(f, "%lu", (unsigned long)cfg->seg_limits[i]);
    }
    fprintf(f, "]\n");
    fprintf(f, "  },\n");
    fprintf(f, "  \"metrics\": {\n");
    fprintf(f, "    \"translations_ok\": %lu,\n", (unsigned long)total_ok);
    fprintf(f, "    \"segfaults\": %lu,\n", (unsigned long)total_sf);
    fprintf(f, "    \"avg_translation_time_ns\": %.0f,\n", avg_ns);
    fprintf(f, "    \"throughput_ops_sec\": %.2f\n", throughput);
    fprintf(f, "  },\n");
    fprintf(f, "  \"runtime_sec\": %.3f\n", runtime_sec);
    fprintf(f, "}\n");

    fclose(f);
}

/* Genera out/summary.json para modo paginacion.
   Incluye dirty_evictions como campo adicional (bonus). */
void write_json_page(const sim_config *cfg, const thread_metrics *per_thread,
                     int num_threads, uint64_t evictions,
                     uint64_t dirty_evictions, double runtime_sec)
{
    uint64_t total_hits = 0, total_misses = 0, total_pf = 0;
    double total_time_ns = 0;
    uint64_t total_ops = 0;

    for (int t = 0; t < num_threads; t++) {
        total_hits += per_thread[t].tlb_hits;
        total_misses += per_thread[t].tlb_misses;
        total_pf += per_thread[t].page_faults;
        total_time_ns += per_thread[t].total_time_ns;
        total_ops += per_thread[t].total_ops;
    }

    double hit_rate = 0;
    if (total_hits + total_misses > 0)
        hit_rate = (double)total_hits / (total_hits + total_misses);

    double avg_ns = (total_ops > 0) ? (total_time_ns / total_ops) : 0;
    double throughput = (runtime_sec > 0) ? (total_ops / runtime_sec) : 0;

    mkdir("out", 0755);
    FILE *f = fopen("out/summary.json", "w");
    if (!f) {
        fprintf(stderr, "Error: no se pudo crear out/summary.json\n");
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"mode\": \"page\",\n");
    fprintf(f, "  \"config\": {\n");
    fprintf(f, "    \"threads\": %d,\n", cfg->threads);
    fprintf(f, "    \"ops_per_thread\": %d,\n", cfg->ops_per_thread);
    fprintf(f, "    \"workload\": \"%s\",\n", cfg->workload);
    fprintf(f, "    \"seed\": %u,\n", cfg->seed);
    fprintf(f, "    \"unsafe\": %s,\n", cfg->unsafe ? "true" : "false");
    fprintf(f, "    \"pages\": %d,\n", cfg->pages);
    fprintf(f, "    \"frames\": %d,\n", cfg->frames);
    fprintf(f, "    \"page_size\": %d,\n", cfg->page_size);
    fprintf(f, "    \"tlb_size\": %d,\n", cfg->tlb_size);
    fprintf(f, "    \"tlb_policy\": \"%s\",\n", cfg->tlb_policy);
    fprintf(f, "    \"evict_policy\": \"%s\"\n", cfg->evict_policy);
    fprintf(f, "  },\n");
    fprintf(f, "  \"metrics\": {\n");
    fprintf(f, "    \"tlb_hits\": %lu,\n", (unsigned long)total_hits);
    fprintf(f, "    \"tlb_misses\": %lu,\n", (unsigned long)total_misses);
    fprintf(f, "    \"hit_rate\": %.3f,\n", hit_rate);
    fprintf(f, "    \"page_faults\": %lu,\n", (unsigned long)total_pf);
    fprintf(f, "    \"evictions\": %lu,\n", (unsigned long)evictions);
    fprintf(f, "    \"dirty_evictions\": %lu,\n", (unsigned long)dirty_evictions);
    fprintf(f, "    \"avg_translation_time_ns\": %.0f,\n", avg_ns);
    fprintf(f, "    \"throughput_ops_sec\": %.2f\n", throughput);
    fprintf(f, "  },\n");
    fprintf(f, "  \"runtime_sec\": %.3f\n", runtime_sec);
    fprintf(f, "}\n");

    fclose(f);
}

/* ================================================================ */
/*                    UTILIDADES                                    */
/* ================================================================ */
/* Retorna el tiempo actual en nanosegundos usando CLOCK_MONOTONIC.
   Se usa para medir tiempos de traduccion y runtime total. */
static double get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

/* ================================================================ */
/*                    PARSEO CLI                                    */
/* ================================================================ */
/* Parsea string CSV de limites de segmentos (ej: "1024,2048,4096").
   Rellena con DEFAULT_SEG_LIMIT si hay menos valores que segmentos. */
static void parse_seg_limits(const char *csv, sim_config *cfg)
{
    char buf[1024];
    strncpy(buf, csv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int idx = 0;
    char *tok = strtok(buf, ",");
    while (tok && idx < MAX_SEGMENTS) {
        cfg->seg_limits[idx] = (uint64_t)atol(tok);
        idx++;
        tok = strtok(NULL, ",");
    }
    /* Si hay menos valores que segmentos, rellenar con DEFAULT_SEG_LIMIT */
    for (int i = idx; i < cfg->segments; i++)
        cfg->seg_limits[i] = DEFAULT_SEG_LIMIT;
}

/* Parsea los 15 flags CLI usando getopt_long.
   Flags: --mode, --threads, --ops-per-thread, --workload, --seed,
   --unsafe, --stats, --segments, --seg-limits, --pages, --frames,
   --page-size, --tlb-size, --tlb-policy, --evict-policy */
static void parse_args(int argc, char **argv, sim_config *cfg)
{
    static struct option long_opts[] = {
        {"mode",          required_argument, 0, 'm'},
        {"threads",       required_argument, 0, 't'},
        {"ops-per-thread",required_argument, 0, 'o'},
        {"workload",      required_argument, 0, 'w'},
        {"seed",          required_argument, 0, 's'},
        {"unsafe",        no_argument,       0, 'u'},
        {"stats",         no_argument,       0, 'S'},
        {"segments",      required_argument, 0, 'g'},
        {"seg-limits",    required_argument, 0, 'l'},
        {"pages",         required_argument, 0, 'p'},
        {"frames",        required_argument, 0, 'f'},
        {"page-size",     required_argument, 0, 'z'},
        {"tlb-size",      required_argument, 0, 'T'},
        {"tlb-policy",    required_argument, 0, 'P'},
        {"evict-policy",  required_argument, 0, 'e'},
        {0, 0, 0, 0}
    };

    int opt;
    int opt_index = 0;
    while ((opt = getopt_long(argc, argv, "", long_opts, &opt_index)) != -1) {
        switch (opt) {
        case 'm':
            strncpy(cfg->mode, optarg, sizeof(cfg->mode) - 1);
            break;
        case 't':
            cfg->threads = atoi(optarg);
            break;
        case 'o':
            cfg->ops_per_thread = atoi(optarg);
            break;
        case 'w':
            strncpy(cfg->workload, optarg, sizeof(cfg->workload) - 1);
            break;
        case 's':
            cfg->seed = (unsigned int)atoi(optarg);
            break;
        case 'u':
            cfg->unsafe = 1;
            break;
        case 'S':
            cfg->stats = 1;
            break;
        case 'g':
            cfg->segments = atoi(optarg);
            break;
        case 'l':
            parse_seg_limits(optarg, cfg);
            break;
        case 'p':
            cfg->pages = atoi(optarg);
            break;
        case 'f':
            cfg->frames = atoi(optarg);
            break;
        case 'z':
            cfg->page_size = atoi(optarg);
            break;
        case 'T':
            cfg->tlb_size = atoi(optarg);
            break;
        case 'P':
            strncpy(cfg->tlb_policy, optarg, sizeof(cfg->tlb_policy) - 1);
            break;
        case 'e':
            strncpy(cfg->evict_policy, optarg, sizeof(cfg->evict_policy) - 1);
            break;
        default:
            break;
        }
    }
}
