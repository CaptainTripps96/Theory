#pragma once

#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/Project.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::sequencing
{
enum class ExpressionRouteMappingCurve
{
    linear
};

enum class ExpressionRouteSmoothingPolicy
{
    none
};

struct ExpressionDestinationRuntimeSupport
{
    bool playbackMapped = false;
    bool plainMidiExportMapped = false;
    std::string statusLabel;
    std::string detailText;
};

struct ExpressionDestinationMetadata
{
    ExpressionDestination destination;
    std::string stableId;
    std::string displayName;
    std::string detailText;
    double defaultOutputMin = 0.0;
    double defaultOutputMax = 1.0;
    std::string units;
    bool discrete = false;
    bool available = false;
    bool expressionTarget = false;
    bool playbackMapped = false;
    bool plainMidiExportMapped = false;
    std::string supportLabel;
    std::string supportDetailText;
};

std::string expressionDestinationKindId (ExpressionDestinationKind kind);
ExpressionDestinationKind expressionDestinationKindFromId (const std::string& id);

double mapExpressionRouteValue (const ExpressionRoute& route,
                                double laneValue,
                                ExpressionLanePolarity polarity,
                                ExpressionRouteMappingCurve curve = ExpressionRouteMappingCurve::linear) noexcept;

ExpressionRouteSmoothingPolicy defaultExpressionRouteSmoothingPolicy (const ExpressionDestination& destination) noexcept;
ExpressionDestinationRuntimeSupport expressionDestinationRuntimeSupport (const ExpressionDestination& destination);

bool expressionDestinationIsAvailable (const Project& project, const ExpressionDestination& destination);
ExpressionDestinationMetadata expressionDestinationMetadata (const Project& project, const ExpressionDestination& destination);
std::vector<ExpressionDestinationMetadata> expressionDestinationMetadataForTrack (const Project& project, const Track& track);

const devices::FirstPartyParameterDefinition* findFirstPartyExpressionTarget (const DeviceSlot& slot,
                                                                              const std::string& parameterId);
}
