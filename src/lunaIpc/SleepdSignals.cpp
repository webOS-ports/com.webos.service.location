// Copyright (c) 2026 LuneOS project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <pbnjson.h>

#include "SleepdSignals.h"

int sleepdParseResumeType(const char *payload) {
    int resumetype = -1;

    if (!payload)
        return -1;

    jschema_ref schema = jschema_parse(j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
    if (!schema)
        return -1;

    JSchemaInfo schemaInfo;
    jschema_info_init(&schemaInfo, schema, NULL, NULL);
    jvalue_ref parsed = jdom_parse(j_cstr_to_buffer(payload), DOMOPT_NOOPT, &schemaInfo);
    jschema_release(&schema);

    if (!jis_valid(parsed))
        return -1;

    if (!jis_object(parsed)) {
        j_release(&parsed);
        return -1;
    }

    jvalue_ref value = NULL;
    if (!jobject_get_exists(parsed, J_CSTR_TO_BUF("resumetype"), &value) ||
        !jis_number(value) ||
        jnumber_get_i32(value, &resumetype) != CONV_OK) {
        resumetype = -1;
    }

    j_release(&parsed);
    return resumetype;
}

bool sleepdResumeIsKernelWake(int resumetype) {
    return resumetype == SLEEPD_RESUME_KERNEL;
}
