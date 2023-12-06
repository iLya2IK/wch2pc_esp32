/* Copyright (c) 2023 Ilya Medvedkov <sggdev.im@gmail.com>

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "wcframe.h"

static const char *TAG = "WC_FRAME";

/* wcFramePool */

wc_frame_pool * wcFramePool_init(int16_t frames_limit, int32_t frames_size_limit) {
    wc_frame_pool * res = malloc(sizeof(wc_frame_pool));
    if (res == NULL) return NULL;

    res->first_frame = NULL;
    res->last_frame = NULL;
    res->frames_cnt = 0;
    res->total_frames_size = 0;
    res->on_erase_cb = NULL;
    res->on_erase_data = res;

    res->cnt_limit = frames_limit;
    res->sz_limit = frames_size_limit;

    res->mux = xSemaphoreCreateMutex();

    return res;
}

bool wcFramePool_lock(wc_frame_pool * pool) {
    if (!pool) return false;
    return (xSemaphoreTake(pool->mux, portMAX_DELAY) == pdTRUE);
}

void wcFramePool_unlock(wc_frame_pool * pool) {
    if (!pool) return;
    xSemaphoreGive(pool->mux);
}

void wcFramePool_push_back(wc_frame_pool * pool, wc_frame * fr) {
    if (!pool) return;
    if (!fr) return;

    if (wcFramePool_lock(pool)) {
        wcFramePool_push_back_nonsafe(pool, fr);

        wcFramePool_unlock(pool);
    }
}

void wcFramePool_push_back_nonsafe(wc_frame_pool * pool, wc_frame * fr) {
    fr->next = NULL;

    pool->frames_cnt++;
    pool->total_frames_size += fr->size;

    if (pool->last_frame) pool->last_frame->next = fr;
    pool->last_frame = fr;
    if (!pool->first_frame) pool->first_frame = fr;

    if ((pool->frames_cnt > pool->cnt_limit) ||
        (pool->total_frames_size > pool->sz_limit)) {
        ESP_LOGE(TAG, "limit overflow. frames: %d/%d, size: %d/%d", pool->frames_cnt, pool->cnt_limit,
                                                                    pool->total_frames_size, pool->sz_limit);
        wcFramePool_erase_front_nonsafe(pool);
    }
}

wc_frame * wcFramePool_pop_front(wc_frame_pool * pool) {
    if (!pool) return NULL;
    wc_frame * fr = NULL;
    if (wcFramePool_lock(pool)) {
        fr = wcFramePool_pop_front_nonsafe(pool);
        wcFramePool_unlock(pool);
    }
    return fr;
}

wc_frame * wcFramePool_pop_front_nonsafe(wc_frame_pool * pool) {
    wc_frame * fr = NULL;
    if (pool->first_frame) {
        fr = pool->first_frame;
        pool->first_frame = fr->next;
        if (!pool->first_frame) pool->last_frame = NULL;
        pool->frames_cnt--;
        pool->total_frames_size -= fr->size;
    }
    return fr;
}

void wcFramePool_erase_front(wc_frame_pool * pool) {
    if (!pool) return;
    wc_frame * fr = wcFramePool_pop_front(pool);
    if (fr) {
        if (pool->on_erase_cb)
            pool->on_erase_cb(pool->on_erase_data, fr);
        wcFrame_free(fr);
    }
}

void wcFramePool_erase_front_nonsafe(wc_frame_pool * pool) {
    if (!pool) return;
    wc_frame * fr = wcFramePool_pop_front_nonsafe(pool);
    if (fr) {
        if (pool->on_erase_cb)
            pool->on_erase_cb(pool->on_erase_data, fr);
        wcFrame_free(fr);
    }
}

void wcFramePool_clear(wc_frame_pool * pool) {
    if (wcFramePool_lock(pool)) {
        while (pool->frames_cnt > 0) {
            wcFramePool_erase_front_nonsafe(pool);
        }
        wcFramePool_unlock(pool);
    }
}

void wcFramePool_free(wc_frame_pool * pool) {
    if (!pool) return;
    wcFramePool_clear(pool);
    if (pool->mux)
      vSemaphoreDelete(pool->mux);
    free(pool);
}

/* wcFrame */

void wcFrame_free(wc_frame * frm) {
    if (!frm) return;
    if (frm->data)
        free(frm->data);

    free(frm);
}

void wcFrame_clear(wc_frame * frm) {
    if (!frm) return;
    frm->pos = 0;
    frm->size = 0;
}

wc_frame * wcFrame_init() {
    return wcFrame_init_cap(INITIAL_FRAME_BUFFER);
}

wc_frame * wcFrame_init_cap(int capacity) {
    wc_frame * fr = malloc(sizeof(wc_frame));
    if (fr == NULL) return NULL;

    fr->size = 0;
    fr->pos = 0;
    fr->cap = capacity;
    fr->data = malloc(fr->cap);
    return fr;
}

void wcFrame_writeData(wc_frame * fr, const void * buf, int32_t sz) {
    if (fr->cap < (fr->size + sz)) {
        fr->cap = ((fr->size + sz) / 0x400 + 1) * 0x400;
        fr->data = realloc(fr->data, fr->cap);
    }
    memcpy(fr->data + fr->pos, buf, sz);
    fr->pos += sz;
    if (fr->size < fr->pos) fr->size = fr->pos;
}

uint8_t wcFrame_readByte(wc_frame * fr) {
    uint8_t res = 0;
    memcpy(&res, fr->data + fr->pos, 1);
    fr->pos++;
    return res;
}

uint16_t wcFrame_readWord(wc_frame * fr) {
    uint16_t res = 0;
    memcpy(&res, fr->data + fr->pos, 2);
    fr->pos += 2;
    return res;
}

uint32_t wcFrame_readUInt32(wc_frame * fr) {
    uint32_t res = 0;
    memcpy(&res, fr->data + fr->pos, 4);
    fr->pos += 4;
    return res;
}

int32_t wcFrame_readBuffer(wc_frame * fr, void * buf, int32_t sz) {
    if (fr->size < (sz + fr->pos)) {
        sz = fr->size - fr->pos;
    }
    if (sz > 0) {
        memcpy(buf, fr->data + fr->pos, sz);
        fr->pos += sz;
    }
    return sz;
}
