// Copyright 2024 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

// Optimize SYSCLK parameters for high-speed (80Mhz), so that we can
// process samples quickly.  The fact that HSI is the PLL source and
// that we configure the PLLM at /4 is also required by the
// SAI peripheral clock setup.
#define CLOCK_OPTIMIZE_FOR_HIGH_SPEED


