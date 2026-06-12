#pragma once

#include "core/sequencing/Automation.h"
#include "core/time/Tick.h"

#include <vector>

namespace tsq::core::sequencing
{
class Project;

struct AutomationPlaybackValue
{
    AutomationTarget target;
    double normalizedValue = 0.0;
};

struct AutomationPlaybackSnapshot
{
    time::TickPosition position {};
    std::vector<AutomationPlaybackValue> values;
};

double automationValueFromVolumeDb (double volumeDb);
double volumeDbFromAutomationValue (double normalizedValue);
double automationValueFromPan (double pan);
double panFromAutomationValue (double normalizedValue);
bool automationTargetIsAvailable (const Project& project, const AutomationTarget& target);
double defaultAutomationValueForTarget (const Project& project, const AutomationTarget& target);
AutomationPlaybackSnapshot automationPlaybackSnapshotAt (const Project& project, time::TickPosition position);
}
