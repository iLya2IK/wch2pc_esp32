// Copyright 2023 Medvedkov Ilya
//
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wcprotocol.h"

const char * REST_RESPONSE_ERRORS[]  = {
                              "NO_ERROR",
                              "UNSPECIFIED",
                              "INTERNAL_UNKNOWN_ERROR",
                              "DATABASE_FAIL",
                              "JSON_PARSER_FAIL",
                              "JSON_FAIL",
                              "NO_SUCH_SESSION",
                              "NO_SUCH_USER",
                              "NO_DEVICES_ONLINE",
                              "NO_SUCH_RECORD",
                              "NO_DATA_RETURNED",
                              "EMPTY_REQUEST",
                              "MALFORMED_REQUEST"};