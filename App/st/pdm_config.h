
#pragma once

#define AUDIO_IN_FREQ_MHZ                   3047619
#define AUDIO_IN_FREQ                       (AUDIO_IN_FREQ_MHZ/1000)
#define AUDIO_IN_DECIMATOR_FACTOR           64   // ((AUDIO_IN_FREQ * 1000) / AUDIO_IN_SAMPLING_FREQUENCY)
#define MAX_AUDIO_IN_CHANNEL_NBR_PER_IF     1
#define MAX_AUDIO_IN_CHANNEL_NBR_TOTAL      1
#define AUDIO_IN_CHANNELS                   1
#define AUDIO_IN_BITPERSAMPLE               16
#define AUDIO_IN_SAMPLING_FREQUENCY         48000
