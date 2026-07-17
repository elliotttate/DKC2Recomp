#pragma once

#include <stddef.h>
#include <stdint.h>

#include "common_cpu_infra.h"

const RtlGameInfo *Dkc2GameInfo(void);
void Dkc2BeginDrawing(uint8_t *pixels, size_t pitch);
void Dkc2DrawPpuFrame(void);
uint32_t Dkc2ResumePc(void);
int Dkc2LastLleResult(void);
