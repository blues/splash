// Copyright 2024 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include "app.h"
#include "sai.h"
#include <math.h>

// PCM buffer
#if USE_SIMPLE_DECIMATION
int16_t pcm_buffer[BLOCK_SIZE * 8 / AUDIO_IN_DECIMATOR_FACTOR];
#else
int16_t pcm_buffer[BLOCK_SIZE / (DEC_CIC_FACTOR * DEC_OUT_FACTOR)];
#endif

// Define buffer sizes for processing half a buffer
double lastSpl = 0;

// Errors
uint32_t saiErrorCount = 0;

// Forwards
bool processAudio(void);
double compute_spl(int16_t *pcm_data, int num_samples);

// Process one chunk of PDM data
void processPDMData(uint8_t *pdm_data, uint32_t pdm_size)
{

    // Exit if incorrect pdm_size
    if (pdm_size != BLOCK_SIZE) {
        return;
    }

    // Convert the PDM data tp PCM data
    // To convert the PDM data captured from the microphone to PCM data with an 8 kHz sample rate,
    // 1.	Decimation:
    //      Reduce the high-frequency PDM signal to a lower-frequency PCM signal.
    //      This involves summing groups of bits and downsampling the data.
    // 2.	Low-Pass Filtering:
    //      Remove high-frequency noise introduced by the PDM encoding.
#if USE_SIMPLE_DECIMATION
    simple_pdm2pcm(pdm_data, pcm_buffer, pdm_size);
#else
    pdm2pcm(pdm_data, pcm_buffer, pdm_size);
#endif

    // Remember it
    uint32_t pcm_entries = sizeof(pcm_buffer) / sizeof(pcm_buffer[0]);
    lastSpl = compute_spl(pcm_buffer, pcm_entries);

}

// Compute the spl
double compute_spl(int16_t *pcm_data, int num_samples)
{
    int64_t sum = 0;

    // Compute the sum of squared PCM values
    for (int i = 0; i < num_samples; i++) {
        sum += pcm_data[i] * pcm_data[i];
    }

    // Compute RMS value
    double rms = sqrt((double)sum / num_samples);

    // Compute SPL in dB
    double reference_rms = 1032.0f;
    double spl = 20.0f * log10f(rms / reference_rms) + 26.0f;  // Adjust for -26 dBFS sensitivity

    return spl;
}

// Get the last spl
double audioSpl(void)
{
    return lastSpl;
}


// Audio task
void audioTask(void *params)
{

    // Init task
    taskRegister(TASKID_AUDIO, TASKNAME_AUDIO, TASKLETTER_AUDIO, TASKSTACK_AUDIO);

    // Init pdm2pcm
#if USE_SIMPLE_DECIMATION
    simple_pdm2pcm_init();
#else
    if (pdm2pcm_init(BYTE_LEFT_MSB, PDM_ENDIANNESS_LE, SINC4) != 0) {
        debugBreakpoint();
    }
#endif


    // Init buffer I/O management
    bufferInit();

    // Initialize SAI
    MX_SAI1_Init();

    // Start the first receive
    HAL_SAI_RxCpltCallback(&hsai_BlockA1);

    // Loop, polling
    while (true) {
        if (!processAudio()) {
            taskTake(TASKID_AUDIO, ms1Hour);
        }
    }

}

// Start the next receive
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    uint8_t *pdm_buffer;
    uint32_t pdm_buffer_length;
    bufferGetNextFree(&pdm_buffer, &pdm_buffer_length);
    HAL_SAI_Receive_DMA(hsai, pdm_buffer, pdm_buffer_length);
    taskGiveFromISR(TASKID_AUDIO);
}

// Errors
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
    saiErrorCount++;
    HAL_SAI_RxCpltCallback(hsai);
}

// Poll, processing inbound audio buffers
bool processAudio(void)
{

    // If no audio buffer ready, exit
    uint8_t *buf;
    uint32_t buflen;
    if (!bufferGetNextCompleted(&buf, &buflen)) {
        return false;
    }

    // Process it
    processPDMData(buf, buflen);

    // Done
    bufferFree(buf);
    return true;

}
