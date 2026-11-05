#pragma once
#include <cstdint>

// 头文件 inline 局部：第 11 行的 imageCount 与主文件（noise_sample.cpp）第 11
// 行的同名声明行号撞号——引擎若无主文件收敛，会把这里的行号当成主文件行号输出。
inline uint32_t helperNoise(uint32_t v) {
    uint32_t a = v + 1;
    uint32_t b = a + 1;
    uint32_t c = b + 1;
    uint32_t d = c + 1;
    uint32_t imageCount = d + 1;
    return imageCount;
}
