#pragma once

#include "core/sequencing/MidiClip.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tsq::core::midi
{
struct MidiImportOptions
{
    std::string clipId;
    std::string clipName;
    time::TickPosition startInProject {};
};

struct MidiImportResult
{
    sequencing::MidiClip clip;
    int sourcePulsesPerQuarterNote = 0;
    std::size_t importedNoteCount = 0;
    std::size_t sourceTrackCount = 0;
};

class MidiImporter
{
public:
    static MidiImportResult importClipFromBytes (const std::vector<std::uint8_t>& bytes,
                                                 MidiImportOptions options);

    static MidiImportResult importClipFromFile (const std::filesystem::path& filePath,
                                                MidiImportOptions options);
};
}
