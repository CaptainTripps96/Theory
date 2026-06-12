#include "app/MidiInputRecordingService.h"

#include <utility>

namespace tsq::app
{
namespace
{
std::string toStdString (const juce::String& text)
{
    return text.toStdString();
}
}

MidiInputRecordingService::MidiInputRecordingService() = default;

MidiInputRecordingService::~MidiInputRecordingService()
{
    closeInputDevice();
}

std::vector<MidiInputDeviceInfo> MidiInputRecordingService::availableInputDevices() const
{
    std::vector<MidiInputDeviceInfo> result;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        const auto name = toStdString (device.name);
        const auto identifier = toStdString (device.identifier);
        result.push_back (MidiInputDeviceInfo {
            identifier,
            name,
            name.empty() ? identifier : name
        });
    }

    return result;
}

bool MidiInputRecordingService::openInputDevice (const std::string& identifier)
{
    closeInputDevice();

    input_ = juce::MidiInput::openDevice (juce::String::fromUTF8 (identifier.c_str()), this);
    if (input_ == nullptr)
        return false;

    const auto info = input_->getDeviceInfo();
    selectedInputIdentifier_ = toStdString (info.identifier);
    selectedInputName_ = toStdString (info.name);
    input_->start();
    return true;
}

void MidiInputRecordingService::closeInputDevice()
{
    if (input_ != nullptr)
    {
        input_->stop();
        input_.reset();
    }

    selectedInputIdentifier_.clear();
    selectedInputName_.clear();
    eventFifo_.reset();
}

bool MidiInputRecordingService::hasOpenInputDevice() const noexcept
{
    return input_ != nullptr;
}

const std::string& MidiInputRecordingService::selectedInputIdentifier() const noexcept
{
    return selectedInputIdentifier_;
}

const std::string& MidiInputRecordingService::selectedInputName() const noexcept
{
    return selectedInputName_;
}

int MidiInputRecordingService::droppedEventCount() const noexcept
{
    return droppedEvents_.load (std::memory_order_relaxed);
}

std::vector<QueuedMidiInputEvent> MidiInputRecordingService::drainEvents()
{
    std::vector<QueuedMidiInputEvent> result;

    auto start1 = 0;
    auto size1 = 0;
    auto start2 = 0;
    auto size2 = 0;
    eventFifo_.prepareToRead (queueCapacity, start1, size1, start2, size2);

    result.reserve (static_cast<std::size_t> (size1 + size2));
    for (auto index = 0; index < size1; ++index)
        result.push_back (events_[static_cast<std::size_t> (start1 + index)]);

    for (auto index = 0; index < size2; ++index)
        result.push_back (events_[static_cast<std::size_t> (start2 + index)]);

    eventFifo_.finishedRead (size1 + size2);
    return result;
}

void MidiInputRecordingService::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // MIDI device callback path. Keep this allocation-free and lock-free:
    // copy primitive note data into the fixed SPSC FIFO and let AppServices
    // perform sorting, theory analysis, commands, logging, and UI work later.
    if (! message.isNoteOnOrOff())
        return;

    auto event = QueuedMidiInputEvent {};
    event.type = message.isNoteOn() ? QueuedMidiInputEvent::Type::noteOn : QueuedMidiInputEvent::Type::noteOff;
    event.channel = message.getChannel();
    event.noteNumber = message.getNoteNumber();
    event.velocity = static_cast<int> (message.getVelocity());
    event.timestampSeconds = message.getTimeStamp();

    auto start1 = 0;
    auto size1 = 0;
    auto start2 = 0;
    auto size2 = 0;
    eventFifo_.prepareToWrite (1, start1, size1, start2, size2);

    if (size1 <= 0)
    {
        droppedEvents_.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    events_[static_cast<std::size_t> (start1)] = event;
    eventFifo_.finishedWrite (1);
}
}
