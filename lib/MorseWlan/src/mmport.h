/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define MMPORT_BREAKPOINT() while (1)
#define MMPORT_GET_LR() (__builtin_return_address(0))
#define MMPORT_GET_PC(_a) ((_a) = 0) // TODO
#define MMPORT_MEM_SYNC() __sync_synchronize()