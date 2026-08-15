#ifndef DKC2_INPUT_RECORDING_H
#define DKC2_INPUT_RECORDING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Dkc2InputRecorder {
  FILE *stream;
  unsigned long long frames;
} Dkc2InputRecorder;

bool Dkc2InputRecorderOpen(Dkc2InputRecorder *recorder, const char *path,
                           char *error, size_t error_size);
bool Dkc2InputRecorderWrite(Dkc2InputRecorder *recorder, uint32_t controller,
                            char *error, size_t error_size);
bool Dkc2InputRecorderClose(Dkc2InputRecorder *recorder,
                            char *error, size_t error_size);
bool Dkc2InputRecorderIsOpen(const Dkc2InputRecorder *recorder);

#endif
