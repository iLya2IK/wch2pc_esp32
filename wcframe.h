// Copyright 2023 Medvedkov Ilya
//
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef WC_FRAME_H
#define WC_FRAME_H

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event_loop.h"

#define INITIAL_FRAME_BUFFER 0x8000

typedef struct wc_frame {
    struct wc_frame * next;
    int32_t size;
    int32_t cap;
    int32_t pos;
    unsigned char * data;
} wc_frame;

typedef void (*wc_frame_erase) (void * data, wc_frame * frm);

typedef struct wc_frame_pool {
    SemaphoreHandle_t mux;

    wc_frame * first_frame;
    wc_frame * last_frame;
    int16_t frames_cnt;
    int32_t total_frames_size;
    wc_frame_erase on_erase_cb;
    void * on_erase_data;

    int16_t cnt_limit;
    int32_t sz_limit;
} wc_frame_pool;

wc_frame_pool * wcFramePool_init(int16_t frames_limit, int32_t frames_size_limit);
bool wcFramePool_lock(wc_frame_pool * pool);
void wcFramePool_unlock(wc_frame_pool * pool);
void wcFramePool_push_back(wc_frame_pool * pool, wc_frame * fr);
void wcFramePool_push_back_nonsafe(wc_frame_pool * pool, wc_frame * fr);
wc_frame * wcFramePool_pop_front(wc_frame_pool * pool);
wc_frame * wcFramePool_pop_front_nonsafe(wc_frame_pool * pool);
void wcFramePool_erase_front(wc_frame_pool * pool);
void wcFramePool_erase_front_nonsafe(wc_frame_pool * pool);
void wcFramePool_clear(wc_frame_pool * pool);
void wcFramePool_free(wc_frame_pool * pool);

wc_frame * wcFrame_init();
wc_frame * wcFrame_init_cap(int capacity);
void wcFrame_writeData(wc_frame * fr, const void * buf, int32_t sz);
uint8_t wcFrame_readByte(wc_frame * fr);
uint16_t wcFrame_readWord(wc_frame * fr);
uint32_t wcFrame_readUInt32(wc_frame * fr);
int32_t wcFrame_readBuffer(wc_frame * fr, void * buf, int32_t sz);
void wcFrame_clear(wc_frame * frm);
void wcFrame_free(wc_frame * frm);

#endif
