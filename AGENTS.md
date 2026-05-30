# FreeRTOS Training Project Rules

项目：Waveshare RP2350-PiZero / RP2350B + Pico SDK + FreeRTOS 学习工程。

默认协作方式：
1. 默认只改代码和文档，不编译、不烧录，除非用户明确要求。
2. 如果需要编译，只使用 Ninja，不使用 Visual Studio generator。
3. 用户希望亲自熟悉流程，所以优先给出解释、步骤和检查建议。
4. 修改前先查看相关源码、CMake、FreeRTOSConfig 和 docs。
5. 不回退用户已有修改。

学习流程：
1. 每次只引入一个核心 RTOS 概念。
2. demo 跑通后，结合串口输出解释机制。
3. 讲解时给出项目文件和 FreeRTOS/Pico SDK 源码位置。
4. 如引用官方手册，GitHub仓库等外部资料，请给出具体页码供用户查阅原文。
5. 笔记放在 `docs/`，按 `00`、`01`、`02` 编号。
6. 笔记包含“报错/问题修复”小节。

项目特殊问题：
1. RP2350-PiZero 自动 `Run Project (USB)` 烧录不稳定。
2. 手动 BOOTSEL + UF2 或手动 BOOTSEL + VSCode Run Project 是当前可靠流程。
3. 曾出现 Python/CMake build cache 路径问题，不要随意污染 build 缓存。