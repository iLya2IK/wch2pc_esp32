// Copyright 2022 Medvedkov Ilya
//
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HTTP2_PROTO_CLIENT
#define HTTP2_PROTO_CLIENT

#include <cJSON.h>
#ifdef CONFIG_WC_USE_IO_STREAMS
#include "wcframe.h"
#endif
#include "wcprotocol.h"

// http2 client mode
#define H2PC_MODE_MESSAGING  0x01
#define H2PC_MODE_OUTGOING   0x02
#define H2PC_MODE_INCOMING   0x04

// error codes
#define H2PC_EMPTY_RESPONSE    0x5000
#define H2PC_ERR_NOT_CONNECTED 0x5001
#define H2PC_ERR_PROTOCOL      0x5002
#define H2PC_ERR_INTERNAL      0x5010

// response buffer config
#define H2PC_INITIAL_RESP_BUFFER CONFIG_H2PC_INITIAL_RESP_BUFFER
#define H2PC_MAXIMUM_RESP_BUFFER CONFIG_H2PC_MAXIMUM_RESP_BUFFER

#ifdef CONFIG_WC_USE_IO_STREAMS
// incoming frames config
#define H2PC_MAX_ALLOWED_FRAMES      CONFIG_H2PC_MAX_ALLOWED_FRAMES
#define H2PC_MAX_ALLOWED_FRAMES_SIZE CONFIG_H2PC_MAX_ALLOWED_FRAMES_SIZE

// incomig frames defines
#define H2PC_FST_WAITING_START_OF_FRAME 0
#define H2PC_FST_WAITING_DATA 1

// kinds of datastreams
#define H2PC_OUT_STREAM 1
#define H2PC_INC_STREAM 2

#endif

extern const char const UPPER_XDIGITS[];

#ifdef CONFIG_WC_USE_IO_STREAMS
typedef bool (* h2pc_cb_inc_frame_analyse)(void * user_data, wc_frame * frm, int offset);
typedef bool (* h2pc_cb_stream_next_device)(const cJSON * device, const cJSON * dev_name, const cJSON * sub_proto);
#endif
typedef bool (* h2pc_cb_next_msg)(const cJSON * src, const cJSON * kind, const cJSON * params, const cJSON * msg_id);

int  h2pc_initialize(int mode);
void h2pc_reset_buffers();
void h2pc_reset();
void h2pc_finalize();

/* getters */
const char * h2pc_get_sid();
int     h2pc_get_protocol_errors_cnt();
int     h2pc_get_last_error();
bool    h2pc_get_connected();
bool    h2pc_get_is_streaming();
uint8_t h2pc_get_streaming();

/* helpers. common utilities */
void    h2pc_encode_http_str(const char * str, char * dst);
int     h2pc_extract_protocol_error(cJSON * resp);
void    h2pc_msg_set_res(cJSON * msg, int res);

/* messages */
/* outgoing messages */
void h2pc_om_add_msg(const char * amsg, const char * atarget, cJSON * content);
void h2pc_om_add_msg_res(const char * amsg, const char * atarget, cJSON * content, bool ok);
void h2pc_om_add_msg_res_code(const char * amsg, const char * atarget, cJSON * content, int error_code);
bool h2pc_om_locked_waiting();
bool h2pc_om_lock();
cJSON * h2pc_om_get_pool();
void h2pc_om_set_pool(cJSON * data);
void h2pc_om_clr_pool();
void h2pc_om_unlock();
/* incoming messages */
void h2pc_im_proceed(h2pc_cb_next_msg on_next_msg, int limit_cnt);
bool h2pc_im_locked_waiting();
bool h2pc_im_lock();
cJSON * h2pc_im_get_pool();
void h2pc_im_set_pool(cJSON * data);
void h2pc_im_clr_pool();
cJSON * h2pc_im_set_from_response();
void h2pc_im_unlock();

/* sync helpers */
int h2pc_req_authorize_sync(const char * name, const char * pwrd, const char * dev, cJSON * meta, bool is_own_meta);
#ifdef CONFIG_WC_USE_IO_STREAMS
int h2pc_req_get_streams_sync(h2pc_cb_stream_next_device on_next_device);
#endif
int h2pc_req_send_msgs_sync();
int h2pc_req_send_media_record_sync();
int h2pc_req_get_msgs_sync();

/* low-level network methods */
bool h2pc_connect_to_http2(char * aserver);
void h2pc_prepare_to_send(cJSON * tosend);
void h2pc_prepare_to_send_static(char * buf, int size);
void h2pc_do_post(char * aPath);
bool h2pc_wait_for_response();
cJSON * h2pc_consume_response_content();
void h2pc_disconnect_http2();

#ifdef CONFIG_WC_USE_IO_STREAMS
/* incoming streaming */
int  h2pc_is_launch(const char * device_name, wc_frame_pool * inc_pool,
                       h2pc_cb_inc_frame_analyse analyser, void* analyser_data );
void h2pc_is_set_pool(wc_frame_pool * inc_pool, h2pc_cb_inc_frame_analyse analyser, void * user_data);
bool h2pc_is_wait_for_frame();
void h2pc_is_stop();

/* outgoing streaming */
int  h2pc_os_prepare();
void h2pc_os_prepare_frame(char * buf, int size);
bool h2pc_os_wait_for_frame();
#endif


#endif