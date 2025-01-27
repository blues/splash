
#include "app.h"
#include <math.h>
#include "st/pdm2pcm_config.h"

void simple_pdm2pcm_init()
{
}

void simple_pdm2pcm(uint8_t *pdm_data, int16_t *pcm_data, int block_size)
{

    // Decimation
    size_t pcm_index = 0;
    uint32_t accumulator = 0;
    for (size_t i = 0; i < block_size * 8; i++) {             // Process each PDM bit
        uint8_t pdm_byte = pdm_data[i / 8];
        uint8_t bit = (pdm_byte >> (7 - (i % 8))) & 0x01;   // Extract each bit
        accumulator += bit;
        // When we've processed enough bits for one PCM sample
        if ((i + 1) % AUDIO_IN_DECIMATOR_FACTOR == 0) {
            // Normalize and store the PCM value
            pcm_data[pcm_index++] = (int16_t)((accumulator * 32767) / AUDIO_IN_DECIMATOR_FACTOR);
            // Reset accumulator for the next PCM sample
            accumulator = 0;
        }
    }

    // Use an FIR filter to remove the high-frequency noise from the PCM data
    // FIR low-pass filter coefficients (example for decimation with OSF=258)
    // The fir coefficients are the number of taps in the filter.
    const float fir_coefficients[32] = {
        0.005, 0.010, 0.025, 0.050, 0.100, 0.150, 0.200, 0.250,
        0.250, 0.200, 0.150, 0.100, 0.050, 0.025, 0.010, 0.005,
        0.005, 0.010, 0.025, 0.050, 0.100, 0.150, 0.200, 0.250,
        0.250, 0.200, 0.150, 0.100, 0.050, 0.025, 0.010, 0.005
    };
    int16_t *input = pcm_data;
    int16_t *output = input;
    size_t size = pcm_index;
    for (size_t i = 0; i < size; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < sizeof(fir_coefficients)/sizeof(fir_coefficients[0]); j++) {
            if (i >= j) {
                sum += input[i - j] * fir_coefficients[j];
            }
        }
        output[i] = (int16_t)sum;
    }
}
