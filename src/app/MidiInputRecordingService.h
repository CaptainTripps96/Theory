#pragma once

#include "app/MidiInputTypes.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

namespace tsq::app
{
class MidiInputRecordingService final : private juce::MidiInputCallback
{
public:
    MidiInputRecordingService();
    ~MidiInputRecordingService() override;

    std::vector<MidiInputDeviceInfo> availableInputDevices() const;
    bool openInputDevice (const std::string& identifier);
    void closeInputDevice();

    bool hasOpenInputDevice() const noexcept;
    const std::string& selectedInputIdentifier() const noexcept;
    const std::string& selectedInputName() const noexcept;
    int droppedEventCount() const noexcept;

    std::vector<QueuedMidiInputEvent> drainEvents();

private:
    static constexpr int queueCapacity = 512;

    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    std::unique_ptr<juce::MidiInput> input_;
    std::string selectedInputIdentifier_;
    std::string selectedInputName_;
    juce::AbstractFifo eventFifo_ { queueCapacity };
    std::array<QueuedMidiInputEvent, queueCapacity> events_ {};
    std::atomic<int> droppedEvents_ { 0 };
};
}
