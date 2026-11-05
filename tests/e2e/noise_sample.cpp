#include <cstdint>
#include <iostream>
#include "noise_helper.h"

// 函数指针 typedef 原型：形参 imageCount 不属于任何 FunctionDecl
// （getParentFunctionOrMethod() 返回 null），--var 命中它曾触发
// dyn_cast<FunctionDecl>(null) 解引用崩溃（exit 0xC0000005）。
typedef void (*PFN_process)(uint32_t imageCount);

int main() {
    uint32_t imageCount = 42;
    std::cout << "pengtest noise probe" << std::endl;
    return static_cast<int>(helperNoise(imageCount));
}
