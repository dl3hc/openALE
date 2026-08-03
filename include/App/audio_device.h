/**
 * \file App/audio_device.h
 * \brief ale::AudioDevice — domain alias for pal::IAudioDriver.
 *
 * The canonical audio interface is pal::IAudioDriver (pull model).
 * This header exposes it under the ale namespace so domain-layer code
 * (ALEController, ale_cli) can use AudioDevice without a pal:: prefix,
 * and provides the make_audio_device() factory for backward compat.
 */

#pragma once
#include "PAL/audio_driver.h"
#include <memory>

namespace ale {

using AudioDevice = pal::IAudioDriver;

/** Factory: returns the platform-appropriate AudioDevice (= pal::IAudioDriver). */
inline std::unique_ptr<AudioDevice> make_audio_device()
{
    return pal::create_audio_driver();
}

} // namespace ale
