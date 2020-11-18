//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ANGLEPerfTestArgs.cpp:
//   Parse command line arguments for angle_perftests.
//

#include "ANGLEPerfTestArgs.h"
#include <string.h>
#include <sstream>

namespace angle
{
bool gCalibration          = false;
int gStepsToRunOverride    = -1;
bool gEnableTrace          = false;
const char *gTraceFile     = "ANGLETrace.json";
const char *gScreenShotDir = nullptr;
bool gVerboseLogging       = false;
double gTestTimeSeconds    = 1.0;
int gTestTrials            = 3;

// Default to three warmup loops. There's no science to this. More than two loops was experimentally
// helpful on a Windows NVIDIA setup when testing with Vulkan and native trace tests.
int gWarmupLoops = 3;
}  // namespace angle

namespace
{
int ReadIntArgument(const char *arg)
{
    std::stringstream strstr;
    strstr << arg;

    int value;
    strstr >> value;
    return value;
}

// The same as --screenshot-dir, but used by Chrome tests.
constexpr char kRenderTestDirArg[] = "--render-test-output-dir=";
}  // namespace

using namespace angle;

void ANGLEProcessPerfTestArgs(int *argc, char **argv)
{
    int argcOutCount = 0;

    for (int argIndex = 0; argIndex < *argc; argIndex++)
    {
        if (strcmp("--one-frame-only", argv[argIndex]) == 0)
        {
            gStepsToRunOverride = 1;
            gWarmupLoops        = 0;
        }
        else if (strcmp("--enable-trace", argv[argIndex]) == 0)
        {
            gEnableTrace = true;
        }
        else if (strcmp("--trace-file", argv[argIndex]) == 0 && argIndex < *argc - 1)
        {
            gTraceFile = argv[argIndex + 1];
            // Skip an additional argument.
            argIndex++;
        }
        else if (strcmp("--calibration", argv[argIndex]) == 0)
        {
            gCalibration = true;
        }
        else if (strcmp("--steps", argv[argIndex]) == 0 && argIndex < *argc - 1)
        {
            gStepsToRunOverride = ReadIntArgument(argv[argIndex + 1]);
            // Skip an additional argument.
            argIndex++;
        }
        else if (strcmp("--screenshot-dir", argv[argIndex]) == 0 && argIndex < *argc - 1)
        {
            gScreenShotDir = argv[argIndex + 1];
            argIndex++;
        }
        else if (strcmp("--verbose-logging", argv[argIndex]) == 0)
        {
            gVerboseLogging = true;
        }
        else if (strcmp("--warmup-loops", argv[argIndex]) == 0)
        {
            gWarmupLoops = ReadIntArgument(argv[argIndex + 1]);
            // Skip an additional argument.
            argIndex++;
        }
        else if (strcmp("--no-warmup", argv[argIndex]) == 0)
        {
            gWarmupLoops = 0;
        }
        else if (strncmp(kRenderTestDirArg, argv[argIndex], strlen(kRenderTestDirArg)) == 0)
        {
            gScreenShotDir = argv[argIndex] + strlen(kRenderTestDirArg);
        }
        else if (strcmp("--test-time", argv[argIndex]) == 0)
        {
            gTestTimeSeconds = ReadIntArgument(argv[argIndex + 1]);
            // Skip an additional argument.
            argIndex++;
        }
        else if (strcmp("--trials", argv[argIndex]) == 0)
        {
            gTestTrials = ReadIntArgument(argv[argIndex + 1]);
            // Skip an additional argument.
            argIndex++;
        }
        else
        {
            argv[argcOutCount++] = argv[argIndex];
        }
    }

    *argc = argcOutCount;
}
