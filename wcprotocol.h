// Copyright 2023 Medvedkov Ilya
//
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef WC_PROTOCOL_
#define WC_PROTOCOL_

#include <cJSON.h>

/* Dataframes defines */
#define WEBCAM_FRAME_HEADER_SIZE (sizeof(uint16_t) + sizeof(uint32_t))
#define WEBCAM_FRAME_START_SEQ   0xaaaa

/* Commands */
#define HTTP2_STREAMING_AUTH_PATH        "/authorize.json"
#define HTTP2_STREAMING_GETSTREAMS_PATH  "/getStreams.json"
#define HTTP2_STREAMING_GETMSGS_PATH     "/getMsgsAndSync.json"
#define HTTP2_STREAMING_ADDMSGS_PATH     "/addMsgs.json"
#define HTTP2_STREAMING_ADDREC_PATH      "/addRecord.json?shash=%s"
#define HTTP2_STREAMING_INP_PATH         "/output.raw?shash=%s&device=%s"
#define HTTP2_STREAMING_OUT_PATH         "/input.raw?shash=%s"

/* JSON-RPC fields */
#define JSON_RPC_TARGET_BROADCAST        NULL

#define JSON_RPC_OK                      "OK"
#define JSON_RPC_BAD                     "BAD"

#define REST_SYNC_MSG                    "{\"msg\":\"sync\"}"
#define JSON_RPC_SYNC                    "sync"
#define JSON_RPC_MSG                     "msg"
#define JSON_RPC_MSGS                    "msgs"
#define JSON_RPC_DEVICES                 "devices"
#define JSON_RPC_RESULT                  "result"
#define JSON_RPC_CODE                    "code"
#define JSON_RPC_NAME                    "name"
#define JSON_RPC_PASS                    "pass"
#define JSON_RPC_SHASH                   "shash"
#define JSON_RPC_META                    "meta"
#define JSON_RPC_STAMP                   "stamp"
#define JSON_RPC_MID                     "mid"
#define JSON_RPC_DEVICE                  "device"
#define JSON_RPC_TARGET                  "target"
#define JSON_RPC_PARAMS                  "params"
#define JSON_RPC_SUBPROTO                "subproto"

#define REST_RESULT_OK                   0
#define REST_ERR_UNSPECIFIED             1
#define REST_ERR_INTERNAL_UNK            2
#define REST_ERR_DATABASE_FAIL           3
#define REST_ERR_JSON_PARSER_FAIL        4
#define REST_ERR_JSON_FAIL               5
#define REST_ERR_NO_SUCH_SESSION         6
#define REST_ERR_NO_SUCH_USER            7
#define REST_ERR_NO_DEVICES              8
#define REST_ERR_NO_SUCH_RECORD          9
#define REST_ERR_NO_DATA_RETURNED        10
#define REST_ERR_EMPTY_REQUEST           11
#define REST_ERR_MALFORMED_REQUEST       12


extern const char * REST_RESPONSE_ERRORS[];

#endif
