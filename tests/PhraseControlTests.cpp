#include "../MainComponent.h"
#include <cmath>
#include <stdexcept>
namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
namespace effecthack
{
struct PhraseControlsTestAccess
{
    static void tick(MainComponent& component, int blocks)
    {
        juce::AudioBuffer<float> input(2, 128), output(2, 128);
        for (int n = 0; n < blocks; ++n)
        {
            for (int i = 0; i < 128; ++i)
            {
                input.setSample(0, i, .8f);
                input.setSample(1, i, .15f * std::sin(static_cast<float>(n * 128 + i) * .057595865f));
            }
            component.engine.audioDeviceIOCallbackWithContext(input.getArrayOfReadPointers(), 2, output.getArrayOfWritePointers(), 2, 128, {});
        }
    }
    static void pump(MainComponent& component)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        do
        {
            tick(component, 4); juce::MessageManager::getInstance()->runDispatchLoopUntil(3);
            component.serviceToneChange(); component.refreshDryPhrase(); component.refreshSourceStatus();
        } while ((component.preparingPhrase || component.clearingPhrase || component.loadingPreset || component.engine.phraseSwitchPending())
            && juce::Time::getMillisecondCounter() < deadline);
        require(!component.preparingPhrase && !component.clearingPhrase && !component.loadingPreset && !component.engine.phraseSwitchPending(), "Phrase preparation and fades complete");
    }
    static void start(MainComponent& component)
    {
        component.phraseCaptureRate = 48000; component.phraseGeneration = component.engine.deviceGeneration.load();
        component.engine.requestPhraseAudition(false); tick(component, 30);
        component.engine.phraseRecorder.arm(DryPhraseRecorder::allocate(48000));
    }
    static void run(const juce::File& folder)
    {
        const auto session = folder.getChildFile("phrase-session.json");
        {
            MainComponent c(session, folder.getChildFile("phrase-presets"), false); c.stopTimer();
            require(c.output.getValue() == 0 && captureRack(c.engine).values[3] == 0, "UI and engine Master Output default to 0 dB");
            c.engine.prepare(48000, 128); c.engine.monitoring = true;
            c.engine.inputSource = AudioEngine::InputSource::hardware; c.engine.inputChannel = 1;
            c.sourceSelector.setSelectedId(3, juce::dontSendNotification);
            c.engine.testInput.setLooping(false); c.loopButton.setToggleState(false, juce::dontSendNotification);
            start(c); tick(c, 200); c.refreshDryPhrase();
            require(c.phrasePanel.record.getButtonText() == "Finish", "Recording displays Finish");
            c.togglePhraseRecording(); pump(c);
            require(c.engine.phraseInput.hasClip() && c.engine.isPhraseAuditioning(), "Finishing automatically auditions the take");
            require(c.engine.phraseInput.getClip()->samples.getMagnitude(0, 0, 10000) < .16f, "UI capture uses the selected right channel");
            const auto* take = c.engine.phraseInput.getClip();
            require(c.loopButton.getToggleState() && !c.loopButton.isEnabled(), "Audition loops independently of the original source");
            c.selectToneSlot(1); pump(c); c.engine.eqParameters.trebleDb = 3;
            const auto before = c.engine.playbackPosition(); c.selectToneSlot(0); pump(c);
            require(c.engine.isPhraseAuditioning() && c.engine.phraseInput.getClip() == take
                && c.engine.playbackPosition() != before && c.engine.parameters.outputDb.load() == 0, "A/B retains the same phrase and Master level");
            c.saveSession(); SessionState state; bool found = false;
            require(SessionStore(session).read(state, found).wasOk() && found && state.sourceId == 3 && !state.loop, "Startup remembers original input and loop preference");
            for (int width : {940, 1120})
            {
                c.setSize(width, 900);
                require(c.phrasePanel.getBottom() <= c.getHeight() && c.phrasePanel.clear.getRight() < c.phrasePanel.getWidth(), "Recorder controls fit minimum width");
                juce::MemoryOutputStream stream;
                require(juce::PNGImageFormat().writeImageToStream(c.createComponentSnapshot(c.getLocalBounds()), stream)
                    && folder.getParentDirectory().getChildFile("dry-phrase-" + juce::String(width) + ".png").replaceWithData(stream.getData(), stream.getDataSize()), "Render phrase UI");
            }
            c.returnFromPhrase(); pump(c);
            require(!c.engine.isPhraseAuditioning() && c.engine.inputSource.load() == AudioEngine::InputSource::hardware, "Live restores original guitar source");
            start(c); tick(c, 200); c.engine.phraseRecorder.stop(); c.refreshDryPhrase(); c.engine.monitoring = false; pump(c);
            require(c.engine.phraseInput.hasClip() && !c.engine.isPhraseAuditioning() && !c.engine.monitoring, "Pausing during preparation never restarts audio");
            c.engine.monitoring = true; c.togglePhraseAudition(); pump(c);
            require(c.engine.isPhraseAuditioning(), "Phrase can be auditioned again");
            c.clearPhrase(); require(c.engine.phraseInput.hasClip(), "Clear waits for fade-down before releasing audio storage"); pump(c);
            require(!c.engine.phraseInput.hasClip() && !c.engine.isPhraseAuditioning(), "Clear returns to original input");
            start(c); tick(c, 200); c.engine.phraseRecorder.stop(); c.refreshDryPhrase(); c.clearPhrase(); pump(c);
            for (int i = 0; i < 20; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(3);
            require(!c.engine.phraseInput.hasClip(), "Late worker results cannot restore a cleared phrase");
            c.engine.parameters.outputDb = -13; c.saveSession();
        }
        {
            MainComponent c(session, folder.getChildFile("phrase-presets"), false); c.stopTimer(); pump(c);
            require(!c.engine.phraseInput.hasClip() && !c.engine.isPhraseAuditioning() && !c.engine.monitoring
                && c.engine.inputSource.load() == AudioEngine::InputSource::hardware && c.engine.parameters.outputDb.load() == -13,
                "Temporary phrases are not restored; explicitly saved listening levels still are");
        }
    }
};
}
void testPhraseControls(const juce::File& folder) { effecthack::PhraseControlsTestAccess::run(folder); }
