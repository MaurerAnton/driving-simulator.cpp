// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/voicesettings.ts @ v1.7.7
/**
 * The voice of the spoken instructions, in the settings: which of the
 * browser's voices reads the current language (automatic: the best one),
 * how fast and how high, a button to hear it – kept per language.  Below,
 * what the player should know: that this device has no voice for the
 * language (the instructions are then shown only, and where a voice can be
 * added), that only a related language's voice is there, that the voices
 * marked "online" send the text to the browser's speech service.
 *
 * The list follows the browser's voices as they arrive (audio/speech.ts).
 */
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>

#include "audio/voicepick.h"
#include "i18n/index.h"
#include "platform/dom.h"

namespace fs {

/** `(key, vars?) => string`: a text in the player's language (the original's `T`) */
using TrFn = std::function<std::string(const std::string& key, const TrParams& vars)>;

/** What the settings panel hands over: these three of the settings (`VoiceSettingsHost{s.voice, s.volume, s.speech}`). */
struct VoiceSettingsHost {
  bool& voice;
  double& volume;
  std::optional<SpeechPrefs>& speech;
};

/**
 * The rows of the voice settings for a language.  `changed`: store the
 * settings (called after every change).
 */
dom::El voiceSettings(VoiceSettingsHost s, const std::string& lang, const TrFn& t, std::function<void()> changed);

}  // namespace fs
