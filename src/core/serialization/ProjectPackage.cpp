#include "core/serialization/ProjectPackage.h"

#include "core/serialization/ProjectSerializer.h"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace tsq::core::serialization
{
namespace
{
void createPackageDirectories (const std::filesystem::path& packagePath)
{
    std::filesystem::create_directories (packagePath);
    std::filesystem::create_directories (packagePath / "plugin-states");
    std::filesystem::create_directories (packagePath / "assets");
    std::filesystem::create_directories (packagePath / "exports");
    std::filesystem::create_directories (packagePath / "waveform-cache");
}

bool isSafeRelativePath (const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute())
        return false;

    for (const auto& part : path)
        if (part == "..")
            return false;

    return true;
}

void createPluginStatePlaceholders (const sequencing::Project& project, const std::filesystem::path& packagePath)
{
    for (const auto& track : project.tracks())
    {
        std::vector<std::string> stateFiles;
        if (track.instrument().has_value() && ! track.instrument()->pluginStateFile.empty())
            stateFiles.push_back (track.instrument()->pluginStateFile);

        for (const auto& slot : track.deviceChain().slots())
            if (! slot.pluginStateFile().empty())
                stateFiles.push_back (slot.pluginStateFile());

        for (const auto& stateFile : stateFiles)
        {
            const std::filesystem::path relativePath { stateFile };
            if (! isSafeRelativePath (relativePath))
                continue;

            const auto statePath = packagePath / relativePath;
            std::filesystem::create_directories (statePath.parent_path());

            if (! std::filesystem::exists (statePath))
            {
                std::ofstream placeholder { statePath, std::ios::binary };
                if (! placeholder)
                    throw std::runtime_error ("Could not create plugin state placeholder");
            }
        }
    }
}

void addPluginStateWarning (std::vector<std::string>& warnings,
                            std::set<std::string>& seenStateFiles,
                            const std::filesystem::path& packagePath,
                            const std::string& trackName,
                            const std::string& stateFile)
{
    if (stateFile.empty() || ! seenStateFiles.insert (stateFile).second)
        return;

    const std::filesystem::path relativePath { stateFile };
    if (! isSafeRelativePath (relativePath))
    {
        warnings.push_back ("Unsafe plugin state path on track '" + trackName + "': " + stateFile);
        return;
    }

    if (! std::filesystem::is_regular_file (packagePath / relativePath))
        warnings.push_back ("Missing plugin state file on track '" + trackName + "': " + stateFile);
}

void addAudioSourceWarnings (std::vector<std::string>& warnings,
                             const std::filesystem::path& packagePath,
                             const sequencing::Project& project)
{
    for (const auto& track : project.tracks())
    {
        for (const auto& clip : track.audioClips())
        {
            const auto& source = clip.source();
            const std::filesystem::path sourcePath { source.filePath };
            const auto resolvedPath = sourcePath.is_absolute() ? sourcePath : packagePath / sourcePath;

            if (! std::filesystem::is_regular_file (resolvedPath))
            {
                warnings.push_back ("Missing audio source for clip '" + clip.name()
                                   + "' on track '" + track.name() + "': " + source.filePath);
            }
        }
    }
}

void addPackageWarnings (std::vector<std::string>& warnings,
                         const std::filesystem::path& packagePath,
                         const sequencing::Project& project)
{
    std::set<std::string> seenStateFiles;
    for (const auto& track : project.tracks())
    {
        if (track.instrument().has_value())
            addPluginStateWarning (warnings, seenStateFiles, packagePath, track.name(), track.instrument()->pluginStateFile);

        for (const auto& slot : track.deviceChain().slots())
            addPluginStateWarning (warnings, seenStateFiles, packagePath, track.name(), slot.pluginStateFile());
    }

    addAudioSourceWarnings (warnings, packagePath, project);
}
}

void ProjectPackage::save (const sequencing::Project& project, const std::filesystem::path& packagePath)
{
    createPackageDirectories (packagePath);
    createPluginStatePlaceholders (project, packagePath);

    std::ofstream stream { projectJsonPath (packagePath) };
    if (! stream)
        throw std::runtime_error ("Could not open project.json for writing");

    stream << ProjectSerializer::serialize (project);
}

sequencing::Project ProjectPackage::load (const std::filesystem::path& packagePath)
{
    return std::move (loadWithWarnings (packagePath).project);
}

ProjectLoadResult ProjectPackage::loadWithWarnings (const std::filesystem::path& packagePath)
{
    std::ifstream stream { projectJsonPath (packagePath) };
    if (! stream)
        throw std::runtime_error ("Could not open project.json for reading");

    std::ostringstream content;
    content << stream.rdbuf();
    auto result = ProjectSerializer::deserializeWithWarnings (content.str());
    addPackageWarnings (result.warnings, packagePath, result.project);
    return result;
}

std::filesystem::path ProjectPackage::projectJsonPath (const std::filesystem::path& packagePath)
{
    return packagePath / "project.json";
}
}
