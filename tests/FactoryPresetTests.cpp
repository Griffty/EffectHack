#include "FactoryPresetTestSupport.h"
#include <set>
#include <iostream>

namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
void testFactoryPresets(const juce::File& scratch)
{
    using namespace effecthack;
    PresetManager manager(scratch.getChildFile("factory-copies"));
    require(factoryPresets().size() == 6, "Six useful factory tones must be available");
    std::set<juce::String> ids;
    double reference = 0;
    for (const auto& entry : factoryPresets())
    {
        const auto& preset = entry.preset;
        require(ids.insert(preset.id).second && isFactoryPreset(preset.id), "Factory IDs must be unique and recognized");
        require(entry.description.isNotEmpty() && validateRack(preset.rack).wasOk(), "Factory configurations must be valid and described");
        require(preset.rack.cabinetPath.isEmpty(), "Factories must work without external IR files");
        require(preset.rack.values[3] == 0, "Factory Master Output defaults to 0 dB");
        require(preset.rack.bypass[2] && !preset.rack.bypass[3] && !preset.rack.bypass[4], "Use amp/cabinet without stacked distortion");
        Preset read;
        require(manager.read(preset.id, read).wasOk() && read.rack.matches(preset.rack), "Factory read must return its full configuration");
        require(!manager.directory().exists(), "Reading a factory must not write user files");
        require(manager.save(preset).failed() && manager.remove(preset.id).failed()
            && manager.rename(preset, "Overwrite factory", read).failed(), "All factory mutations must be rejected");
        const auto measurement = measureFactoryPreset(preset);
        if (reference == 0) reference = measurement.rms;
        const auto difference = 20 * std::log10(measurement.rms / reference);
        require(std::abs(difference) < .7, "Factory reference-riff RMS levels must be balanced");
        require(measurement.peak < .8f && !measurement.clipped, "Reference riff must retain peak headroom");
        const auto hot = measureFactoryPreset(preset, 2);
        require(hot.peak < .8f && !hot.clipped, "Hotter reference input must not clip");
        if (preset.id == "factory.chorus-clean" || preset.id == "factory.ambient-clean")
            require(measurement.width > 1, "Spatial clean presets must retain stereo width");
        std::cout << preset.name << ": reference RMS=" << measurement.rms << ", peak=" << measurement.peak
                  << ", relative dB=" << difference << '\n';
    }
    const auto& original = factoryPresets()[0].preset;
    Preset copy;
    require(manager.saveAs("My clean", original.rack, copy).wasOk(), "Factory Save as must create a user copy");
    require(!isFactoryPreset(copy.id) && copy.rack.matches(original.rack), "Copy must use its own ID and preserve tone");
    copy.rack.values[0] = -4;
    require(manager.save(copy).wasOk(), "Factory copies must be editable");
    require(factoryPresets()[0].preset.rack.matches(original.rack), "User edits must leave built-ins intact");
    require(manager.remove(copy.id).wasOk(), "Factory copies must be deletable");
}
