#include "core/sequencing/Routing.h"

#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
double validatedNormalizedLevel (double value)
{
    if (std::isnan (value) || value < 0.0 || value > 1.0)
        throw std::invalid_argument ("Send level must be normalized between 0 and 1");

    return value;
}

bool trackExists (const Project& project, const std::string& trackId)
{
    return project.findTrackById (trackId) != nullptr;
}

bool returnTrackExists (const Project& project, const std::string& trackId)
{
    const auto* track = project.findTrackById (trackId);
    return track != nullptr && track->type() == TrackType::returnTrack;
}

bool routeTargetExists (const Project& project, const RouteEndpoint& endpoint)
{
    switch (endpoint.kind)
    {
        case RouteEndpointKind::none:
        case RouteEndpointKind::master:
        case RouteEndpointKind::hardwareOutput:
        case RouteEndpointKind::sidechain:
            return true;
        case RouteEndpointKind::track:
            return trackExists (project, endpoint.id);
        case RouteEndpointKind::returnTrack:
            return returnTrackExists (project, endpoint.id);
    }

    return true;
}

std::vector<std::string> returnTargetsFor (const Track& track)
{
    std::vector<std::string> targets;

    const auto& audioTo = track.routing().audioTo();
    if (audioTo.kind == RouteEndpointKind::returnTrack)
        targets.push_back (audioTo.id);

    for (const auto& send : track.routing().sends())
        if (send.normalizedLevel > 0.0)
            targets.push_back (send.targetReturnTrackId);

    return targets;
}

std::optional<std::string> graphAudioTargetFor (const Track& track)
{
    const auto& audioTo = track.routing().audioTo();
    if (audioTo.kind == RouteEndpointKind::track || audioTo.kind == RouteEndpointKind::returnTrack)
        return audioTo.id;

    return std::nullopt;
}

bool reachesReturnTarget (const Project& project,
                          const Track& source,
                          const std::string& targetReturnTrackId,
                          std::set<std::string>& visiting)
{
    for (const auto& returnTrackId : returnTargetsFor (source))
    {
        if (returnTrackId == targetReturnTrackId)
            return true;

        if (! visiting.insert (returnTrackId).second)
            continue;

        const auto* returnTrack = project.findTrackById (returnTrackId);
        if (returnTrack != nullptr
            && returnTrack->type() == TrackType::returnTrack
            && reachesReturnTarget (project, *returnTrack, targetReturnTrackId, visiting))
        {
            return true;
        }
    }

    return false;
}
}

RouteEndpoint RouteEndpoint::none()
{
    return {};
}

RouteEndpoint RouteEndpoint::track (std::string trackId)
{
    if (trackId.empty())
        throw std::invalid_argument ("Track route endpoint requires a track ID");

    return RouteEndpoint { RouteEndpointKind::track, std::move (trackId), {} };
}

RouteEndpoint RouteEndpoint::returnTrack (std::string trackId)
{
    if (trackId.empty())
        throw std::invalid_argument ("Return route endpoint requires a track ID");

    return RouteEndpoint { RouteEndpointKind::returnTrack, std::move (trackId), {} };
}

RouteEndpoint RouteEndpoint::master()
{
    return RouteEndpoint { RouteEndpointKind::master, "master", "Master" };
}

RouteEndpoint RouteEndpoint::hardwareOutput (std::string outputName)
{
    if (outputName.empty())
        throw std::invalid_argument ("Hardware output route requires a name");

    return RouteEndpoint { RouteEndpointKind::hardwareOutput, std::move (outputName), {} };
}

RouteEndpoint RouteEndpoint::sidechain (std::string sourceId)
{
    if (sourceId.empty())
        throw std::invalid_argument ("Sidechain route requires a source ID");

    return RouteEndpoint { RouteEndpointKind::sidechain, std::move (sourceId), {} };
}

bool RouteEndpoint::referencesTrack() const noexcept
{
    return kind == RouteEndpointKind::track || kind == RouteEndpointKind::returnTrack;
}

bool RouteEndpoint::empty() const noexcept
{
    return kind == RouteEndpointKind::none;
}

bool operator== (const RouteEndpoint& lhs, const RouteEndpoint& rhs) noexcept
{
    return lhs.kind == rhs.kind && lhs.id == rhs.id && lhs.label == rhs.label;
}

bool operator!= (const RouteEndpoint& lhs, const RouteEndpoint& rhs) noexcept
{
    return ! (lhs == rhs);
}

ReturnSend::ReturnSend (std::string target, double level)
    : targetReturnTrackId (std::move (target)),
      normalizedLevel (validatedNormalizedLevel (level))
{
    if (targetReturnTrackId.empty())
        throw std::invalid_argument ("Return send requires a target return track ID");
}

const RouteEndpoint& TrackRouting::audioFrom() const noexcept
{
    return audioFrom_;
}

const RouteEndpoint& TrackRouting::audioTo() const noexcept
{
    return audioTo_;
}

const RouteEndpoint& TrackRouting::midiFrom() const noexcept
{
    return midiFrom_;
}

const RouteEndpoint& TrackRouting::midiTo() const noexcept
{
    return midiTo_;
}

const std::vector<ReturnSend>& TrackRouting::sends() const noexcept
{
    return sends_;
}

void TrackRouting::setAudioFrom (RouteEndpoint endpoint)
{
    audioFrom_ = std::move (endpoint);
}

void TrackRouting::setAudioTo (RouteEndpoint endpoint)
{
    audioTo_ = std::move (endpoint);
}

void TrackRouting::setMidiFrom (RouteEndpoint endpoint)
{
    midiFrom_ = std::move (endpoint);
}

void TrackRouting::setMidiTo (RouteEndpoint endpoint)
{
    midiTo_ = std::move (endpoint);
}

void TrackRouting::addOrReplaceSend (ReturnSend send)
{
    const auto match = std::find_if (sends_.begin(), sends_.end(), [&send] (const auto& existing) {
        return existing.targetReturnTrackId == send.targetReturnTrackId;
    });

    if (match == sends_.end())
        sends_.push_back (std::move (send));
    else
        *match = std::move (send);
}

ReturnSend TrackRouting::removeSend (const std::string& targetReturnTrackId)
{
    const auto match = std::find_if (sends_.begin(), sends_.end(), [&targetReturnTrackId] (const auto& send) {
        return send.targetReturnTrackId == targetReturnTrackId;
    });

    if (match == sends_.end())
        throw std::invalid_argument ("Track routing does not contain a send to this return track");

    auto removed = *match;
    sends_.erase (match);
    return removed;
}

bool RoutingValidationResult::valid() const noexcept
{
    return errors.empty();
}

std::string RoutingValidationResult::summary() const
{
    std::string result;
    for (const auto& error : errors)
    {
        if (! result.empty())
            result += "; ";

        result += error;
    }

    return result;
}

RoutingValidationResult validateProjectRouting (const Project& project)
{
    RoutingValidationResult result;
    std::map<std::string, std::vector<std::string>> graph;

    for (const auto& track : project.tracks())
    {
        const auto& routing = track.routing();

        if (routing.audioTo().referencesTrack() && routing.audioTo().id == track.id())
            result.errors.push_back ("Track '" + track.name() + "' cannot route audio to itself");

        if (! routeTargetExists (project, routing.audioTo()))
            result.errors.push_back ("Track '" + track.name() + "' routes audio to a missing or invalid target");

        if (routing.audioFrom().referencesTrack())
        {
            if (routing.audioFrom().id == track.id())
                result.errors.push_back ("Track '" + track.name() + "' cannot receive audio from itself");
            else if (! routeTargetExists (project, routing.audioFrom()))
                result.errors.push_back ("Track '" + track.name() + "' receives audio from a missing or invalid target");
            else
                graph[routing.audioFrom().id].push_back (track.id());
        }

        if (routing.midiFrom().referencesTrack() && ! routeTargetExists (project, routing.midiFrom()))
            result.errors.push_back ("Track '" + track.name() + "' receives MIDI from a missing target");

        if (routing.midiTo().referencesTrack() && ! routeTargetExists (project, routing.midiTo()))
            result.errors.push_back ("Track '" + track.name() + "' routes MIDI to a missing target");

        for (const auto& send : routing.sends())
        {
            if (! returnTrackExists (project, send.targetReturnTrackId))
                result.errors.push_back ("Track '" + track.name() + "' has a send to a missing or non-return track");
            else if (send.normalizedLevel > 0.0)
            {
                if (send.targetReturnTrackId == track.id())
                    result.errors.push_back ("Track '" + track.name() + "' cannot send to itself");

                graph[track.id()].push_back (send.targetReturnTrackId);
            }
        }

        if (const auto target = graphAudioTargetFor (track))
            graph[track.id()].push_back (*target);
    }

    std::set<std::string> visited;
    std::set<std::string> visiting;

    std::function<void (const std::string&)> visit = [&] (const std::string& trackId)
    {
        if (visited.find (trackId) != visited.end())
            return;

        if (visiting.find (trackId) != visiting.end())
        {
            result.errors.push_back ("Audio routing contains a cycle involving track '" + trackId + "'");
            return;
        }

        visiting.insert (trackId);
        if (const auto targets = graph.find (trackId); targets != graph.end())
            for (const auto& target : targets->second)
                visit (target);

        visiting.erase (trackId);
        visited.insert (trackId);
    };

    for (const auto& [source, target] : graph)
    {
        (void) target;
        visit (source);
    }

    std::sort (result.errors.begin(), result.errors.end());
    result.errors.erase (std::unique (result.errors.begin(), result.errors.end()), result.errors.end());
    return result;
}

bool returnTrackIsRequiredForSoloPath (const Project& project, const std::string& returnTrackId)
{
    const auto* targetReturn = project.findTrackById (returnTrackId);
    if (targetReturn == nullptr || targetReturn->type() != TrackType::returnTrack)
        return false;

    for (const auto& track : project.tracks())
    {
        if (! track.mixerStrip().soloed() || track.id() == returnTrackId)
            continue;

        std::set<std::string> visiting;
        if (reachesReturnTarget (project, track, returnTrackId, visiting))
            return true;
    }

    return false;
}
}
