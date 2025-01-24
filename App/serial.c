// Copyright 2024 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include "app.h"
#include "usart.h"
#include "usb_device.h"

// This set of methods has two jobs:
// 1. rapidly transfer data from interrupt buffers into userspace buffers without loss
// 2. gather non-blank lines that are terminated by either \r or \n and wake up req task to process them

// Port descriptors
typedef struct {
    array *bytes;
    bool bytesTerminated;
    bool swallowNextNewline;
    mutex rxLock;
    mutex txLock;
    int taskId;
} serialDesc;
STATIC serialDesc usbDesc = {0};
#if ENABLE_USART1
STATIC serialDesc usart1Desc = {0};
#endif
#if ENABLE_USART2
STATIC serialDesc usart2Desc = {0};
#endif
STATIC serialDesc lpuart1Desc = {0};

// Interrupt buffers
STATIC uint8_t lpuart1InterruptBuffer[600];
#if ENABLE_USART1
STATIC uint8_t usart1InterruptBuffer[600];
#endif
#if ENABLE_USART2
STATIC uint8_t usart2InterruptBuffer[600];
#endif
STATIC uint8_t usbInterruptBuffer[600];
STATIC uint8_t isrDebugOutput[120];             // some messages will get truncated but who cares
STATIC uint32_t isrDebugOutputLen = 0;

// Last time we did work moving serial data
STATIC int64_t lastTimeDidWorkMs = 0L;

// Task ID
STATIC uint32_t serialTaskID = TASKID_UNKNOWN;

// Whether or not the serial polling is active
bool serialActive = false;

// Forwards
void serialReceivedNotification(UART_HandleTypeDef *huart, bool error);
bool pollPort(UART_HandleTypeDef *huart);
void debugOutput(uint8_t *buf, uint32_t buflen);

// Serial poller init
void serialInit(uint32_t taskID)
{

    // Remember our task ID
    serialTaskID = taskID;

    // Init the mutexes
    mutexInit(&lpuart1Desc.rxLock, MTX_SERIAL_RX);
    mutexInit(&lpuart1Desc.txLock, MTX_SERIAL_TX);
    mutexInit(&usbDesc.rxLock, MTX_SERIAL_RX);
    mutexInit(&usbDesc.txLock, MTX_SERIAL_TX);
#if ENABLE_USART1
    mutexInit(&usart1Desc.rxLock, MTX_SERIAL_RX);
    mutexInit(&usart1Desc.txLock, MTX_SERIAL_TX);
#endif
#if ENABLE_USART2
    mutexInit(&usart2Desc.rxLock, MTX_SERIAL_RX);
    mutexInit(&usart2Desc.txLock, MTX_SERIAL_TX);
#endif

    // Set the tasks of the handlers
    usbDesc.taskId = TASKID_REQ;
    lpuart1Desc.taskId = TASKID_REQ;
#if ENABLE_USART1
    usart1Desc.taskId = TASKID_REQ;
#endif
#if ENABLE_USART2
    usart2Desc.taskId = TASKID_REQ;
#endif

    // LPUART1
    MX_UART_RxConfigure(&hlpuart1, lpuart1InterruptBuffer, sizeof(lpuart1InterruptBuffer), serialReceivedNotification);
    MX_LPUART1_UART_Init(false, 9600);

    // USART1
#if ENABLE_USART1
    MX_UART_RxConfigure(&huart1, usart1InterruptBuffer, sizeof(usart1InterruptBuffer), serialReceivedNotification);
    MX_USART1_UART_Init(9600);
#endif

    // USART2
#if ENABLE_USART2
    MX_UART_RxConfigure(&huart2, usart2InterruptBuffer, sizeof(usart2InterruptBuffer), serialReceivedNotification);
    MX_USART2_UART_Init(9600);
#endif

    // USB (debug port)
    MX_UART_RxConfigure(NULL, usbInterruptBuffer, sizeof(usbInterruptBuffer), serialReceivedNotification);

    // Set debug function
    MX_DBG_SetOutput(debugOutput);

}

// Serial poller
void serialPoll(void)
{

    // Rest the USB if it needs to be reset
    if (osUsbDetected() && (peripherals & PERIPHERAL_USB) == 0) {
        MX_USB_DEVICE_Init();
    } else if (!osUsbDetected() && (peripherals & PERIPHERAL_USB) != 0) {
        MX_USB_DEVICE_DeInit();
    }

    // Note that we are busy servicing serial
    serialActive = true;

    // Poll ports so long as there's something to do
    bool didWork = false;
    while (true) {
        bool didSomething = false;
        didSomething |= pollPort(&hlpuart1);
        didSomething |= pollPort(&huart1);
        didSomething |= pollPort(&huart2);
        didSomething |= pollPort(NULL);
        if (isrDebugOutputLen > 0) {
            debugOutput(isrDebugOutput, isrDebugOutputLen);
            isrDebugOutputLen = 0;
        }
        didWork |= didSomething;
        if (!didSomething) {
            break;
        }
    }

    // If nothing was done, truly go idle
    uint32_t pollMs;
    if (didWork) {
        pollMs = 1;
        lastTimeDidWorkMs = timerMs();
    } else if (!timerMsElapsed(lastTimeDidWorkMs, 3000)) {
        pollMs = 100;
    } else {
        pollMs = ms1Hour;
        serialActive = false;
    }

    // Wait until there's something to do
    taskTake(serialTaskID, pollMs);

}

// Whether or not serial is active
bool serialIsActive(void)
{
    return serialActive;
}

// Notification
void serialReceivedNotification(UART_HandleTypeDef *huart, bool error)
{
    if (serialTaskID != TASKID_UNKNOWN) {

        // Because notifications are received (on LPUART1) before the character
        // has been fully received, and because the receive is not actually
        // yet completed, make sure we mark this as "work done" so that the
        // serialPoll loop doesn't just go back to sleep on the first iteration.
        lastTimeDidWorkMs = timerMs();

        // Wake the serial task
        taskGiveFromISR(serialTaskID);

    }
}

// Get the descriptor for the port
serialDesc *portDesc(UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        return &usbDesc;
#if ENABLE_USART1
    } else if (huart == &huart1) {
        return &usart1Desc;
#endif
#if ENABLE_USART2
    } else if (huart == &huart2) {
        return &usart2Desc;
#endif
    } else if (huart == &hlpuart1) {
        return &lpuart1Desc;
    }
    return NULL;
}

// See if there's port activity
bool pollPort(UART_HandleTypeDef *huart)
{

    // Get the descriptor
    serialDesc *desc = portDesc(huart);
    if (desc == NULL) {
        return false;
    }

    // Exit if we're already processing something for this task
    if (mutexIsLocked(&desc->rxLock, NULL)) {
        return false;
    }

    // Exit immediately if nothing available
    if (!MX_UART_RxAvailable(huart)) {
        return false;
    }

    // Lock the mutex and see if we're already processing something.  This
    // is a really unusual condition because it means that someone is
    // sequentially chaining "cmd"s or something like that.  We need
    // to wait until that command is processed, though, before taking
    // any more from the interrupt handler.
    mutexLock(&desc->rxLock);
    if (desc->bytesTerminated) {
        mutexUnlock(&desc->rxLock);
        taskGive(desc->taskId);
        timerMsSleep(50);
        return false;
    }

    // Get the databyte
    uint8_t databyte = MX_UART_RxGet(huart);

    // Alloc if new
    if (desc->bytes == NULL) {
        if (arrayAllocBytes(&desc->bytes) != errNone) {
            mutexUnlock(&desc->rxLock);
            return false;
        }
        desc->bytesTerminated = false;
    }

    // Swallow newline if appropriate
    if (databyte == '\n' && desc->swallowNextNewline) {
        desc->swallowNextNewline = false;
        mutexUnlock(&desc->rxLock);
        return true;
    }

    // Awaken request processing task if a control character, because it's a waste to do otherwise
    if (databyte == '\r' || databyte == '\n') {
        desc->swallowNextNewline = (databyte == '\r');
        if (desc->taskId != TASKID_UNKNOWN) {
            desc->bytesTerminated = true;
            taskGive(desc->taskId);
        }
        mutexUnlock(&desc->rxLock);
        return true;
    }
    desc->swallowNextNewline = false;

    // Append all bytes except \r and \n, making sure that we ALWAYS have a '\0' at the end
    // so that later we can do a JParse that requires a null-terminated string.
    char byteString[2];
    byteString[0] = (char) databyte;
    byteString[1] = '\0';
    if (arrayAppendStringBytes(desc->bytes, byteString) != errNone) {
        mutexUnlock(&desc->rxLock);
        return false;
    }

    // Done
    mutexUnlock(&desc->rxLock);
    return true;
}

// See if there's data, and lock the receive buffer if so
bool serialLock(UART_HandleTypeDef *huart, uint8_t **retData, uint32_t *retDataLen, bool *retDiagAllowed)
{

    // Get port desc
    serialDesc *desc = portDesc(huart);
    if (desc == NULL) {
        return false;
    }

    // If a different task is already processing this serial, don't block
    if (mutexIsLocked(&desc->rxLock, NULL)) {
        return false;
    }

    // If no data is waiting, don't block
    if (desc->bytes == NULL) {
        return false;
    }
    mutexLock(&desc->rxLock);
    if (desc->bytes == NULL) {
        mutexUnlock(&desc->rxLock);
        return false;
    }

    // Return buffer, leaving desc locked.  Note that we return
    // with 0-length if just a newline was passed to us, and this
    // is essential when note-c is initializing and it's just
    // sending \n\n\n's to see if we're alive.
    if (!desc->bytesTerminated) {
        mutexUnlock(&desc->rxLock);
        return false;
    }
    *retData = (uint8_t *) arrayAddress(desc->bytes);
    *retDataLen = arrayLength(desc->bytes);
    *retDiagAllowed = serialIsDebugPort(huart);
    return true;

}

// Unlock and potentially reset
void serialUnlock(UART_HandleTypeDef *huart, bool reset)
{

    // Get port desc
    serialDesc *desc = portDesc(huart);
    if (desc == NULL) {
        return;
    }

    // Reset if desired
    if (reset) {
        if (desc->bytes != NULL) {
            arrayFree(desc->bytes);
            desc->bytes = NULL;
        }
        desc->bytesTerminated = false;
    }

    // Unlock
    mutexUnlock(&desc->rxLock);
}

// Output string to debug uart
void serialOutputString(UART_HandleTypeDef *huart, char *buf)
{
    serialOutput(huart, (uint8_t *)buf, strlen(buf));
}

// See if this is one of the debug ports
bool serialIsDebugPort(UART_HandleTypeDef *huart)
{
    if (huart == NULL || huart == &huart1) {
        return true;
    }
    return false;
}

// Debug output
void debugOutput(uint8_t *buf, uint32_t buflen)
{

    // Buffer the output as debug output if we're in an ISR (which ST's middleware does)
    if (MX_InISR()) {
        uint32_t left = sizeof(isrDebugOutput)-isrDebugOutputLen;
        if (buflen > left) {
            buflen = left;
        }
        memcpy(&isrDebugOutput[isrDebugOutputLen], buf, buflen);
        isrDebugOutputLen += buflen;
        taskGiveFromISR(serialTaskID);
        return;
    }

    // Output to USB if it's detected
    if (osUsbDetected()) {
        serialOutput(NULL, buf, buflen);
    }

}

// Output to a port, suspending tasks that might interrupt it if not DMA
void uartTransmit(UART_HandleTypeDef *huart, uint8_t *buf, uint32_t buflen, uint32_t timeoutMs)
{
    if (MX_UART_IsDMA(huart)) {
        MX_UART_Transmit(huart, buf, buflen, timeoutMs);
    } else {
        uint32_t prio = taskIOPriorityBegin();
        MX_UART_Transmit(huart, buf, buflen, timeoutMs);
        taskIOPriorityEnd(prio);
    }
}

// Output to the specified port
void serialOutput(UART_HandleTypeDef *huart, uint8_t *buf, uint32_t buflen)
{

    // Output
    serialDesc *desc = portDesc(huart);
    if (desc == NULL) {
        return;
    }
    if (buflen > 0) {
        mutexLock(&desc->txLock);
        uartTransmit(huart, buf, buflen, 500);
        mutexUnlock(&desc->txLock);
    }
}

// Output to the specified port with the Request Terminator (\r\n)
void serialOutputLn(UART_HandleTypeDef *huart, uint8_t *buf, uint32_t buflen)
{

    // Change terminator based upon target
    uint8_t *terminator = (uint8_t *) "\r\n";;
    uint32_t terminatorLen = 2;

    serialDesc *desc = portDesc(huart);
    if (desc == NULL) {
        return;
    }
    mutexLock(&desc->txLock);
    if (buflen == 0) {
        uartTransmit(huart, terminator, terminatorLen, 500);
    } else {
        // We prefer to send it in a single chunk because it eliminates
        // an I2C poll iteration for the client.
        uint8_t *temp;
        err_t err = memDup(buf, buflen+terminatorLen, &temp);
        if (err) {
            uartTransmit(huart, buf, buflen, 500);
            uartTransmit(huart, terminator, terminatorLen, 500);
        } else {
            for (int i=0; i<terminatorLen; i++) {
                temp[buflen++] = terminator[i];
            }
            uartTransmit(huart, temp, buflen, 500);
            memFree(temp);
        }
    }
    mutexUnlock(&desc->txLock);
}
