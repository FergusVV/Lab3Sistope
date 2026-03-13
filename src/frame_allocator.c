#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "frame_allocator.h"
#include "paginacion.h"
#include "simulator.h"

/* Crear frame allocator con num_frames frames fisicos.
   Inicializa bitmap (libre/ocupado), cola FIFO para eviction,
   y mutex para proteccion en modo SAFE. */
frame_allocator *fa_create(int num_frames, int unsafe)
{
    frame_allocator *fa = malloc(sizeof(frame_allocator));
    if (!fa) return NULL;

    fa->num_frames = num_frames;
    fa->free_count = num_frames;
    fa->unsafe = unsafe;
    fa->evictions = 0;
    fa->dirty_evictions = 0;

    fa->bitmap = calloc(num_frames, sizeof(int));
    if (!fa->bitmap) { free(fa); return NULL; }

    /* Cola FIFO: capacidad = num_frames */
    fa->fifo_capacity = num_frames;
    fa->fifo_queue = calloc(num_frames, sizeof(fifo_entry));
    if (!fa->fifo_queue) { free(fa->bitmap); free(fa); return NULL; }
    fa->fifo_head = 0;
    fa->fifo_tail = 0;
    fa->fifo_count = 0;

    /* Inicializar mutex */
    pthread_mutex_init(&fa->mutex, NULL);

    /* Referencias globales se configuran despues */
    fa->all_tlbs = NULL;
    fa->all_page_tables = NULL;
    fa->num_threads = 0;

    return fa;
}

/* Configurar referencias globales necesarias para eviction:
   punteros a todas las TLBs y tablas de paginas de todos los threads. */
void fa_set_globals(frame_allocator *fa, tlb_t **all_tlbs,
                    page_table **all_page_tables, int num_threads)
{
    fa->all_tlbs = all_tlbs;
    fa->all_page_tables = all_page_tables;
    fa->num_threads = num_threads;
}

/* Buscar frame libre en bitmap */
static int find_free_frame(frame_allocator *fa)
{
    for (int i = 0; i < fa->num_frames; i++) {
        if (fa->bitmap[i] == 0)
            return i;
    }
    return -1;
}

/* Evictar pagina mas antigua usando politica FIFO.
   Pasos: (a) tomar victima del head, (b) writeback si dirty (bonus, 3ms),
   (c) invalidar PT victima, (d) invalidar todas las TLBs, (e) liberar frame. */
static int evict_fifo(frame_allocator *fa)
{
    if (fa->fifo_count == 0)
        return -1; /* No deberia pasar */

    /* Tomar victima del head */
    fifo_entry victim = fa->fifo_queue[fa->fifo_head];
    fa->fifo_head = (fa->fifo_head + 1) % fa->fifo_capacity;
    fa->fifo_count--;

    /* Invalidar tabla de paginas del thread victima */
    if (fa->all_page_tables && fa->all_page_tables[victim.thread_id]) {
        /* BONUS: Si pagina victima estaba dirty, simular writeback a disco */
        if (fa->all_page_tables[victim.thread_id]->entries[victim.vpn].dirty) {
            fa->all_page_tables[victim.thread_id]->entries[victim.vpn].dirty = 0;
            fa->dirty_evictions++;
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 3000000L }; /* 3ms */
            nanosleep(&ts, NULL);
        }
        fa->all_page_tables[victim.thread_id]->entries[victim.vpn].valid = 0;
    }

    /* Invalidar en TODAS las TLBs */
    if (fa->all_tlbs) {
        for (int t = 0; t < fa->num_threads; t++) {
            if (fa->all_tlbs[t])
                tlb_invalidate_by_frame(fa->all_tlbs[t], victim.frame);
        }
    }

    /* Marcar frame libre */
    fa->bitmap[victim.frame] = 0;
    fa->free_count++;
    fa->evictions++;

    return victim.frame;
}

/* Asignar un frame fisico. Si no hay libres, evicta con FIFO.
   Protegido con mutex en modo SAFE (SAFE_LOCK/SAFE_UNLOCK).
   Registra (thread_id, vpn, frame) en cola FIFO para futura eviction. */
int fa_allocate(frame_allocator *fa, int thread_id, uint64_t vpn)
{
    int frame;

    SAFE_LOCK(&fa->mutex, fa->unsafe);

    frame = find_free_frame(fa);

    if (frame < 0) {
        /* No hay frames libres, evictar */
        frame = evict_fifo(fa);
    }

    if (frame >= 0) {
        /* Marcar como ocupado */
        fa->bitmap[frame] = 1;
        fa->free_count--;

        /* Agregar al FIFO */
        fa->fifo_queue[fa->fifo_tail].thread_id = thread_id;
        fa->fifo_queue[fa->fifo_tail].vpn = vpn;
        fa->fifo_queue[fa->fifo_tail].frame = frame;
        fa->fifo_tail = (fa->fifo_tail + 1) % fa->fifo_capacity;
        fa->fifo_count++;
    }

    SAFE_UNLOCK(&fa->mutex, fa->unsafe);

    return frame;
}

/* Liberar memoria del frame allocator y destruir mutex */
void fa_free(frame_allocator *fa)
{
    if (fa) {
        pthread_mutex_destroy(&fa->mutex);
        free(fa->bitmap);
        free(fa->fifo_queue);
        free(fa);
    }
}
