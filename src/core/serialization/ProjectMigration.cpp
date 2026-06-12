#include "core/serialization/ProjectMigration.h"

#include "core/serialization/ProjectSchemaVersion.h"

#include <stdexcept>
#include <utility>

namespace tsq::core::serialization
{
namespace
{
JsonValue endpointJson (std::string kind, std::string id = {}, std::string label = {})
{
    return JsonValue::object ({
        { "kind", JsonValue::string (std::move (kind)) },
        { "id", JsonValue::string (std::move (id)) },
        { "label", JsonValue::string (std::move (label)) },
    });
}

JsonValue defaultRoutingJson()
{
    return JsonValue::object ({
        { "audioFrom", endpointJson ("none") },
        { "audioTo", endpointJson ("master", "master", "Master") },
        { "midiFrom", endpointJson ("none") },
        { "midiTo", endpointJson ("none") },
        { "sends", JsonValue::array() },
    });
}

JsonValue defaultMixerStripJson()
{
    return JsonValue::object ({
        { "volumeDb", JsonValue::number (0.0) },
        { "linearGain", JsonValue::number (1.0) },
        { "pan", JsonValue::number (0.0) },
        { "active", JsonValue::boolean (true) },
        { "muted", JsonValue::boolean (false) },
        { "soloed", JsonValue::boolean (false) },
        { "meterSourceId", JsonValue::string ({}) },
        { "colorArgb", JsonValue::null() },
    });
}

JsonValue pluginReferenceFromLegacyInstrument (const JsonValue& pluginReference)
{
    return JsonValue::object ({
        { "pluginName", requireField (pluginReference, "pluginName") },
        { "manufacturer", requireField (pluginReference, "manufacturer") },
        { "format", requireField (pluginReference, "format") },
        { "fileOrIdentifier", requireField (pluginReference, "fileOrIdentifier") },
        { "uniqueIdentifier", requireField (pluginReference, "uniqueIdentifier") },
        { "uniqueId", requireField (pluginReference, "uniqueId") },
        { "deprecatedUid", requireField (pluginReference, "deprecatedUid") },
        { "numInputChannels", requireField (pluginReference, "numInputChannels") },
        { "numOutputChannels", requireField (pluginReference, "numOutputChannels") },
    });
}

JsonValue deviceChainFromLegacyInstrument (const JsonValue& track)
{
    const auto& pluginReference = requireField (track, "pluginReference");
    if (pluginReference.isNull())
        return JsonValue::object ({ { "slots", JsonValue::array() } });

    auto stateFile = JsonValue::string ({});
    const auto& referenceObject = requireObject (pluginReference, "legacy track plugin reference");
    if (const auto state = referenceObject.find ("pluginStateFile"); state != referenceObject.end())
        stateFile = state->second;

    JsonValue::Array slots;
    slots.push_back (JsonValue::object ({
        { "id", JsonValue::string ("instrument") },
        { "plugin", pluginReferenceFromLegacyInstrument (pluginReference) },
        { "kind", JsonValue::string ("instrument") },
        { "bypassed", JsonValue::boolean (false) },
        { "pluginStateFile", stateFile },
    }));

    return JsonValue::object ({ { "slots", JsonValue::array (std::move (slots)) } });
}

JsonValue audioAssetsJson()
{
    return JsonValue::object ({
        { "policy", JsonValue::string ("referenceOnly") },
        { "waveformCache", JsonValue::object ({
            { "directory", JsonValue::string ("waveform-cache") },
            { "entries", JsonValue::array() },
        }) },
    });
}

JsonValue migrateV1ToV2 (JsonValue projectJson)
{
    auto& object = projectJson.asObject();
    object["schemaVersion"] = JsonValue::number (static_cast<double> (currentProjectSchemaVersion));
    object["audioAssets"] = audioAssetsJson();

    auto& tracks = object.at ("tracks").asArray();
    for (auto& track : tracks)
    {
        auto& trackObject = track.asObject();
        trackObject["type"] = JsonValue::string ("midi");
        trackObject["mixerStrip"] = defaultMixerStripJson();
        trackObject["routing"] = defaultRoutingJson();
        trackObject["deviceChain"] = deviceChainFromLegacyInstrument (track);
        trackObject["audioClips"] = JsonValue::array();
        trackObject["automationLanes"] = JsonValue::array();
    }

    return projectJson;
}
}

JsonValue ProjectMigration::migrateToCurrent (JsonValue projectJson)
{
    const auto schemaVersion = requireInt (requireField (projectJson, "schemaVersion"), "schemaVersion");

    if (schemaVersion == currentProjectSchemaVersion)
        return projectJson;

    if (schemaVersion == 1)
        return migrateV1ToV2 (std::move (projectJson));

    throw std::runtime_error ("Unsupported project schema version " + std::to_string (schemaVersion));
}
}
