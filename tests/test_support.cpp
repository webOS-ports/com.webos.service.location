// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
//
// The LS_LOG_* macros in loc_log.h reference the process-wide PmLog context
// that the service defines in Main.cpp; test binaries provide their own.

#include <PmLogLib.h>

PmLogContext gLsLogContext;
