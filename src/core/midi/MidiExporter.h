#pragma once

#include "core/diagnostics/Result.h"
#include "core/midi/MidiExportOptions.h"
#include "core/sequencing/MidiClip.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tsq::core::midi
{
class MidiExporter
{
public:
    static std::vector<std::uint8_t> exportClipToBytes (const sequencing::MidiClip& clip,
                                                        const MidiExportOptions& options = {});

    static void exportClipToFile (const sequencing::MidiClip& clip,
                                  const std::filesystem::path& filePath,
                                  const MidiExportOptions& options = {});

    static diagnostics::Result tryExportClipToFile (const sequencing::MidiClip& clip,
                                                    const std::filesystem::path& filePath,
                                                    const MidiExportOptions& options = {});
};
}
