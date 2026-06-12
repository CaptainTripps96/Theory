#pragma once

#include "core/sequencing/AudioClip.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace tsq::ui
{
class WaveformCache final : private juce::ChangeListener
{
public:
    WaveformCache();
    ~WaveformCache() override;

    std::function<void()> onWaveformChanged;

    void drawWaveform (juce::Graphics& graphics,
                       const core::sequencing::AudioClip& clip,
                       juce::Rectangle<int> bounds,
                       juce::Colour waveformColour,
                       juce::Colour mutedColour);
    void clear();

private:
    struct Entry
    {
        std::string cacheKey;
        juce::File file;
        std::unique_ptr<juce::AudioThumbnail> thumbnail;
        bool missing = false;
    };

    Entry& entryFor (const core::sequencing::AudioSourceReference& source);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::AudioFormatManager formatManager_;
    juce::AudioThumbnailCache thumbnailCache_ { 64 };
    std::unordered_map<std::string, std::unique_ptr<Entry>> entries_;
};
}
