#pragma once

#include "core/time/Tick.h"

#include <string>

namespace tsq::core::sequencing
{
struct AudioSourceReference
{
    std::string sourceId;
    std::string filePath;
    std::string displayName;
    bool embeddedInProject = false;

    bool isValid() const noexcept;
};

class AudioClip
{
public:
    AudioClip (std::string id,
               std::string name,
               AudioSourceReference source,
               time::TickPosition startInProject,
               time::TickDuration length,
               time::TickDuration sourceOffset = {});

    const std::string& id() const noexcept;
    const std::string& name() const noexcept;
    const AudioSourceReference& source() const noexcept;
    time::TickPosition startInProject() const noexcept;
    time::TickPosition endInProject() const noexcept;
    time::TickDuration length() const noexcept;
    time::TickDuration sourceOffset() const noexcept;
    bool loopEnabled() const noexcept;
    void setLoopEnabled (bool enabled) noexcept;
    bool stretchToTempo() const noexcept;
    void setStretchToTempo (bool stretch) noexcept;
    double gainDb() const noexcept;
    void setGainDb (double gainDb);

    AudioClip withStartInProject (time::TickPosition startInProject) const;
    AudioClip withLength (time::TickDuration length) const;

private:
    std::string id_;
    std::string name_;
    AudioSourceReference source_;
    time::TickPosition startInProject_ {};
    time::TickDuration length_ {};
    time::TickDuration sourceOffset_ {};
    bool loopEnabled_ = false;
    bool stretchToTempo_ = false;
    double gainDb_ = 0.0;
};

bool audioClipsOverlap (const AudioClip& lhs, const AudioClip& rhs) noexcept;
}
