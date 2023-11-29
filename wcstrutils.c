// Copyright 2023 Medvedkov Ilya
//
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wcstrutils.h"

bool wcsu_starts_with(const char * str, const char * prefix) {
    if(strncmp(str, prefix, strlen(prefix)) == 0) return true;
    return false;
}

int32_t wcsu_char_pos(const char subs, const char * str) {
    int32_t p = 0;
    const char * s = str;
    while (*s) {
        if (*s == subs) return p;
        s++;
        p++;
    }
    return -1;
}

bool wcsu_try_str_to_int(const char * str, int32_t * v) {
    *v = atoi(str);
    return (*v > 0);
}

void wcsu_to_upper_case(char * str) {
    char * s = str;
    while (*s) {
        *s = toupper((unsigned char) *s);
        s++;
    }
}
