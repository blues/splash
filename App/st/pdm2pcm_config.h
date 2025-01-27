
#pragma once

// Whether or not to use simple version or sophisticated version of decimator
#define USE_SIMPLE_DECIMATION   false

#include "pdm_config.h"

// Number of PDM samples being processed at each iteration.
#if USE_SIMPLE_DECIMATION
#define BLOCK_SIZE 2048
#else
#define BLOCK_SIZE 9200   // a reasonable chunk that is a multiple of AUDIO_IN_FREQ and is an even factor of both DEC_CIC_FACTOR and DEC_OUT_FACTOR
#endif

#define DEC_CIC_FACTOR 8  // First filter Decimation factor

#define N_DATA_CIC_DEC (BLOCK_SIZE/DEC_CIC_FACTOR)

#define DEC_OUT_FACTOR 10  // Second filter Decimation factor

#define FIR_DELAY 4// ceil(gd_cic_1(1)/8) + ceil(gd_cic_2(1)/10) where 8 and 10 are the decimation factors of two filter cascade
