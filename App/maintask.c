// Copyright 2024 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include "app.h"
#include "task.h"
#include "queue.h"
#include "stm32_lpm.h"
#include "timer_if.h"
#include "rtc.h"
#include "utilities_def.h"

// Main task
void mainTask(void *params)
{

    // Init task
    taskRegister(TASKID_MAIN, TASKNAME_MAIN, TASKLETTER_MAIN, TASKSTACK_MAIN);

    // Init low power manager
    UTIL_LPM_Init();
    UTIL_LPM_SetOffMode((1 << CFG_LPM_APPLI_Id), UTIL_LPM_DISABLE);
    if (osDebugging()) {
        UTIL_LPM_SetStopMode((1 << CFG_LPM_APPLI_Id), UTIL_LPM_DISABLE);
    }

    // Init timer manager
    UTIL_TIMER_Init();

    // Remember the timer value when we booted
    timerSetBootTime();

    // Initialize serial, thus enabling debug output
    serialInit(TASKID_MAIN);

    // Enable debug info at startup, by policy
    MX_DBG_Enable(true);

    // Display usage stats
    debugf("\n");
    debugf("**********************\n");
    char timestr[48];
    uint32_t s = timeSecs();
    timeDateStr(s, timestr, sizeof(timestr));
    if (timeIsValid()) {
        debugf("%s UTC (%d)\n", timestr, s);
    }
    uint32_t pctUsed = ((heapPhysical-heapFreeAtStartup)*100)/65536;
    debugf("RAM: %lu free of %lu (%d%% used)\n", (unsigned long) heapFreeAtStartup, heapPhysical, pctUsed);
    extern void *ROM_CONTENT$$Limit;
    uint32_t imageSize = (uint32_t) (&ROM_CONTENT$$Limit) - FLASH_BASE;
    pctUsed = (imageSize*100)/262144;
    debugf("ROM: %lu free of 256KB (%d%% used)\n", (unsigned long) (262144 - imageSize), pctUsed);
    debugf("**********************\n");

    // Signal that we've started, but do it here so we don't block request processing
    ledRestartSignal();

    // Create the serial request processing task
    xTaskCreate(reqTask, TASKNAME_REQ, STACKWORDS(TASKSTACK_REQ), NULL, TASKPRI_REQ, NULL);

    // Initialize audio, and create the audio processing task
    xTaskCreate(audioTask, TASKNAME_AUDIO, STACKWORDS(TASKSTACK_AUDIO), NULL, TASKPRI_AUDIO, NULL);

    // Poll, moving serial data from interrupt buffers to app buffers
    for (;;) {
        serialPoll();
    }

}
