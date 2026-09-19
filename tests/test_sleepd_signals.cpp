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

#include <glib.h>

#include "SleepdSignals.h"

static void test_parse_resume_type(void) {
    g_assert_cmpint(sleepdParseResumeType("{\"resumetype\":0}"), ==, 0);
    g_assert_cmpint(sleepdParseResumeType("{\"resumetype\":1}"), ==, 1);
    g_assert_cmpint(sleepdParseResumeType("{\"resumetype\":2}"), ==, 2);
    g_assert_cmpint(sleepdParseResumeType("{\"resumetype\": 2, \"extra\": true}"), ==, 2);

    /* the addmatch acknowledgement, and junk, carry no resumetype */
    g_assert_cmpint(sleepdParseResumeType("{\"returnValue\":true}"), ==, -1);
    g_assert_cmpint(sleepdParseResumeType("{}"), ==, -1);
    g_assert_cmpint(sleepdParseResumeType("[]"), ==, -1);
    g_assert_cmpint(sleepdParseResumeType("not json"), ==, -1);
    g_assert_cmpint(sleepdParseResumeType(""), ==, -1);
    g_assert_cmpint(sleepdParseResumeType(NULL), ==, -1);
    g_assert_cmpint(sleepdParseResumeType("{\"resumetype\":\"2\"}"), ==, -1);
}

static void test_only_kernel_resume_is_a_wake(void) {
    g_assert_true(sleepdResumeIsKernelWake(SLEEPD_RESUME_KERNEL));
    g_assert_false(sleepdResumeIsKernelWake(SLEEPD_RESUME_ACTIVITY));
    g_assert_false(sleepdResumeIsKernelWake(SLEEPD_RESUME_ABORT_SUSPEND));
    g_assert_false(sleepdResumeIsKernelWake(-1));
    g_assert_false(sleepdResumeIsKernelWake(42));

    /* the exact broadcast a refused attempt produces on LuneOS */
    g_assert_false(sleepdResumeIsKernelWake(sleepdParseResumeType("{\"resumetype\":2}")));
    g_assert_true(sleepdResumeIsKernelWake(sleepdParseResumeType("{\"resumetype\":0}")));
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/sleepd/parse_resume_type", test_parse_resume_type);
    g_test_add_func("/sleepd/only_kernel_resume_is_a_wake", test_only_kernel_resume_is_a_wake);
    return g_test_run();
}
