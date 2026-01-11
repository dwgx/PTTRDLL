#pragma once
#include <cstdint>

// Simplified SecureFloat/SecureInt layouts taken from IL2CPP dump.
// These mirror the field offsets so we can safely read/write health-related data.
struct SecureFloat
{
    int32_t offset1;        // 0x0
    void* value1;           // 0x8 (float[])
    float valueF1;          // 0x10
    float valueF2;          // 0x14
    int32_t offset2;        // 0x18
    float valueF3;          // 0x1C
    void* value2;           // 0x20 (float[])
    int32_t offset3;        // 0x28
    int32_t value1Index;    // 0x2C
    int32_t value2Index;    // 0x30
    void* setValueCallback; // 0x38
    void* getValueCallback; // 0x40
};
static_assert(sizeof(SecureFloat) == 0x48, "SecureFloat size mismatch");

struct SecureInt
{
    int32_t offset1;        // 0x0
    void* value1;           // 0x8 (int[])
    int32_t valueF1;        // 0x10
    int32_t valueF2;        // 0x14
    int32_t offset2;        // 0x18
    int32_t valueF3;        // 0x1C
    void* value2;           // 0x20 (int[])
    int32_t offset3;        // 0x28
    int32_t value1Index;    // 0x2C
    int32_t value2Index;    // 0x30
    void* setValueCallback; // 0x38
    void* getValueCallback; // 0x40
};
static_assert(sizeof(SecureInt) == 0x48, "SecureInt size mismatch");
