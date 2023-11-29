/* HTTP2 helper msgs/frames pools for client

   Part of WCWebCamServer project

   Copyright (c) 2022 Ilya Medvedkov <sggdev.im@gmail.com>

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "wcprotocol.h"
#ifdef CONFIG_WC_USE_IO_STREAMS
#include "wcframe.h"
#endif
#include "lwip/apps/sntp.h"
#include "http2_protoclient.h"
#include "sh2lib.h"
#include "esp_log.h"

static const char * H2PC_TAG = "H2PC";

/* current http2 connection */
static struct sh2lib_handle hd;

volatile int    h2pc_mode = 0;              // current client mode
static   char * h2pc_sid = NULL;            // current session id
volatile int    h2pc_protocol_errors = 0;   // protocol errors count
volatile int    h2pc_err_code = 0;          // last error code
static   char * h2pc_last_stamp = NULL;     // last time stamp from server

/* request data */
volatile bool   bytes_need_to_free = false; // is current raw bytes need to free after request sent
static char *   bytes_tosend = NULL;        // current raw bytes request content
volatile int    bytes_tosend_len = 0;       // current raw bytes request content length
volatile int    bytes_tosend_pos = 0;       // current raw bytes request content pos
volatile bool   request_finished = false;   // is current request finished

#ifdef CONFIG_WC_USE_IO_STREAMS
/* outgoing frames */
static char *   bytes_frame = NULL;         // current raw bytes frame content
volatile int    bytes_frame_len = 0;        // current raw bytes frame content length
volatile int    bytes_frame_pos = 0;        // current raw bytes frame content pos
volatile int    bytes_frame_pos_sended = 0;
volatile int32_t out_streaming_strm_id = -1;
volatile bool   sending_finished = false;
#endif

/* response data */
volatile int    resp_buffer_size = H2PC_INITIAL_RESP_BUFFER;  // current response content buffer size
volatile int    resp_len = 0;               // current response content length
static char * resp_buffer = NULL;           // current response content

#ifdef CONFIG_WC_USE_IO_STREAMS
/* incoming frames data */
static SemaphoreHandle_t inc_frames_mux = NULL;
volatile int    frame_buffer_size = 0;      // current frame content length
volatile int    frame_body_size = 0;
volatile int    frame_state = H2PC_FST_WAITING_START_OF_FRAME;
static wc_frame * frame_buffer;
static wc_frame_pool * inc_frame_pool;
static h2pc_cb_inc_frame_analyse inc_frame_analyser;
static void * inc_frame_analyser_data;
static int32_t   inc_streaming_strm_id = -1;
#endif

/* messages pools */
static SemaphoreHandle_t incoming_msgs_mux = NULL;
static SemaphoreHandle_t outgoing_msgs_mux = NULL;
static cJSON * incoming_msgs = NULL;
static cJSON * outgoing_msgs = NULL;
volatile int  incoming_msgs_pos = 0;         // helpers work with the pool of incoming msgs
volatile int  incoming_msgs_size = 0;

volatile bool client_connected = false;

static const int PATH_LENGTH  = 256;
static const int TOKEN_LENGTH = 128;

const char const UPPER_XDIGITS[] = "0123456789ABCDEF";

/* encode str to percent-string */
void h2pc_encode_http_str(const char * str, char * dst) {
    if (!str) return;
    int p =0;
    for (int i = 0; i < strlen(str); i++) {
        char c = str[i];
        if ( ((c >= 48) && (c <= 57)) ||
             ((c >= 65) && (c <= 90)) ||
             ((c >= 97) && (c <= 122)) ) {
            dst[p++] = c;
            continue;
        }

        dst[p++] = '%';
        dst[p++] = UPPER_XDIGITS[(c >> 4) & 0x0f];
        dst[p++] = UPPER_XDIGITS[(c & 0x0f)];
    }
    dst[p] = 0;
}

/* sync helpers */
int h2pc_extract_protocol_error(cJSON * resp) {
    cJSON * code = cJSON_GetObjectItem(resp, JSON_RPC_CODE);
    if (code != NULL && cJSON_IsNumber(code)) {
        h2pc_err_code = (uint8_t)code->valueint;
    } else
        h2pc_err_code = REST_ERR_UNSPECIFIED;
    return h2pc_err_code;
}

void    h2pc_msg_set_res(cJSON * msg, int res) {
    if (res == REST_RESULT_OK)
        cJSON_AddStringToObject(msg, JSON_RPC_RESULT, JSON_RPC_OK);
    else {
        cJSON_AddStringToObject(msg, JSON_RPC_RESULT, JSON_RPC_BAD);
        if (res != REST_ERR_UNSPECIFIED) {
            cJSON_AddNumberToObject(msg, JSON_RPC_CODE, res);
        }
    }
}

static void __consume_protocol_error(cJSON * resp) {
    h2pc_protocol_errors++; // some server error
    h2pc_extract_protocol_error(resp);
    ESP_LOGE(H2PC_TAG, "protocol error %d (%s)", h2pc_err_code, REST_RESPONSE_ERRORS[h2pc_err_code]);
}

int h2pc_req_authorize_sync(const char * name, const char * pwrd, const char * dev, cJSON * meta, bool is_own_meta) {
    if (h2pc_sid) {
        free(h2pc_sid);
        h2pc_sid = NULL;
    }
    /* HTTP GET SID */
    cJSON * tosend = cJSON_CreateObject();
    cJSON_AddStringToObject(tosend, JSON_RPC_NAME,   name);
    cJSON_AddStringToObject(tosend, JSON_RPC_PASS,   pwrd);
    cJSON_AddStringToObject(tosend, JSON_RPC_DEVICE, dev);

    if (is_own_meta)
        cJSON_AddItemToObject(tosend, JSON_RPC_META, meta);
    else
        cJSON_AddItemReferenceToObject(tosend, JSON_RPC_META, meta);

    h2pc_prepare_to_send(tosend);
    cJSON_Delete(tosend);
    h2pc_do_post(HTTP2_STREAMING_AUTH_PATH);
    h2pc_wait_for_response();

    if (h2pc_get_connected()) {
        /* extract sid */
        int res = ESP_OK;
        cJSON * resp = h2pc_consume_response_content();
        if (resp) {
            cJSON * shash = cJSON_GetObjectItem(resp, JSON_RPC_SHASH);
            if (shash) {
                char * hash = shash->valuestring;
                h2pc_sid = malloc(strlen(hash) + 1);
                h2pc_protocol_errors = 0;
                strcpy(h2pc_sid, hash);
                strcpy(h2pc_last_stamp, REST_SYNC_MSG);
            } else {
                __consume_protocol_error(resp);
                res = H2PC_ERR_PROTOCOL;
            }
            cJSON_Delete(resp);
        }
        return res;
    } else {
        return H2PC_ERR_NOT_CONNECTED;
    }
}

#ifdef CONFIG_WC_USE_IO_STREAMS
int h2pc_req_get_streams_sync(h2pc_cb_stream_next_device on_next_device) {
    if (!h2pc_sid) return ESP_ERR_INVALID_STATE;

    int ret = ESP_OK;

    cJSON * tosend = cJSON_CreateObject();
    cJSON_AddStringToObject(tosend, JSON_RPC_SHASH, h2pc_sid);
    h2pc_prepare_to_send(tosend);
    cJSON_Delete(tosend);
    h2pc_do_post(HTTP2_STREAMING_GETSTREAMS_PATH);
    h2pc_wait_for_response();
    /* extract result */
    cJSON * resp = h2pc_consume_response_content();
    if (resp) {
        cJSON * result = cJSON_GetObjectItem(resp, JSON_RPC_RESULT);
        if (result &&
            (strcmp(result->valuestring, JSON_RPC_OK) == 0)) {
            cJSON* devices = cJSON_DetachItemFromObject(resp, JSON_RPC_DEVICES);

            if (devices) {
                bool next = true;
                while ((next) && (cJSON_GetArraySize(devices) > 0)) {
                    if (on_next_device) {
                        cJSON * item = cJSON_DetachItemFromArray(devices, 0);
                        cJSON * device_name = cJSON_GetObjectItem(item, JSON_RPC_DEVICE);
                        cJSON * subproto = cJSON_GetObjectItem(item, JSON_RPC_SUBPROTO);
                        if (subproto && device_name)
                            next = on_next_device(item, device_name, subproto);
                        cJSON_Delete(item);
                    } else
                        break;
                }

                cJSON_Delete(devices);
            }
        } else {
            __consume_protocol_error(resp);
            ret = H2PC_ERR_PROTOCOL;
        }
        cJSON_Delete(resp);
    }
    return ret;
}
#endif

int h2pc_req_send_msgs_sync() {
    if (!h2pc_sid) return ESP_ERR_INVALID_STATE;
    if ((h2pc_mode & H2PC_MODE_MESSAGING) == 0) return ESP_ERR_INVALID_STATE;

    int ret = ESP_OK;

    cJSON * outgoing_msgs_dub = NULL;
    if (h2pc_om_lock()) {
        cJSON * outgoing_msgs = h2pc_om_get_pool();
        if ((outgoing_msgs) && (cJSON_GetArraySize(outgoing_msgs) > 0)) {
            /* dublicate outgoing data to restore on error */
            outgoing_msgs_dub = cJSON_Duplicate(outgoing_msgs, true);
            h2pc_om_clr_pool();
        }
        //
        h2pc_om_unlock();
    }
    if (outgoing_msgs_dub) {
        cJSON * tosend = cJSON_CreateObject();
        cJSON_AddStringToObject(tosend, JSON_RPC_SHASH, h2pc_sid);
        cJSON_AddItemToObject(tosend, JSON_RPC_MSGS, outgoing_msgs_dub);
        h2pc_prepare_to_send(tosend);

        ESP_LOGI(H2PC_TAG, "sending msgs %d bytes", bytes_tosend_len);

        h2pc_do_post(HTTP2_STREAMING_ADDMSGS_PATH);
        h2pc_wait_for_response();
        /* extract result */
        cJSON * resp  = h2pc_consume_response_content();
        if (resp) {
            cJSON * result = cJSON_GetObjectItem(resp, JSON_RPC_RESULT);
            if (result &&
                (strcmp(result->valuestring, JSON_RPC_OK) == 0)) {
                ret = ESP_OK;
            } else {
                /* restore not-sended data */
                if (h2pc_om_lock()) {
                    cJSON * outgoing_msgs = h2pc_om_get_pool();
                    if (outgoing_msgs) {
                        while (cJSON_GetArraySize(outgoing_msgs_dub) > 0) {
                            cJSON * item = cJSON_DetachItemFromArray(outgoing_msgs_dub, 0);
                            cJSON_AddItemToArray(outgoing_msgs, item);
                        }
                    } else {
                        h2pc_om_set_pool(cJSON_Duplicate(outgoing_msgs_dub, true));
                    }
                    h2pc_om_unlock();
                }
                __consume_protocol_error(resp);
                ret = H2PC_ERR_PROTOCOL;
            }
            cJSON_Delete(resp);
        } else {
            ret = H2PC_ERR_INTERNAL;
        }
        cJSON_Delete(tosend);
    }
    return ret;
}

int h2pc_req_send_media_record_sync(const char * buf, size_t sz) {
    if (!h2pc_sid) return ESP_ERR_INVALID_STATE;
    // prepare path?query string
    int ret = ESP_OK;

    char * aPath = NULL;
    char * aSID = NULL;
    aPath = malloc(PATH_LENGTH);
    if (aPath == NULL) goto error_no_memory;
    aSID    = malloc(TOKEN_LENGTH);
    if (aSID == NULL) goto error_no_memory;
    memset(aPath, 0, PATH_LENGTH);
    memset(aSID, 0, TOKEN_LENGTH);
    h2pc_encode_http_str(h2pc_sid, aSID);

    sprintf(aPath, HTTP2_STREAMING_ADDREC_PATH, aSID);

    h2pc_prepare_to_send_static((char *) buf, sz);
    h2pc_do_post(aPath);
    h2pc_wait_for_response();

    if (h2pc_get_connected()) {
        /* extract result */
        cJSON * resp = h2pc_consume_response_content();
        if (resp) {
            cJSON * result = cJSON_GetObjectItem(resp, JSON_RPC_RESULT);
            if (result &&
                (strcmp(result->valuestring, JSON_RPC_OK) == 0)) {
                ret = ESP_OK;
            } else {
                __consume_protocol_error(resp);
                ret = H2PC_ERR_PROTOCOL;
            }
            cJSON_Delete(resp);
        }
    } else {
        ret = H2PC_ERR_NOT_CONNECTED;
    }
    goto final;
error_no_memory:
    ret = ESP_ERR_NO_MEM;
final:
    if (aSID) free(aSID);
    if (aPath) free(aPath);
    return ret;
}

int h2pc_req_get_msgs_sync() {
    if ((h2pc_mode & H2PC_MODE_MESSAGING) == 0) return ESP_ERR_INVALID_STATE;
    if (!h2pc_sid) return ESP_ERR_INVALID_STATE;
    if (!h2pc_last_stamp) return ESP_ERR_INVALID_STATE;

    cJSON * tosend = cJSON_CreateObject();
    cJSON_AddStringToObject(tosend, JSON_RPC_SHASH, h2pc_sid);
    cJSON_AddStringToObject(tosend, JSON_RPC_STAMP, h2pc_last_stamp);
    h2pc_prepare_to_send(tosend);
    cJSON_Delete(tosend);
    h2pc_do_post(HTTP2_STREAMING_GETMSGS_PATH);
    h2pc_wait_for_response();
    int ret = ESP_OK;
    /* extract result */
    if (h2pc_im_lock()) {
        cJSON * resp = h2pc_consume_response_content();
        incoming_msgs_size = 0;
        incoming_msgs_pos = 0;
        if (resp) {
            cJSON * result = cJSON_GetObjectItem(resp, JSON_RPC_RESULT);
            if (result &&
                (strcmp(result->valuestring, JSON_RPC_OK) == 0)) {
                cJSON* msgs = cJSON_DetachItemFromObject(resp, JSON_RPC_MSGS);
                if (msgs) {
                    incoming_msgs_size = cJSON_GetArraySize(msgs);
                    incoming_msgs_pos = 0;
                    h2pc_im_set_pool(msgs);
                } else
                    ret = H2PC_EMPTY_RESPONSE;
            } else {
                __consume_protocol_error(resp);
                ret = H2PC_ERR_PROTOCOL;
            }
            cJSON_Delete(resp);
        }
        h2pc_im_unlock();
    }
    return ret;
}

void __h2pc_om_add_msg_full(const char * amsg, const char * atarget, cJSON * content, int error_code, bool add_res) {
    if ((h2pc_mode & H2PC_MODE_MESSAGING) == 0) return;

    if (h2pc_om_lock()) {
        cJSON * outgoing_msgs = h2pc_om_get_pool();
        if (outgoing_msgs == NULL) {
            outgoing_msgs = cJSON_CreateArray();
            h2pc_om_set_pool(outgoing_msgs);
        }
        cJSON * msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, JSON_RPC_MSG, amsg);
        if (atarget)
            cJSON_AddStringToObject(msg, JSON_RPC_TARGET, atarget);
        if (content)
            cJSON_AddItemToObject(msg, JSON_RPC_PARAMS, content);

        if (add_res)
            h2pc_msg_set_res(msg, error_code);

        cJSON_AddItemToArray(outgoing_msgs, msg);
        //
        h2pc_om_unlock();
    }
}

void h2pc_om_add_msg(const char * amsg, const char * atarget, cJSON * content) {
    __h2pc_om_add_msg_full(amsg, atarget, content, REST_RESULT_OK, false);
}

void h2pc_om_add_msg_res(const char * amsg, const char * atarget, cJSON * content, bool ok) {
    __h2pc_om_add_msg_full(amsg, atarget, content, ok ? REST_RESULT_OK : REST_ERR_UNSPECIFIED, true);
}

void h2pc_om_add_msg_res_code(const char * amsg, const char * atarget, cJSON * content, int error_code) {
    __h2pc_om_add_msg_full(amsg, atarget, content, error_code, true);
}

void h2pc_im_proceed(h2pc_cb_next_msg on_next_msg, int limit_cnt) {
    if ((h2pc_mode & H2PC_MODE_MESSAGING) == 0) return;

    if (h2pc_im_lock()) {
        cJSON * incoming_msgs = h2pc_im_get_pool();
        if (incoming_msgs && (incoming_msgs_pos < incoming_msgs_size)) {
            int cnt = 0;
            bool next = true;
            while (next) {
                cJSON * msg = cJSON_GetArrayItem(incoming_msgs, incoming_msgs_pos);

                if (msg) {
                    /* proceed message */
                    cJSON * ssrc = cJSON_GetObjectItem(msg,  JSON_RPC_DEVICE);  //who sent
                    cJSON * skind = cJSON_GetObjectItem(msg, JSON_RPC_MSG);     //what sent
                    cJSON * stmp = cJSON_GetObjectItem(msg,  JSON_RPC_STAMP);   //when sent
                    cJSON * spars = cJSON_GetObjectItem(msg, JSON_RPC_PARAMS);  //params
                    if (stmp) strcpy(h2pc_last_stamp, stmp->valuestring);
                    cJSON * smid;
                    if (spars) {
                        smid = cJSON_GetObjectItem(spars, JSON_RPC_MID); //msg id
                    } else smid = NULL;

                    /* check completeness */
                    if (ssrc && skind) {
                        if (on_next_msg)
                            next = on_next_msg(ssrc, skind, spars, smid);
                        cnt++;
                    }
                }

                incoming_msgs_pos++;
                if (incoming_msgs_pos >= incoming_msgs_size) {
                    h2pc_im_clr_pool();
                    break;
                }
                if (cnt > limit_cnt)
                    break;
            }

        }
        h2pc_im_unlock();
    }
}

int h2pc_initialize(int mode) {
    h2pc_mode = mode;

    h2pc_last_stamp = malloc(128);
    if (h2pc_last_stamp == NULL) return ESP_ERR_NO_MEM;
    h2pc_last_stamp[0] = 0;

    /* allocating responsing content */
    resp_buffer = malloc(resp_buffer_size);
    if (resp_buffer == NULL) return ESP_ERR_NO_MEM;

    resp_len = 0;

#ifdef CONFIG_WC_USE_IO_STREAMS
    if (mode & H2PC_MODE_INCOMING) {
        inc_frame_pool = NULL;
        inc_frame_analyser = NULL;
        inc_frame_analyser_data = NULL;
        frame_buffer = wcFrame_init();
        if (frame_buffer == NULL) return ESP_ERR_NO_MEM;
        inc_frames_mux = xSemaphoreCreateMutex();
        if (inc_frames_mux == NULL) return ESP_ERR_NO_MEM;
    } else {
        frame_buffer = NULL;
    }
#endif

    if (mode & H2PC_MODE_MESSAGING) {
        incoming_msgs_mux = xSemaphoreCreateMutex();
        if (incoming_msgs_mux == NULL) return ESP_ERR_NO_MEM;
        outgoing_msgs_mux = xSemaphoreCreateMutex();
        if (outgoing_msgs_mux == NULL) return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

const char * h2pc_get_sid() {
    return h2pc_sid;
}

int h2pc_get_last_error() {
    return h2pc_err_code;
}

int  h2pc_get_protocol_errors_cnt() {
    return h2pc_protocol_errors;
}

bool h2pc_get_connected() {
    return client_connected;
}

#ifdef CONFIG_WC_USE_IO_STREAMS

bool __inc_frames_lock() {
    return (xSemaphoreTake(inc_frames_mux, portMAX_DELAY) == pdTRUE);
}

void __inc_frames_unlock() {
    xSemaphoreGive(inc_frames_mux);
}

void h2pc_clear_incoming_frames() {
    if (__inc_frames_lock()) {
        if (inc_frame_pool)
            wcFramePool_clear(inc_frame_pool);
        __inc_frames_unlock();
    }
}

bool h2pc_get_is_streaming() {
    return (out_streaming_strm_id > 0) || (inc_streaming_strm_id > 0);
}

uint8_t h2pc_get_streaming() {
    uint8_t res = 0;
    if (out_streaming_strm_id > 0) res |= H2PC_OUT_STREAM;
    if (inc_streaming_strm_id > 0) res |= H2PC_INC_STREAM;

    return res;
}

#endif

bool h2pc_im_lock() {
    return (xSemaphoreTake(incoming_msgs_mux, portMAX_DELAY) == pdTRUE);
}

cJSON * h2pc_im_get_pool() {
    return incoming_msgs;
}

void h2pc_im_set_pool(cJSON * data) {
    if (incoming_msgs) cJSON_Delete(incoming_msgs);
    incoming_msgs = data;
}

cJSON * h2pc_im_set_from_response() {
    if (incoming_msgs) cJSON_Delete(incoming_msgs);
    incoming_msgs = h2pc_consume_response_content();
    return incoming_msgs;
}

void h2pc_im_clr_pool() {
    if (incoming_msgs) cJSON_Delete(incoming_msgs);
    incoming_msgs = NULL;
}

void h2pc_im_unlock() {
    xSemaphoreGive(incoming_msgs_mux);
}

bool h2pc_om_lock() {
    return (xSemaphoreTake(outgoing_msgs_mux, portMAX_DELAY) == pdTRUE);
}

cJSON * h2pc_om_get_pool() {
    return outgoing_msgs;
}

void h2pc_om_clr_pool() {
    if (outgoing_msgs) cJSON_Delete(outgoing_msgs);
    outgoing_msgs = NULL;
}

void h2pc_om_set_pool(cJSON * data) {
    if (outgoing_msgs) cJSON_Delete(outgoing_msgs);
    outgoing_msgs = data;
}

void h2pc_om_unlock() {
    xSemaphoreGive(outgoing_msgs_mux);
}

bool h2pc_im_locked_waiting() {
    bool val = true;
    if (xSemaphoreTake(incoming_msgs_mux, portMAX_DELAY) == pdTRUE) {
        if (incoming_msgs)
          val = cJSON_GetArraySize(incoming_msgs) == 0;
        xSemaphoreGive(incoming_msgs_mux);
    }
    return val;
}

bool h2pc_om_locked_waiting() {
    bool val = false;
    if (xSemaphoreTake(outgoing_msgs_mux, portMAX_DELAY) == pdTRUE) {
        val = (outgoing_msgs) && (cJSON_GetArraySize(outgoing_msgs) > 0);
        xSemaphoreGive(outgoing_msgs_mux);
    }
    return val;
}

void h2pc_reset_buffers() {
    resp_len = 0;
    if (bytes_need_to_free && bytes_tosend) {
        free(bytes_tosend);
        bytes_tosend = NULL;
        bytes_need_to_free = false;
    }
#ifdef CONFIG_WC_USE_IO_STREAMS
    if (bytes_frame) {
        free(bytes_frame);
        bytes_frame = NULL;
    }
#endif
}

void h2pc_reset() {
    h2pc_reset_buffers();
    h2pc_protocol_errors = 0;
    h2pc_err_code = 0;
#ifdef CONFIG_WC_USE_IO_STREAMS
    h2pc_is_set_pool(NULL, NULL, NULL);
    frame_buffer_size = 0;
    frame_body_size = 0;
    frame_state = H2PC_FST_WAITING_START_OF_FRAME;
    if (frame_buffer) wcFrame_clear(frame_buffer);
#endif
    if (h2pc_sid) free(h2pc_sid);
    h2pc_sid = NULL;
}

void h2pc_finalize() {
    h2pc_reset();

    if (incoming_msgs) cJSON_Delete(incoming_msgs);
    if (outgoing_msgs) cJSON_Delete(outgoing_msgs);
#ifdef CONFIG_WC_USE_IO_STREAMS
    if (frame_buffer)  wcFrame_free(frame_buffer);
#endif
    if (h2pc_last_stamp) free(h2pc_last_stamp);
    if (incoming_msgs_mux) vSemaphoreDelete(incoming_msgs_mux);
    if (outgoing_msgs_mux) vSemaphoreDelete(outgoing_msgs_mux);

    incoming_msgs = NULL;
    outgoing_msgs = NULL;
#ifdef CONFIG_WC_USE_IO_STREAMS
    frame_buffer = NULL;
#endif
    h2pc_last_stamp = NULL;
    incoming_msgs_mux = NULL;
    outgoing_msgs_mux = NULL;
}

void h2pc_disconnect_http2() {
    if (client_connected) {
        sh2lib_free(&hd);
#ifdef CONFIG_WC_USE_IO_STREAMS
        out_streaming_strm_id = -1;
        inc_streaming_strm_id = -1;
#endif
        client_connected = false;
    }
    h2pc_reset();
}

bool h2pc_connect_to_http2(char * aserver) {
    /* HTTP2: one connection multiple requests. Do the TLS/TCP connection first */
    ESP_LOGI(H2PC_TAG, "Connecting to server: %s", aserver);
    if (sh2lib_connect(&hd, aserver) != 0) {
        ESP_LOGE(H2PC_TAG, "Failed to connect");
        return false;
    }
    ESP_LOGI(H2PC_TAG, "Connection done");

    client_connected = true;
    return true;
}

void h2pc_prepare_to_send(cJSON * tosend) {
    bytes_tosend = cJSON_PrintUnformatted(tosend);
    bytes_tosend_len = strlen(bytes_tosend);
    bytes_tosend_pos = 0;
    bytes_need_to_free = true;
    request_finished = false;
}

void h2pc_prepare_to_send_static(char * buf, int size) {
    bytes_tosend = buf;
    bytes_tosend_len = size;
    bytes_tosend_pos = 0;
    bytes_need_to_free = false;
    request_finished = false;
}

#ifdef CONFIG_WC_USE_IO_STREAMS
void h2pc_os_prepare_frame(char * buf, int size) {
    bytes_frame = buf;
    bytes_frame_len = size + WEBCAM_FRAME_HEADER_SIZE;
    bytes_frame_pos = 0;
    bytes_frame_pos_sended = 0;
    sending_finished = false;
}

void truncateFrameBuffer(int32_t * BP) {
    if (*BP > 0) {
        if ((frame_buffer_size - *BP) > 0)
        {
            frame_buffer_size = frame_buffer_size - *BP;
            memmove(frame_buffer->data, frame_buffer->data + *BP, frame_buffer_size);
        }
        else
            frame_buffer_size = 0;
        *BP = 0;
    }
}

int32_t bufferFreeSize() {
    return H2PC_MAX_ALLOWED_FRAMES_SIZE - frame_buffer_size;
}

void pushFrame(int32_t aStartAt) {
    wc_frame * aFrame = wcFrame_init();
    frame_buffer->pos = aStartAt;

    if (aFrame == NULL) return;

    wcFrame_writeData(aFrame, frame_buffer->data + frame_buffer->pos, frame_body_size + WEBCAM_FRAME_HEADER_SIZE);
    aFrame->pos = 0;

    if (__inc_frames_lock()) {
        if (inc_frame_pool) {
            bool flag = true;
            if (inc_frame_analyser)
                flag = inc_frame_analyser(inc_frame_analyser_data, aFrame, WEBCAM_FRAME_HEADER_SIZE);

            if (flag) {
                wcFramePool_push_back(inc_frame_pool, aFrame);

                ESP_LOGI(H2PC_TAG, "New frame pushed. size %d", aFrame->size);
            } else {
                wcFrame_free(aFrame);
                ESP_LOGE(H2PC_TAG, "Frame is not pushed");
            }
        }
        else
            wcFrame_free(aFrame);
        __inc_frames_unlock();
    }
}

int tryConsumeFrame(const void* Chunk, size_t ChunkSz)
{
    int32_t BP = 0;
    int32_t P;
    uint32_t C;
    uint16_t W;
    int ChunkPos = 0;

    bool proceed = true;
    while (proceed)
    {
        if (bufferFreeSize() == 0)
        {
            ESP_LOGE(H2PC_TAG, "Frame buffer overflow");
            proceed = false;
            break;
        }

        if (ChunkPos < ChunkSz)
        {
            frame_buffer->pos = frame_buffer_size;
            P = ChunkSz - ChunkPos;
            if (P > bufferFreeSize()) P = bufferFreeSize();
            wcFrame_writeData(frame_buffer, ((char*)Chunk + ChunkPos), P);
            ChunkPos += P;
            frame_buffer_size = frame_buffer->pos;
        }

        frame_buffer->pos = BP;
        switch (frame_state) {
            case H2PC_FST_WAITING_START_OF_FRAME:
            {
                frame_body_size = 0;
                if (((int32_t)frame_buffer_size - BP) >= (int32_t)WEBCAM_FRAME_HEADER_SIZE)
                {
                    W = wcFrame_readWord(frame_buffer);
                    if (W == WEBCAM_FRAME_START_SEQ)
                    {
                        C = wcFrame_readUInt32(frame_buffer);
                        if (C > (H2PC_MAX_ALLOWED_FRAMES_SIZE - WEBCAM_FRAME_HEADER_SIZE))
                        {
                            ESP_LOGE(H2PC_TAG, "Frame size is too big");
                            proceed = false;
                        } else {
                            frame_body_size = C;
                            frame_state = H2PC_FST_WAITING_DATA;
                        }
                    } else {
                        ESP_LOGE(H2PC_TAG, "Frame wrong header");
                        proceed = false;
                    }
                } else
                {
                    truncateFrameBuffer(&BP);
                    if (ChunkPos == ChunkSz) proceed = false;
                }
                break;
            }
            case H2PC_FST_WAITING_DATA:
            {
                if (((int32_t)frame_buffer_size - BP) >= (int32_t)(frame_body_size + WEBCAM_FRAME_HEADER_SIZE))
                {
                    pushFrame(BP);
                    BP += frame_body_size + WEBCAM_FRAME_HEADER_SIZE;
                    frame_state = H2PC_FST_WAITING_START_OF_FRAME;
                } else
                {
                    truncateFrameBuffer(&BP);
                    if (ChunkPos == ChunkSz) proceed = false;
                }
                break;
            }
        }
    }
    return ChunkPos;
}

int handle_frame_response(struct sh2lib_handle *handle, int32_t stream_id, const char *data, size_t len, int flags)
{
    if (len) {
        ESP_LOGI(H2PC_TAG, "[get-frame-response] Data frame received. Size %d", len);
        tryConsumeFrame((const void *)data, len);
    }
    if (flags == DATA_RECV_FRAME_COMPLETE) {
        ESP_LOGI(H2PC_TAG, "[get-frame-response] Frame fully received");
    } else
    if ( flags == DATA_RECV_RST_STREAM ) {
        ESP_LOGI(H2PC_TAG, "[get-frame-response] Stream Closed");
        if (stream_id == inc_streaming_strm_id)
            inc_streaming_strm_id = -1;
     } else
    if ( flags == DATA_RECV_GOAWAY ) {
        h2pc_disconnect_http2();
    }
    return 0;
}

#endif

int handle_get_response(struct sh2lib_handle *handle, int32_t stream_id, const char *data, size_t len, int flags)
{
    if (len) {
        ESP_LOGI(H2PC_TAG, "[get-response] %.*s", len, data);
        int new_resp_buffer_size = resp_len + len;
        if (new_resp_buffer_size >= resp_buffer_size) {
            if (new_resp_buffer_size < H2PC_MAXIMUM_RESP_BUFFER) {
                new_resp_buffer_size = (new_resp_buffer_size / 1024 + 1) * 1024;
                if (new_resp_buffer_size > H2PC_MAXIMUM_RESP_BUFFER) {
                    new_resp_buffer_size = H2PC_MAXIMUM_RESP_BUFFER;
                }
                resp_buffer = realloc(resp_buffer, new_resp_buffer_size);
                resp_buffer_size = new_resp_buffer_size;
            } else {
                ESP_LOGI(H2PC_TAG, "[get-response] response buffer overflow");
                return 0;
            }
        }
        memcpy(&(resp_buffer[resp_len]), data, len);
        resp_len += len;
    }
    if (flags == DATA_RECV_FRAME_COMPLETE) {
        ESP_LOGI(H2PC_TAG, "[get-response] Frame fully received");
    } else
    if ( flags == DATA_RECV_RST_STREAM ) {
        ESP_LOGI(H2PC_TAG, "[get-response] Stream Closed");
        if (resp_len == resp_buffer_size) {
            /* not often but may be */
            resp_buffer = realloc(resp_buffer, resp_buffer_size + 1);
        }
        resp_buffer[resp_len] = 0; // terminate string
        request_finished = true;
    } else
    if ( flags == DATA_RECV_GOAWAY ) {
        h2pc_disconnect_http2();
    }
    return 0;
}

int send_post_data(struct sh2lib_handle *handle, int32_t stream_id, char *buf, size_t length, uint32_t *data_flags)
{
    int cur_bytes_tosend_len = bytes_tosend_len - bytes_tosend_pos;
    if (cur_bytes_tosend_len < length) length = cur_bytes_tosend_len;

    if (length > 0) {
        /* dst - buf,
         * src - bytes_tosend at bytes_tosend_pos */
        memcpy(buf, &(bytes_tosend[bytes_tosend_pos]), length);
        ESP_LOGI(H2PC_TAG, "[data-prvd] Sending %d bytes", length);
        bytes_tosend_pos += length;
    }

    if (bytes_tosend_len == bytes_tosend_pos) {
        (*data_flags) |= NGHTTP2_DATA_FLAG_EOF;
    }

    return length;
}

void h2pc_do_post(char * aPath) {
    sh2lib_do_post(&hd, aPath, bytes_tosend_len, send_post_data, handle_get_response);
}

bool h2pc_wait_for_response() {
    bool res = true;
    resp_len = 0;
    while (1) {
        /* Process HTTP2 send/receive */
        if (sh2lib_execute(&hd) < 0) {
            ESP_LOGE(H2PC_TAG, "Error in send/receive");
            h2pc_disconnect_http2();
            res = false;
            break;
        }
        if (request_finished || !h2pc_get_connected())
            break;

        vTaskDelay(2);
    }
    if (bytes_need_to_free) {
        free(bytes_tosend);
        //
        bytes_need_to_free = false;
    }
    bytes_tosend = NULL;
    bytes_tosend_len = 0;
    bytes_tosend_pos = 0;
    return res;
}

cJSON * h2pc_consume_response_content() {
    if (resp_len > 0) {
        cJSON * resp = cJSON_Parse(resp_buffer);
        if (resp) {
            return resp;
        } else return NULL;
    } else return NULL;
}

#ifdef CONFIG_WC_USE_IO_STREAMS

int send_put_data(struct sh2lib_handle *handle, int32_t stream_id, char *buf, size_t length, uint32_t *data_flags)
{
    int cur_bytes_tosend_len = bytes_frame_len - bytes_frame_pos;
    if (cur_bytes_tosend_len < length) length = cur_bytes_tosend_len;

    if ( length > 0 ) {

        /* dst - buf,
         * src - bytes_tosend at bytes_tosend_pos */

        int off;
        if (bytes_frame_pos == 0) {
            *((uint16_t *)buf) = WEBCAM_FRAME_START_SEQ;
            uint32_t sz = bytes_frame_len - WEBCAM_FRAME_HEADER_SIZE;
            *((uint32_t *)&(buf[sizeof(uint16_t)])) = (uint32_t)(sz);

            off = WEBCAM_FRAME_HEADER_SIZE;
            bytes_frame_pos += off;
        } else
            off = 0;

        size_t len = length - off;
        memcpy(&(buf[off]), &(bytes_frame[bytes_frame_pos - WEBCAM_FRAME_HEADER_SIZE]), len);
        ESP_LOGI(H2PC_TAG, "[data-prvd] Sending %d bytes", length);
        bytes_frame_pos += len;
    }

    if (bytes_frame_len == bytes_frame_pos) {
        (*data_flags) |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
        if (length == 0)
            return NGHTTP2_ERR_DEFERRED;
    }

    return length;
}

int handle_response(struct sh2lib_handle *handle, int32_t stream_id, const char *data, size_t len, int flags)
{
    if (flags == DATA_SEND_FRAME_DATA) {
        size_t lenv = *((size_t*) data);
        bytes_frame_pos_sended += lenv;
        if (bytes_frame_pos_sended == bytes_frame_len) {
            sending_finished = true;
        }
    } else
    if (flags == DATA_RECV_FRAME_COMPLETE) {
        ESP_LOGI(H2PC_TAG, "[put-response] Frame fully received");
    } else
    if ( flags == DATA_RECV_RST_STREAM ) {
        ESP_LOGI(H2PC_TAG, "[put-response] Stream Closed");
        sending_finished = true;
        out_streaming_strm_id = -1;
    } else
    if ( flags == DATA_RECV_GOAWAY ) {
        h2pc_disconnect_http2();
    }
    return NGHTTP2_ERR_WOULDBLOCK;
}

int h2pc_os_prepare() {
    int ret = ESP_OK;

    char * aPath = NULL;
    char * aSID = NULL;
    aPath = malloc(PATH_LENGTH);
    if (aPath == NULL) goto error_no_memory;
    aSID    = malloc(TOKEN_LENGTH);
    if (aSID == NULL) goto error_no_memory;
    memset(aPath, 0, PATH_LENGTH);
    memset(aSID, 0, TOKEN_LENGTH);
    h2pc_encode_http_str(h2pc_sid, aSID);

    sprintf(aPath, HTTP2_STREAMING_ADDREC_PATH, aSID);

    out_streaming_strm_id = sh2lib_do_put(&hd, aPath, send_put_data, handle_response);
    ESP_LOGD(H2PC_TAG, "[data-prvd] Streaming stream id = %d", out_streaming_strm_id);

    goto final;

error_no_memory:
    ret = ESP_ERR_NO_MEM;
final:
    if (aPath) free(aPath);
    if (aSID) free(aSID);
    return ret;
}

bool h2pc_is_wait_for_frame() {
    bool res = true;
    int delay = 20;

    while (delay) {
        if (inc_streaming_strm_id > 0)
            nghttp2_session_resume_data(hd.http2_sess, inc_streaming_strm_id);
        else {
            res = false;
            break;
        }

        /* Process HTTP2 send/receive */
        int ret = nghttp2_session_recv(hd.http2_sess);
        if (ret != 0) {
            ESP_LOGE(H2PC_TAG, "[sh2-frame-send] HTTP2 session recv failed %d", ret);
            h2pc_disconnect_http2();
            res = false;
            break;
        }
        ret = nghttp2_session_send(hd.http2_sess);
        if (ret != 0) {
            ESP_LOGE(H2PC_TAG, "[sh2-frame-send] HTTP2 session send failed %d", ret);
            h2pc_disconnect_http2();
            res = false;
            break;
        }

        if ((inc_streaming_strm_id < 0) || (!h2pc_get_connected())) {
            res = false;
            break;
        }

        vTaskDelay(1);
        delay--;
    }
    return res;
}

bool h2pc_os_wait_for_frame() {
    bool res = true;

    while (1) {
        if (out_streaming_strm_id > 0)
            nghttp2_session_resume_data(hd.http2_sess, out_streaming_strm_id);

        /* Process HTTP2 send/receive */
        int ret = nghttp2_session_recv(hd.http2_sess);
        if (ret != 0) {
            ESP_LOGE(H2PC_TAG, "[sh2-frame-send] HTTP2 session recv failed %d", ret);
            h2pc_disconnect_http2();
            res = false;
            break;
        }
        ret = nghttp2_session_send(hd.http2_sess);
        if (ret != 0) {
            ESP_LOGE(H2PC_TAG, "[sh2-frame-send] HTTP2 session send failed %d", ret);
            h2pc_disconnect_http2();
            res = false;
            break;
        }

        if (sending_finished || !h2pc_get_connected())
            break;

        vTaskDelay(2);
    }
    ESP_LOGD(H2PC_TAG, "Frame sended");
    bytes_frame = NULL;
    bytes_frame_len = 0;
    bytes_frame_pos = 0;
    bytes_frame_pos_sended = 0;
    return res;
}

void h2pc_is_set_pool(wc_frame_pool * inc_pool, h2pc_cb_inc_frame_analyse analyser, void * user_data) {
    if (h2pc_mode & H2PC_MODE_INCOMING) {
        if (__inc_frames_lock()) {
            inc_frame_pool = inc_pool;
            inc_frame_analyser = analyser;
            inc_frame_analyser_data = user_data;
            __inc_frames_unlock();
        }
    }
}

int h2pc_is_launch(const char * device_name, wc_frame_pool * inc_pool,
                       h2pc_cb_inc_frame_analyse analyser, void* analyser_data ) {
    if ((h2pc_mode & H2PC_MODE_INCOMING) == 0) return ESP_ERR_INVALID_STATE;
    if (!h2pc_sid) return ESP_ERR_INVALID_STATE;

    int res = ESP_OK;
    if (device_name) {
        h2pc_is_set_pool(inc_pool, analyser, analyser_data);

        char * aPath = NULL;
        char * aSID = NULL;
        char * aDevice = NULL;

        aPath   = malloc(PATH_LENGTH);
        if (aPath == NULL) goto error_no_memory;
        aSID    = malloc(TOKEN_LENGTH);
        if (aSID == NULL) goto error_no_memory;
        aDevice = malloc(TOKEN_LENGTH);
        if (aDevice == NULL) goto error_no_memory;
        memset(aPath, 0, PATH_LENGTH);
        memset(aSID, 0, TOKEN_LENGTH);
        memset(aDevice, 0, TOKEN_LENGTH);
        h2pc_encode_http_str(h2pc_sid, aSID);
        h2pc_encode_http_str(device_name, aDevice);

        sprintf(aPath, HTTP2_STREAMING_INP_PATH, aSID, aDevice);

        inc_streaming_strm_id = sh2lib_do_get(&hd, aPath, handle_frame_response);
        ESP_LOGD(H2PC_TAG, "[data-prvd] Streaming stream id = %d", inc_streaming_strm_id);

        if (inc_streaming_strm_id <= 0)
            res = ESP_ERR_INVALID_RESPONSE;

        goto final;

    error_no_memory:
        res = ESP_ERR_NO_MEM;
    final:
        if (aSID) free(aSID);
        if (aDevice) free(aDevice);
        if (aPath) free(aPath);
    } else
        res = ESP_ERR_INVALID_ARG;

    return res;
}

void h2pc_is_stop() {
    if (inc_streaming_strm_id > 0) {
        if (hd.http2_sess) {
            nghttp2_submit_rst_stream(hd.http2_sess, NGHTTP2_FLAG_NONE, inc_streaming_strm_id, NGHTTP2_REFUSED_STREAM);
        }
    }
}

#endif
