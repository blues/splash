
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "stm32l4xx.h"


#pragma once

#define ARM_DSP_ATTRIBUTE

typedef int16_t q15_t;
typedef int64_t q63_t;

typedef struct {
    uint16_t numTaps;
    q15_t *pState;
    const q15_t *pCoeffs;
} arm_fir_instance_q15;

typedef enum {
    ARM_MATH_SUCCESS                 =  0,        /**< No error */
    ARM_MATH_ARGUMENT_ERROR          = -1,        /**< One or more arguments are incorrect */
    ARM_MATH_LENGTH_ERROR            = -2,        /**< Length of data buffer is incorrect */
    ARM_MATH_SIZE_MISMATCH           = -3,        /**< Size of matrices is not compatible with the operation */
    ARM_MATH_NANINF                  = -4,        /**< Not-a-number (NaN) or infinity is generated */
    ARM_MATH_SINGULAR                = -5,        /**< Input matrix is singular and cannot be inverted */
    ARM_MATH_TEST_FAILURE            = -6,        /**< Test Failed */
    ARM_MATH_DECOMPOSITION_FAILURE   = -7         /**< Decomposition Failed */
} arm_status;

typedef struct {
    uint8_t M;
    uint16_t numTaps;
    const q15_t *pCoeffs;
    q15_t *pState;
} arm_fir_decimate_instance_q15;

arm_status arm_fir_decimate_init_q15(
    arm_fir_decimate_instance_q15 * S,
    uint16_t numTaps,
    uint8_t M,
    const q15_t * pCoeffs,
    q15_t * pState,
    uint32_t blockSize);

void arm_fir_decimate_q15(
    const arm_fir_decimate_instance_q15 * S,
    const q15_t * pSrc,
    q15_t * pDst,
    uint32_t blockSize);

ARM_DSP_ATTRIBUTE arm_status arm_fir_decimate_init_q15(
    arm_fir_decimate_instance_q15 * S,
    uint16_t numTaps,
    uint8_t M,
    const q15_t * pCoeffs,
    q15_t * pState,
    uint32_t blockSize);

ARM_DSP_ATTRIBUTE void arm_fir_decimate_q15(
    const arm_fir_decimate_instance_q15 * S,
    const q15_t * pSrc,
    q15_t * pDst,
    uint32_t blockSize);
