#include "app/AppSettings.h"

namespace tsq::app
{
bool AppSettings::hasAudioDeviceState() const noexcept
{
    return ! audioDeviceStateXml.empty();
}

bool AppSettings::hasOutputDeviceChoice() const noexcept
{
    return ! outputDeviceName.empty();
}

bool AppSettings::hasSelectedTestInstrument() const noexcept
{
    return ! selectedTestInstrumentIdentifier.empty();
}
}
