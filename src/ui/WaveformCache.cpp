#include "ui/WaveformCache.h"

#include <algorithm>

namespace tsq::ui
{
namespace
{
juce::String toJuceString (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

std::string cacheKeyForFile (const core::sequencing::AudioSourceReference& source, const juce::File& file)
{
    if (! file.existsAsFile())
        return source.sourceId + "::missing::" + source.filePath;

    return source.sourceId + "::" + source.filePath + "::" + std::to_string (file.getSize())
        + "::" + std::to_string (file.getLastModificationTime().toMilliseconds());
}
}

WaveformCache::WaveformCache()
{
    formatManager_.registerBasicFormats();
}

WaveformCache::~WaveformCache()
{
    clear();
}

void WaveformCache::drawWaveform (juce::Graphics& graphics,
                                  const core::sequencing::AudioClip& clip,
                                  juce::Rectangle<int> bounds,
                                  juce::Colour waveformColour,
                                  juce::Colour mutedColour)
{
    if (bounds.getWidth() <= 4 || bounds.getHeight() <= 4)
        return;

    auto& entry = entryFor (clip.source());
    const auto waveformBounds = bounds.reduced (6, 8);

    if (entry.missing || entry.thumbnail == nullptr)
    {
        graphics.setColour (mutedColour.withAlpha (0.70f));
        graphics.drawLine (static_cast<float> (waveformBounds.getX()),
                           static_cast<float> (waveformBounds.getCentreY()),
                           static_cast<float> (waveformBounds.getRight()),
                           static_cast<float> (waveformBounds.getCentreY()),
                           1.0f);
        graphics.setFont (juce::FontOptions { 10.5f });
        graphics.drawFittedText ("Missing audio", waveformBounds, juce::Justification::centred, 1);
        return;
    }

    const auto totalLength = entry.thumbnail->getTotalLength();
    if (totalLength <= 0.0)
    {
        graphics.setColour (mutedColour.withAlpha (0.55f));
        graphics.drawLine (static_cast<float> (waveformBounds.getX()),
                           static_cast<float> (waveformBounds.getCentreY()),
                           static_cast<float> (waveformBounds.getRight()),
                           static_cast<float> (waveformBounds.getCentreY()),
                           1.0f);
        return;
    }

    graphics.setColour (waveformColour.withAlpha (0.90f));
    entry.thumbnail->drawChannels (graphics, waveformBounds, 0.0, totalLength, 1.0f);
}

void WaveformCache::clear()
{
    for (auto& [key, entry] : entries_)
        if (entry != nullptr && entry->thumbnail != nullptr)
            entry->thumbnail->removeChangeListener (this);

    entries_.clear();
    thumbnailCache_.clear();
}

WaveformCache::Entry& WaveformCache::entryFor (const core::sequencing::AudioSourceReference& source)
{
    const auto file = juce::File::createFileWithoutCheckingPath (toJuceString (source.filePath));
    const auto cacheKey = cacheKeyForFile (source, file);
    const auto mapKey = source.sourceId.empty() ? source.filePath : source.sourceId;

    if (auto existing = entries_.find (mapKey);
        existing != entries_.end() && existing->second != nullptr && existing->second->cacheKey == cacheKey)
    {
        return *existing->second;
    }

    auto entry = std::make_unique<Entry>();
    entry->cacheKey = cacheKey;
    entry->file = file;
    entry->missing = ! file.existsAsFile();
    entry->thumbnail = std::make_unique<juce::AudioThumbnail> (512, formatManager_, thumbnailCache_);
    entry->thumbnail->addChangeListener (this);

    if (! entry->missing)
        entry->thumbnail->setSource (new juce::FileInputSource (file));

    auto& stored = entries_[mapKey];
    if (stored != nullptr && stored->thumbnail != nullptr)
        stored->thumbnail->removeChangeListener (this);

    stored = std::move (entry);
    return *stored;
}

void WaveformCache::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (onWaveformChanged)
        onWaveformChanged();
}
}
