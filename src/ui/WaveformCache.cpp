#include "ui/WaveformCache.h"

#include <algorithm>
#include <cstdint>

namespace tsq::ui
{
namespace
{
constexpr std::int64_t fileValidationIntervalMs = 1000;

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
    if (waveformBounds.getWidth() <= 0 || waveformBounds.getHeight() <= 0)
        return;

    if (entry.missing || entry.thumbnail == nullptr)
    {
        if (! renderedImageMatches (entry,
                                    waveformBounds.getWidth(),
                                    waveformBounds.getHeight(),
                                    waveformColour,
                                    mutedColour,
                                    -1.0,
                                    true,
                                    false))
        {
            entry.renderedImage = juce::Image {
                juce::Image::ARGB,
                waveformBounds.getWidth(),
                waveformBounds.getHeight(),
                true
            };
            juce::Graphics imageGraphics { entry.renderedImage };
            const auto imageBounds = juce::Rectangle<int> { 0, 0, waveformBounds.getWidth(), waveformBounds.getHeight() };
            imageGraphics.setColour (mutedColour.withAlpha (0.70f));
            imageGraphics.drawLine (0.0f,
                                    static_cast<float> (imageBounds.getCentreY()),
                                    static_cast<float> (imageBounds.getRight()),
                                    static_cast<float> (imageBounds.getCentreY()),
                                    1.0f);
            imageGraphics.setFont (juce::FontOptions { 10.5f });
            imageGraphics.drawFittedText ("Missing audio", imageBounds, juce::Justification::centred, 1);
            entry.renderedWaveformColour = waveformColour;
            entry.renderedMutedColour = mutedColour;
            entry.renderedTotalLength = -1.0;
            entry.renderedWidth = waveformBounds.getWidth();
            entry.renderedHeight = waveformBounds.getHeight();
            entry.renderedMissing = true;
            entry.renderedPending = false;
        }

        graphics.drawImageAt (entry.renderedImage, waveformBounds.getX(), waveformBounds.getY());
        return;
    }

    const auto totalLength = entry.thumbnail->getTotalLength();
    if (totalLength <= 0.0)
    {
        if (! renderedImageMatches (entry,
                                    waveformBounds.getWidth(),
                                    waveformBounds.getHeight(),
                                    waveformColour,
                                    mutedColour,
                                    0.0,
                                    false,
                                    true))
        {
            entry.renderedImage = juce::Image {
                juce::Image::ARGB,
                waveformBounds.getWidth(),
                waveformBounds.getHeight(),
                true
            };
            juce::Graphics imageGraphics { entry.renderedImage };
            const auto imageBounds = juce::Rectangle<int> { 0, 0, waveformBounds.getWidth(), waveformBounds.getHeight() };
            imageGraphics.setColour (mutedColour.withAlpha (0.55f));
            imageGraphics.drawLine (0.0f,
                                    static_cast<float> (imageBounds.getCentreY()),
                                    static_cast<float> (imageBounds.getRight()),
                                    static_cast<float> (imageBounds.getCentreY()),
                                    1.0f);
            entry.renderedWaveformColour = waveformColour;
            entry.renderedMutedColour = mutedColour;
            entry.renderedTotalLength = 0.0;
            entry.renderedWidth = waveformBounds.getWidth();
            entry.renderedHeight = waveformBounds.getHeight();
            entry.renderedMissing = false;
            entry.renderedPending = true;
        }

        graphics.drawImageAt (entry.renderedImage, waveformBounds.getX(), waveformBounds.getY());
        return;
    }

    if (! renderedImageMatches (entry,
                                waveformBounds.getWidth(),
                                waveformBounds.getHeight(),
                                waveformColour,
                                mutedColour,
                                totalLength,
                                false,
                                false))
    {
        entry.renderedImage = juce::Image {
            juce::Image::ARGB,
            waveformBounds.getWidth(),
            waveformBounds.getHeight(),
            true
        };
        juce::Graphics imageGraphics { entry.renderedImage };
        imageGraphics.setColour (waveformColour.withAlpha (0.90f));
        entry.thumbnail->drawChannels (imageGraphics,
                                       juce::Rectangle<int> { 0, 0, waveformBounds.getWidth(), waveformBounds.getHeight() },
                                       0.0,
                                       totalLength,
                                       1.0f);
        entry.renderedWaveformColour = waveformColour;
        entry.renderedMutedColour = mutedColour;
        entry.renderedTotalLength = totalLength;
        entry.renderedWidth = waveformBounds.getWidth();
        entry.renderedHeight = waveformBounds.getHeight();
        entry.renderedMissing = false;
        entry.renderedPending = false;
    }

    graphics.drawImageAt (entry.renderedImage, waveformBounds.getX(), waveformBounds.getY());
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
    const auto mapKey = source.sourceId.empty() ? source.filePath : source.sourceId;
    const auto nowMs = juce::Time::currentTimeMillis();

    if (auto existing = entries_.find (mapKey);
        existing != entries_.end() && existing->second != nullptr)
    {
        if (nowMs - existing->second->lastValidationMs < fileValidationIntervalMs)
            return *existing->second;

        const auto cacheKey = cacheKeyForFile (source, file);
        if (existing->second->cacheKey == cacheKey)
        {
            existing->second->lastValidationMs = nowMs;
            return *existing->second;
        }
    }

    const auto cacheKey = cacheKeyForFile (source, file);
    auto entry = std::make_unique<Entry>();
    entry->cacheKey = cacheKey;
    entry->file = file;
    entry->lastValidationMs = nowMs;
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

bool WaveformCache::renderedImageMatches (const Entry& entry,
                                          int width,
                                          int height,
                                          juce::Colour waveformColour,
                                          juce::Colour mutedColour,
                                          double totalLength,
                                          bool missing,
                                          bool pending) const noexcept
{
    return entry.renderedImage.isValid()
        && entry.renderedWidth == width
        && entry.renderedHeight == height
        && entry.renderedWaveformColour == waveformColour
        && entry.renderedMutedColour == mutedColour
        && entry.renderedTotalLength == totalLength
        && entry.renderedMissing == missing
        && entry.renderedPending == pending;
}

void WaveformCache::invalidateRenderedImage (Entry& entry) noexcept
{
    entry.renderedImage = {};
    entry.renderedWidth = 0;
    entry.renderedHeight = 0;
    entry.renderedTotalLength = -1.0;
    entry.renderedMissing = false;
    entry.renderedPending = false;
}

void WaveformCache::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& [key, entry] : entries_)
        if (entry != nullptr)
            invalidateRenderedImage (*entry);

    if (onWaveformChanged)
        onWaveformChanged();
}
}
