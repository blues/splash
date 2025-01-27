// Copyright 2025 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.


#include "app.h"
#include <stdatomic.h>

typedef enum {
    BUFFER_STATE_FREE,
    BUFFER_STATE_BUSY,
    BUFFER_STATE_COMPLETED
} BufferState;

typedef struct {
    uint8_t data[BUFFER_SIZE];
    atomic_int state; // Use atomic_int for safe concurrent state updates
    atomic_uint sequence_number;
} Buffer;

static Buffer buffers[BUFFER_COUNT];
static atomic_uint next_sequence_number = 0;
static atomic_uint overrunCount = 0;
static atomic_uint getCount = 0;
static atomic_uint freedCount = 0;
static uint32_t completedHwm = 0;
static int64_t lastGetMs = 0;
static int64_t lastProcessMs = 0;
static uint32_t msBetweenGets[10] = {0};
static uint32_t msToProcess[10] = {0};

void bufferInit(void)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        atomic_store(&buffers[i].state, BUFFER_STATE_FREE);
        atomic_store(&buffers[i].sequence_number, 0);
    }
    atomic_store(&next_sequence_number, 0);
    atomic_store(&overrunCount, 0);
}

// Get the next free buffer, marking it as busy.  Note that only one buffer can be
// marked as busy at any given moment in time.
void bufferGetNextFree(uint8_t **buffer, uint32_t *buffer_length)
{
    *buffer_length = BUFFER_SIZE;

    atomic_fetch_add(&getCount, 1);
    int64_t nowMs = timerMs();
    msBetweenGets[atomic_load(&getCount) % sizeof(msBetweenGets)/sizeof(msBetweenGets[0])] = nowMs - lastGetMs;
    lastGetMs = nowMs;

    int completedCount = 0;
    Buffer *current_busy = NULL;
    Buffer *next_busy = NULL;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        switch (buffers[i].state) {
        case BUFFER_STATE_BUSY:
            if (current_busy != NULL) {
                // Should never happen because only 1 busy at a time
                buffers[i].state = BUFFER_STATE_FREE;
            } else {
                current_busy = &buffers[i];
            }
            break;
        case BUFFER_STATE_FREE:
            next_busy = &buffers[i];
            break;
        case BUFFER_STATE_COMPLETED:
            completedCount++;
            break;
        }
    }

    // Maintain stat
    if (completedCount > completedHwm) {
        completedHwm = completedCount;
    }

    // Issue next busy and mark the previous one as completed
    if (next_busy != NULL) {
        if (current_busy != NULL) {
            atomic_store(&current_busy->sequence_number, atomic_fetch_add(&next_sequence_number, 1));
            atomic_store(&current_busy->state, BUFFER_STATE_COMPLETED);
        }
        atomic_store(&next_busy->state, BUFFER_STATE_BUSY);
        *buffer = next_busy->data;
        return;
    }

    // If no next busy, we have overrun
    atomic_fetch_add(&overrunCount, 1);
    if (current_busy != NULL) {
        // The one that was last issued
        *buffer = current_busy->data;
    } else {
        // Should never happen
        atomic_store(&buffers[0].state, BUFFER_STATE_BUSY);
        *buffer = buffers[0].data;
    }
}

// Get the next completed buffer, in FIFO order
bool bufferGetNextCompleted(uint8_t **buffer, uint32_t *buffer_length)
{
    Buffer *lowest = NULL;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (atomic_load(&buffers[i].state) == BUFFER_STATE_COMPLETED) {
            if (lowest == NULL || atomic_load(&buffers[i].sequence_number) < atomic_load(&lowest->sequence_number)) {
                lowest = &buffers[i];
            }
        }
    }
    if (lowest != NULL) {
        *buffer = lowest->data;
        *buffer_length = BUFFER_SIZE;
        lastProcessMs = timerMs();
        return true;
    }
    return false;
}

// Mark the specified buffer as freed and available for the next get()
void bufferFree(uint8_t *buffer)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (buffers[i].data == buffer) {
            atomic_store(&buffers[i].state, BUFFER_STATE_FREE);
            atomic_fetch_add(&freedCount, 1);
            msToProcess[atomic_load(&freedCount) % sizeof(msToProcess)/sizeof(msToProcess[0])] = timerMs() - lastProcessMs;
            break;
        }
    }
}

// Get buffer stats
void bufferStats(uint32_t *gets, uint32_t *frees, uint32_t *overruns, uint32_t *hwm, double *avgGetMs, double *avgProcessMs)
{
    *gets = (uint32_t) atomic_load(&getCount);
    *frees = (uint32_t) atomic_load(&freedCount);
    *overruns = (uint32_t) atomic_load(&overrunCount);
    *hwm = completedHwm;
    uint32_t avg = 0;
    for (int i=0; i<sizeof(msBetweenGets)/sizeof(msBetweenGets[0]); i++) {
        avg += msBetweenGets[i];
    }
    *avgGetMs = (double) avg / (double) sizeof(msBetweenGets)/sizeof(msBetweenGets[0]);
    for (int i=0; i<sizeof(msToProcess)/sizeof(msToProcess[0]); i++) {
        avg += msToProcess[i];
    }
    *avgProcessMs = (double) avg / (double) sizeof(msToProcess)/sizeof(msToProcess[0]);
}
