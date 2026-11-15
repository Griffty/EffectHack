#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ToneMatching.h"

namespace effecthack
{
/// Song-reference UI and cancellable worker request lifetime. It delegates final
/// rack application to its owner rather than mutating AudioEngine directly.
class SongPresetPanel final : public juce::Component
{
public:
    using Completion = std::function<void(juce::Result)>;
    using ApplySelection = std::function<void(const SongRecommendation&, const Cancellation&, Completion)>;
    using ConfigProvider = std::function<ApiConfig()>;
    using InputContext = std::function<juce::var()>;
    SongPresetPanel(std::vector<SelectionPreset>, ApplySelection, HttpTransport = performHttp, ConfigProvider = loadApiConfig, InputContext = {});
    ~SongPresetPanel() override;
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff1c242d)); }
    void resized() override;
private:
    juce::TooltipWindow tooltips { this, 600 };
    std::vector<SelectionPreset> catalog;
    ApplySelection apply;
    HttpTransport transport;
    ConfigProvider configuration;
    InputContext inputContext;
    juce::ToggleButton custom { "Custom" };
    juce::ToggleButton preserveMaster { "Keep master" };
    juce::ComboBox source, part;
    juce::TextEditor startTime, endTime;
    juce::Label fromLabel, toLabel, sourceBadge;
    juce::TextButton browse { "Browse..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File referenceFile;
    juce::TextEditor link;
    juce::TextButton find { "Find tone" }, cancel { "Cancel" };
    juce::Label title, status;
    double progress = -1;
    juce::ProgressBar activity { progress };
    juce::ThreadPool worker { 1, 0, juce::Thread::Priority::low };
    Cancellation cancellation;
    unsigned requestNumber = 0;
    bool busy = false;
    void start();
    void setBusy(bool);
    void finish(const SongRecommendation&);
    void sourceChanged();
    void chooseReference();
};
}
