#include "SongPresetPanel.h"

namespace effecthack
{
SongPresetPanel::SongPresetPanel(std::vector<SelectionPreset> choices, ApplySelection callback, HttpTransport http, ConfigProvider config, InputContext context)
    : catalog(std::move(choices)), apply(std::move(callback)), transport(std::move(http)), configuration(std::move(config)), inputContext(std::move(context))
{
    setOpaque(true);
    for (auto* component : std::initializer_list<juce::Component*> { &link, &find, &cancel, &title, &status, &activity, &custom,
        &preserveMaster, &source, &part, &startTime, &endTime, &fromLabel, &toLabel, &browse, &sourceBadge }) addAndMakeVisible(component);
    source.addItem("YouTube", 1); source.addItem("Audio reference", 2); source.addItem("Artist + song", 3);
    source.setSelectedId(1, juce::dontSendNotification); source.setTooltip("A guitar-only reference recording gives the clearest target. Public YouTube and full-song audio also work. Artist + song uses a clearly labelled knowledge-based estimate.");
    part.addItem("Main rhythm", 1); part.addItem("Lead", 2); part.addItem("Intro", 3);
    part.setSelectedId(1, juce::dontSendNotification); part.setTooltip("Generate one tone for this guitar part. An explicit section takes priority over the song's other sections.");
    fromLabel.setText("From", juce::dontSendNotification); toLabel.setText("To", juce::dontSendNotification);
    for (auto* editor : { &startTime, &endTime })
    {
        editor->setTextToShowWhenEmpty("Auto", juce::Colour(0xff9aa9b7)); editor->setInputRestrictions(10, "0123456789:.");
        editor->setTooltip("Optional seconds or m:ss. Leave both empty to analyze the song. A From time alone selects 30 seconds; maximum explicit section is six minutes.");
    }
    link.setName("Song reference"); link.onReturnKey = [this] { if (!busy) start(); };
    source.onChange = [this] { sourceChanged(); };
    browse.onClick = [this] { chooseReference(); };
    find.onClick = [this] { start(); };
    custom.setToggleState(true, juce::dontSendNotification);
    custom.setTooltip("Create a custom tone around a rendered processor example. Off: select one unchanged factory preset.");
    preserveMaster.setToggleState(true, juce::dontSendNotification);
    preserveMaster.setTooltip("Keep your current Input trim and Output listening level. Tone loudness is balanced internally once, before application. Off: use Input 0 dB and Output 0 dB.");
    custom.onClick = [this] { find.setButtonText(custom.getToggleState() ? "Match tone" : "Find preset"); preserveMaster.setEnabled(!busy && custom.getToggleState()); };
    custom.onClick();
    cancel.onClick = [this]
    {
        if (cancellation != nullptr) cancellation->store(true);
        ++requestNumber; setBusy(false);
        status.setText("Cancelled", juce::dontSendNotification); status.setColour(juce::Label::textColourId, juce::Colour(0xff9aa9b7));
    };
    title.setColour(juce::Label::textColourId, juce::Colour(0xffedf2f6)); title.setFont(juce::FontOptions(16));
    status.setFont(juce::FontOptions(13)); status.setMinimumHorizontalScale(1);
    sourceBadge.setFont(juce::FontOptions(13)); sourceBadge.setJustificationType(juce::Justification::centredRight);
    activity.setPercentageDisplay(false);
    sourceChanged(); setBusy(false); setSize(750, 235);
}
SongPresetPanel::~SongPresetPanel()
{
    if (cancellation != nullptr) cancellation->store(true);
    worker.removeAllJobs(true, -1);
}
void SongPresetPanel::sourceChanged()
{
    const auto audio = source.getSelectedId() == 2, named = source.getSelectedId() == 3;
    link.setReadOnly(audio); browse.setVisible(audio);
    if (audio) link.setText(referenceFile.existsAsFile() ? referenceFile.getFileName() : juce::String(), false);
    else link.clear();
    link.setTextToShowWhenEmpty(audio ? "Choose a guitar clip or song recording" : named ? "Artist - song title" : "Public YouTube link", juce::Colour(0xff9aa9b7));
    link.setTooltip(audio ? "The selected audio section is converted to mono and uploaded to Gemini for tone analysis. A guitar-only clip is best; a full song is also supported. Nothing is played automatically."
        : named ? "Uses musical knowledge, without a target recording. Results are labelled Song estimate."
        : "The public video is passed to Gemini for audio analysis. Optional From/To selects the guitar section. Synthetic examples from this processor are also sent for comparison.");
    startTime.setEnabled(!named && !busy); endTime.setEnabled(!named && !busy);
    resized();
}
void SongPresetPanel::chooseReference()
{
    chooser = std::make_unique<juce::FileChooser>("Choose a reference recording", referenceFile, "*.wav;*.flac;*.mp3;*.aiff;*.aif;*.ogg");
    auto safe = juce::Component::SafePointer<SongPresetPanel>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& selected)
        {
            if (safe == nullptr) return;
            const auto file = selected.getResult();
            if (file.existsAsFile()) { safe->referenceFile = file; if (safe->source.getSelectedId() == 2) safe->link.setText(file.getFileName(), false); }
        });
}
void SongPresetPanel::setBusy(bool value)
{
    busy = value; find.setEnabled(!value); link.setEnabled(!value); source.setEnabled(!value); part.setEnabled(!value);
    browse.setEnabled(!value); custom.setEnabled(!value); preserveMaster.setEnabled(!value && custom.getToggleState());
    startTime.setEnabled(!value && source.getSelectedId() != 3); endTime.setEnabled(!value && source.getSelectedId() != 3);
    cancel.setEnabled(value); cancel.setVisible(value); activity.setVisible(value);
}
void SongPresetPanel::start()
{
    if (busy) return;
    ToneMatchInput input;
    input.source = source.getSelectedId() == 2 ? ToneSource::audioFile : source.getSelectedId() == 3 ? ToneSource::songName : ToneSource::youtube;
    input.query = link.getText(); input.file = referenceFile;
    input.part = part.getSelectedId() == 2 ? GuitarPart::lead : part.getSelectedId() == 3 ? GuitarPart::intro : GuitarPart::rhythm;
    input.custom = custom.getToggleState(); input.preserveMaster = preserveMaster.getToggleState();
    if (input.source != ToneSource::songName && (!parseReferenceTime(startTime.getText(), input.startSeconds) || !parseReferenceTime(endTime.getText(), input.endSeconds)))
    { status.setText("Use seconds or m:ss for the section.", juce::dontSendNotification); status.setColour(juce::Label::textColourId, juce::Colours::salmon); return; }
    const auto levels = inputContext ? inputContext() : juce::var();
    if (levels.hasProperty("master_input_db")) input.inputDb = static_cast<float>(levels["master_input_db"]);
    if (levels.hasProperty("master_output_db")) input.outputDb = static_cast<float>(levels["master_output_db"]);
    if (auto valid = validateToneMatchInput(input); valid.failed())
    { status.setText(valid.getErrorMessage(), juce::dontSendNotification); status.setTooltip(valid.getErrorMessage()); status.setColour(juce::Label::textColourId, juce::Colours::salmon); return; }
    const auto serial = ++requestNumber;
    cancellation = std::make_shared<std::atomic<bool>>(false);
    const auto token = cancellation;
    progress = -1; setBusy(true); title.setText({}, juce::dontSendNotification); sourceBadge.setText({}, juce::dontSendNotification);
    status.setText("Preparing reference", juce::dontSendNotification); status.setColour(juce::Label::textColourId, juce::Colour(0xff9aa9b7));
    auto safe = juce::Component::SafePointer<SongPresetPanel>(this);
    worker.addJob([safe, serial, input, token, http = transport, config = configuration]
    {
        SongRecommendation result;
        try
        {
            result = matchSongTone(input, config(), http, token, [safe, serial](const juce::String& text, double value)
            {
                juce::MessageManager::callAsync([safe, serial, text, value]
                {
                    if (safe == nullptr || serial != safe->requestNumber) return;
                    safe->progress = value; safe->status.setText(text, juce::dontSendNotification);
                });
            });
        }
        catch (const std::exception&) { result.error = "Could not prepare this song tone. Try another reference."; }
        juce::MessageManager::callAsync([safe, serial, result = std::move(result)]
        {
            if (safe == nullptr || serial != safe->requestNumber) return;
            safe->setBusy(false); safe->finish(result);
        });
    });
}
void SongPresetPanel::finish(const SongRecommendation& result)
{
    title.setText(result.video.title, juce::dontSendNotification); title.setTooltip(result.details);
    sourceBadge.setText(result.sourceLabel, juce::dontSendNotification);
    sourceBadge.setColour(juce::Label::textColourId, result.sourceLabel == "Audio reference" ? juce::Colour(0xff69d5ba) : juce::Colour(0xffffa44d));
    sourceBadge.setTooltip(result.details);
    auto error = result.error;
    const auto selected = std::find_if(catalog.begin(), catalog.end(), [&result](const auto& preset) { return preset.id == result.presetId; });
    if (error.isEmpty() && (result.tone.isObject() || selected != catalog.end()))
    {
        const auto name = result.tone.isObject() ? result.tone["name"].toString() : selected->name;
        const auto description = result.details + "\nSave as to keep this tone; Revert restores the previous rack.";
        const auto serial = requestNumber;
        auto safe = juce::Component::SafePointer<SongPresetPanel>(this);
        progress = -1; setBusy(true); status.setText("Checking headroom", juce::dontSendNotification);
        apply(result, cancellation, [safe, serial, name, description](juce::Result applied)
        {
            if (safe == nullptr || serial != safe->requestNumber) return;
            safe->setBusy(false);
            safe->status.setText(applied.wasOk() ? name : applied.getErrorMessage(), juce::dontSendNotification);
            safe->status.setTooltip(applied.wasOk() ? description : applied.getErrorMessage());
            safe->status.setColour(juce::Label::textColourId, applied.wasOk() ? juce::Colour(0xff69d5ba) : juce::Colours::salmon);
        });
        return;
    }
    if (error.isEmpty()) error = "No supported tone was returned.";
    status.setText(error, juce::dontSendNotification); status.setTooltip(error);
    status.setColour(juce::Label::textColourId, juce::Colours::salmon);
}
void SongPresetPanel::resized()
{
    auto area = getLocalBounds().reduced(16);
    auto row = area.removeFromTop(34);
    source.setBounds(row.removeFromLeft(142)); row.removeFromLeft(8);
    if (source.getSelectedId() == 2) { browse.setBounds(row.removeFromRight(82)); row.removeFromRight(8); }
    link.setBounds(row);
    area.removeFromTop(10); row = area.removeFromTop(30);
    part.setBounds(row.removeFromLeft(150)); row.removeFromLeft(10);
    fromLabel.setBounds(row.removeFromLeft(37)); startTime.setBounds(row.removeFromLeft(70)); row.removeFromLeft(8);
    toLabel.setBounds(row.removeFromLeft(23)); endTime.setBounds(row.removeFromLeft(70)); row.removeFromLeft(12);
    custom.setBounds(row.removeFromLeft(88)); preserveMaster.setBounds(row);
    area.removeFromTop(14); row = area.removeFromTop(34);
    find.setBounds(row.removeFromLeft(108)); row.removeFromLeft(8); cancel.setBounds(row.removeFromLeft(76));
    sourceBadge.setBounds(row.removeFromRight(170)); activity.setBounds(row.reduced(12, 9));
    area.removeFromTop(10); title.setBounds(area.removeFromTop(26));
    status.setBounds(area);
}
}
