#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace meridian
{
namespace ID
{
    // Global
    inline constexpr const char* inGain      = "in_gain";
    inline constexpr const char* outGain     = "out_gain";
    inline constexpr const char* bypass      = "bypass";
    inline constexpr const char* matchBypass = "match_bypass";

    // Unmask EQ (adaptive, perception-driven)
    inline constexpr const char* umOn        = "um_on";
    inline constexpr const char* umRecover   = "um_recover";
    inline constexpr const char* umTame      = "um_tame";
    inline constexpr const char* umBias      = "um_bias";
    inline constexpr const char* umBrighten  = "um_brighten";
    inline constexpr const char* umBoost     = "um_boost";
    inline constexpr const char* umLowLimit  = "um_low_limit";
    inline constexpr const char* umHighLimit = "um_high_limit";
    inline constexpr const char* umGain      = "um_gain";
    inline constexpr const char* umSidechain = "um_sidechain";
    inline constexpr const char* umQuality   = "um_quality";

    // Character (mix-bus chain)
    inline constexpr const char* chOn        = "ch_on";
    inline constexpr const char* chAmount    = "ch_amount";
    inline constexpr const char* chLow       = "ch_low";
    inline constexpr const char* chMid       = "ch_mid";
    inline constexpr const char* chHigh      = "ch_high";
    inline constexpr const char* chSidechain = "ch_sidechain";
    inline constexpr const char* chOversample= "ch_oversample";

    // Limiter
    inline constexpr const char* limOn       = "lim_on";
    inline constexpr const char* limGain     = "lim_gain";
    inline constexpr const char* limCeiling  = "lim_ceiling";

    // Loudness
    inline constexpr const char* lufsTarget  = "lufs_target";
    inline constexpr const char* refPlay     = "ref_play";
    inline constexpr const char* refMatch    = "ref_match";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Loudness targets shown on the Loudness page.
struct LoudnessTarget { const char* name; float lufs; float truePeak; };
inline constexpr LoudnessTarget loudnessTargets[] = {
    { "Streaming  -14 LUFS", -14.0f, -1.0f },
    { "Apple Music  -16 LUFS", -16.0f, -1.0f },
    { "YouTube  -14 LUFS", -14.0f, -1.0f },
    { "Broadcast  -23 LUFS", -23.0f, -1.0f },
    { "Club / Loud  -9 LUFS", -9.0f, -0.3f },
    { "CD / Hot Master  -8 LUFS", -8.0f, -0.3f },
};
inline constexpr int numLoudnessTargets = (int) (sizeof (loudnessTargets) / sizeof (loudnessTargets[0]));

} // namespace meridian
