// Copyright 2023 Medvedkov Ilya
//
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef WC_STR_UTILS_H
#define WC_STR_UTILS_H

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>

bool wcsu_starts_with(const char * str, const char * prefix);
int32_t wcsu_char_pos(const char subs, const char * str);
bool wcsu_try_str_to_int(const char * str, int32_t * v);
void wcsu_to_upper_case(char * str);

#endif