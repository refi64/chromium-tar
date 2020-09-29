// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_TTS_CHROME_TTS_H_
#define CHROMEOS_SERVICES_TTS_CHROME_TTS_H_

#include <cstddef>
#include <cstdint>

void GoogleTtsSetLogger(void (*logger_func)(int severity, const char* message));

bool GoogleTtsInit(const char* pipeline_path, const char* path_prefix);

void GoogleTtsShutdown();

bool GoogleTtsInstallVoice(const char* voice_name,
                           const char* voice_bytes,
                           int size);

bool GoogleTtsInitBuffered(const char* text_jspb, int text_jspb_len);

int GoogleTtsReadBuffered();

void GoogleTtsFinalizeBuffered();

size_t GoogleTtsGetTimepointsCount();

float GoogleTtsGetTimepointsTimeInSecsAtIndex(size_t index);

int GoogleTtsGetTimepointsCharIndexAtIndex(size_t index);

char* GoogleTtsGetEventBufferPtr();

size_t GoogleTtsGetEventBufferLen();
#endif  // CHROMEOS_SERVICES_TTS_CHROME_TTS_H_