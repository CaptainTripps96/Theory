#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::sequencing
{
class Project;

enum class RouteEndpointKind
{
    none,
    track,
    returnTrack,
    master,
    hardwareOutput,
    sidechain
};

struct RouteEndpoint
{
    RouteEndpointKind kind = RouteEndpointKind::none;
    std::string id;
    std::string label;

    static RouteEndpoint none();
    static RouteEndpoint track (std::string trackId);
    static RouteEndpoint returnTrack (std::string trackId);
    static RouteEndpoint master();
    static RouteEndpoint hardwareOutput (std::string outputName);
    static RouteEndpoint sidechain (std::string sourceId);

    bool referencesTrack() const noexcept;
    bool empty() const noexcept;
};

bool operator== (const RouteEndpoint& lhs, const RouteEndpoint& rhs) noexcept;
bool operator!= (const RouteEndpoint& lhs, const RouteEndpoint& rhs) noexcept;

struct ReturnSend
{
    std::string targetReturnTrackId;
    double normalizedLevel = 0.0;

    ReturnSend() = default;
    ReturnSend (std::string targetReturnTrackId, double normalizedLevel);
};

class TrackRouting
{
public:
    const RouteEndpoint& audioFrom() const noexcept;
    const RouteEndpoint& audioTo() const noexcept;
    const RouteEndpoint& midiFrom() const noexcept;
    const RouteEndpoint& midiTo() const noexcept;
    const std::vector<ReturnSend>& sends() const noexcept;

    void setAudioFrom (RouteEndpoint endpoint);
    void setAudioTo (RouteEndpoint endpoint);
    void setMidiFrom (RouteEndpoint endpoint);
    void setMidiTo (RouteEndpoint endpoint);
    void addOrReplaceSend (ReturnSend send);
    ReturnSend removeSend (const std::string& targetReturnTrackId);

private:
    RouteEndpoint audioFrom_ = RouteEndpoint::none();
    RouteEndpoint audioTo_ = RouteEndpoint::master();
    RouteEndpoint midiFrom_ = RouteEndpoint::none();
    RouteEndpoint midiTo_ = RouteEndpoint::none();
    std::vector<ReturnSend> sends_;
};

struct RoutingValidationResult
{
    std::vector<std::string> errors;

    bool valid() const noexcept;
    std::string summary() const;
};

RoutingValidationResult validateProjectRouting (const Project& project);
bool returnTrackIsRequiredForSoloPath (const Project& project, const std::string& returnTrackId);
}
