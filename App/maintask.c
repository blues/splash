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
#include "sai.h"
#include <math.h>

// For SAI
#define PDM_CLOCK_FREQ 2064000  // 2.064 MHz
#define PCM_SAMPLE_RATE 8000    // 8 kHz
#define OVERSAMPLING_FACTOR (PDM_CLOCK_FREQ / PCM_SAMPLE_RATE)

// Define buffer sizes for processing half a buffer
uint8_t pdm_buffer[4096];
#define PDM_BUFFER_SIZE (sizeof(pdm_buffer)/2)
#define PCM_BUFFER_SIZE (PDM_BUFFER_SIZE * 8 / OVERSAMPLING_FACTOR)
int16_t pcm_buffer[PCM_BUFFER_SIZE];

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

    ////////////////////


    MX_SAI1_Init();

    HAL_SAI_Receive_DMA(&hsai_BlockA1, pdm_buffer, sizeof(pdm_buffer));


    ////////////////////



    // Create the serial request processing task
    xTaskCreate(reqTask, TASKNAME_REQ, STACKWORDS(TASKSTACK_REQ), NULL, TASKPRI_REQ, NULL);

    // Poll, moving serial data from interrupt buffers to app buffers
    for (;;) {
        serialPoll();
    }

}

// Process one chunk of PDM data
void processPDMData(uint8_t *pdm_data, uint32_t pdm_size)
{

    // Convert the PDM data tp PCM data
    //
    // To convert the PDM data captured from the microphone to PCM data with an 8 kHz sample rate, 
	// 1.	Decimation:
	//      Reduce the high-frequency PDM signal (~2 MHz) to a lower-frequency PCM signal (8 kHz).
    //      This involves summing groups of bits and downsampling the data.
	// 2.	Low-Pass Filtering:
    //      Remove high-frequency noise introduced by the PDM encoding.
    //
    // The oversampling factor (OSF) determines how many PDM bits correspond to one PCM sample. For an 8 kHz PCM sample rate:
    //   OSF = PDM Clock Frequency (SCK) / PCM Sample Rate, or
    //   OSF = 2064000 / 8000 == 258
    //
    // So, for each sample,
    // 1. Take a group of 258 PDM bits
    // 2. Sum the bits (1s contribute to the signal amplitude, and 0s do not)
    // 3. Normalize the sum to fit within the desired PCM bit depth (e.g., 16 bits)

    // Decimation
    int16_t *pcm_data = pcm_buffer;
    size_t pcm_index = 0;
    uint32_t accumulator = 0;
    for (size_t i = 0; i < pdm_size * 8; i++) {             // Process each PDM bit
        uint8_t pdm_byte = pdm_data[i / 8];
        uint8_t bit = (pdm_byte >> (7 - (i % 8))) & 0x01;   // Extract each bit
        accumulator += bit;
        // When we've processed enough bits for one PCM sample
        if ((i + 1) % OVERSAMPLING_FACTOR == 0) {
            // Normalize and store the PCM value
            pcm_data[pcm_index++] = (int16_t)((accumulator * 32767) / OVERSAMPLING_FACTOR);
            // Reset accumulator for the next PCM sample
            accumulator = 0;  
        }
    }

    // Use an FIR filter to remove the high-frequency noise from the PCM data
    // FIR low-pass filter coefficients (example for decimation with OSF=258)
#define FIR_TAP_COUNT 32  // Number of taps in the filter
    const float fir_coefficients[FIR_TAP_COUNT] = {
        0.005, 0.010, 0.025, 0.050, 0.100, 0.150, 0.200, 0.250,
        0.250, 0.200, 0.150, 0.100, 0.050, 0.025, 0.010, 0.005,
        0.005, 0.010, 0.025, 0.050, 0.100, 0.150, 0.200, 0.250,
        0.250, 0.200, 0.150, 0.100, 0.050, 0.025, 0.010, 0.005
    };
    int16_t *input = pcm_buffer;
    int16_t *output = input;
    size_t size = pcm_index;
    for (size_t i = 0; i < size; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < FIR_TAP_COUNT; j++) {
            if (i >= j) {
                sum += input[i - j] * fir_coefficients[j];
            }
        }
        output[i] = (int16_t)sum;
    }

    // Compute RMS of the samples
    uint64_t sum_of_squares = 0;  // Use a 64-bit integer to avoid overflow
    for (size_t i = 0; i < size; i++) {
        sum_of_squares += (int32_t)pcm_buffer[i] * (int32_t)pcm_buffer[i];  // Accumulate squares
    }
    float mean_of_squares = (float)sum_of_squares / size;
    float rms = sqrtf(mean_of_squares);

    // Reference RMS for our microphone
    #define REFERENCE_RMS 1032.0f

    // Compute SPL
    float SPL = 20.0f * log10f(rms / REFERENCE_RMS);

    // Remember it
    static uint32_t loudCount = 0;
    static uint32_t sampleCount = 0;
    static float sampleSpl[100];
    sampleSpl[sampleCount % (sizeof(sampleSpl)/sizeof(sampleSpl[0]))] = SPL;
    sampleCount++;
    if (SPL > -19) {
        loudCount++;
    }

}

// SAI data is half completed
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai) {
    bool secondHalf = false;
    uint8_t *buffer = pdm_buffer;
    if (secondHalf) {
        buffer += sizeof(pdm_buffer)/2;
    }
    secondHalf = !secondHalf;
    processPDMData(buffer, sizeof(pdm_buffer) / 2);
}

// SAI data is half completed
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai) {
    processPDMData(&pdm_buffer[sizeof(pdm_buffer)/2], sizeof(pdm_buffer) / 2);
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai) {
    // Handle SAI errors (e.g., FIFO underrun)
    debugf("SAI Error: %ld\n", HAL_SAI_GetError(hsai));
}

