#include <stdlib.h>
#include "workloads.h"

/* --- Segmentacion --- */

/* Generar VA para segmentacion con distribucion uniforme.
   seg_id aleatorio en [0, num_segments-1], offset en [0, max_limit-1].
   Puede generar segfaults si offset >= limit del segmento seleccionado. */
void gen_va_seg_uniform(unsigned int *seed, int num_segments,
                        uint64_t max_limit, int *seg_id, uint64_t *offset)
{
    *seg_id = rand_r(seed) % num_segments;
    *offset = (uint64_t)(rand_r(seed) % (unsigned int)max_limit);
}

/* Generar VA para segmentacion con principio de localidad 80-20.
   80% de accesos al primer 20% de segmentos (hot), 20% al resto (cold). */
void gen_va_seg_8020(unsigned int *seed, int num_segments,
                     uint64_t max_limit, int *seg_id, uint64_t *offset)
{
    int hot_count = num_segments * 20 / 100;
    if (hot_count < 1) hot_count = 1;

    int r = rand_r(seed) % 100;

    if (r < 80 || hot_count >= num_segments) {
        /* Hot segments: primer 20% */
        *seg_id = rand_r(seed) % hot_count;
    } else {
        /* Cold segments: el 80% restante */
        *seg_id = hot_count + rand_r(seed) % (num_segments - hot_count);
    }

    *offset = (uint64_t)(rand_r(seed) % (unsigned int)max_limit);
}

/* --- Paginacion --- */

/* Generar VA para paginacion con distribucion uniforme.
   VPN aleatorio en [0, num_pages-1], offset en [0, page_size-1]. */
void gen_va_page_uniform(unsigned int *seed, int num_pages,
                         int page_size, int *vpn, int *offset)
{
    *vpn = rand_r(seed) % num_pages;
    *offset = rand_r(seed) % page_size;
}

/* Generar VA para paginacion con principio de localidad 80-20.
   80% de accesos al primer 20% de VPNs (hot), 20% al resto (cold).
   Ideal para demostrar efectividad del TLB con localidad temporal. */
void gen_va_page_8020(unsigned int *seed, int num_pages,
                      int page_size, int *vpn, int *offset)
{
    int hot_count = num_pages * 20 / 100;
    if (hot_count < 1) hot_count = 1;

    int r = rand_r(seed) % 100;

    if (r < 80 || hot_count >= num_pages) {
        *vpn = rand_r(seed) % hot_count;
    } else {
        *vpn = hot_count + rand_r(seed) % (num_pages - hot_count);
    }

    *offset = rand_r(seed) % page_size;
}
