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

#ifndef _SLEEPD_SIGNALS_H_
#define _SLEEPD_SIGNALS_H_

/*
 * sleepd's /com/palm/power signals on LuneOS.
 *
 * "suspended" is broadcast BEFORE the suspend attempt ("we are about to
 * try"); it does not mean the device slept, and sleepd withholds it during
 * a streak of refused attempts. Whether anything happened is only known
 * from the "resume" that follows, through its resumetype:
 *
 *   0  kernel          the kernel really suspended and woke up again
 *   1  pwrevent_activity  the attempt was abandoned for an activity
 *   2  abort_suspend   the attempt was refused (wakeup source raced the
 *                      suspend, platform refused, client veto, ...)
 *
 * On a battery-powered device with the screen off sleepd retries refused
 * attempts every few seconds, so 1 and 2 arrive many times a minute and
 * must not trigger any work; only 0 is a real sleep/wake edge.
 */
enum SleepdResumeType {
    SLEEPD_RESUME_KERNEL = 0,
    SLEEPD_RESUME_ACTIVITY = 1,
    SLEEPD_RESUME_ABORT_SUSPEND = 2,
};

/* resumetype of a /com/palm/power/resume payload, or -1 when the payload
 * is not an object carrying an integer "resumetype". */
int sleepdParseResumeType(const char *payload);

/* True only for the resume that follows a real kernel suspend. */
bool sleepdResumeIsKernelWake(int resumetype);

#endif
