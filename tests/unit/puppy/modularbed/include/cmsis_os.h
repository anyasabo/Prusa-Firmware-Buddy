// Fake CMSIS-RTOS API for the host tests of the modular bed control code.
// fake_hal.cpp implements the functions. The mail queue is a FIFO with no wait.
#pragma once

#include <cstddef>
#include <cstdint>

uint32_t osKernelSysTick();

using os_pthread = void (*)(const void *);
using osThreadId = void *;
using osMailQId = void *;

struct osThreadDef_t {
    const char *name;
    os_pthread pthread;
};

struct osMailQDef_t {
    size_t queue_sz;
    size_t item_sz;
};

enum osStatus {
    osOK = 0,
    osEventMail = 0x20,
    osErrorResource = 0x81,
};

struct osEvent {
    osStatus status;
    union {
        void *p;
        uint32_t v;
    } value;
};

constexpr uint32_t osWaitForever = 0xFFFFFFFF;

#define osThreadDef(name, thread, priority, instances, stacksz) \
    const osThreadDef_t os_thread_def_##name = { #name, (thread) }
#define osThread(name) &os_thread_def_##name
#define osMailQDef(name, queue_sz, type) \
    const osMailQDef_t os_mailQ_def_##name = { (queue_sz), sizeof(type) }
#define osMailQ(name) &os_mailQ_def_##name

osThreadId osThreadCreate(const osThreadDef_t *thread_def, void *argument);
osMailQId osMailCreate(const osMailQDef_t *queue_def, osThreadId thread_id);
void *osMailAlloc(osMailQId queue_id, uint32_t millisec);
osStatus osMailPut(osMailQId queue_id, void *mail);
osEvent osMailGet(osMailQId queue_id, uint32_t millisec);
osStatus osMailFree(osMailQId queue_id, void *mail);
osStatus osDelay(uint32_t millisec);
