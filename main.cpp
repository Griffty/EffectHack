#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

namespace effecthack
{
class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        return getCommandLineParameters().startsWith("--smoke-test ");
    }
    void initialise(const juce::String& commandLine) override
    {
        window = std::make_unique<Window>(getApplicationName(), commandLine.startsWith("--smoke-test "));
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        // A development smoke check renders the real UI while monitoring stays muted.
        if ((args.size() == 2 || args.size() == 4) && args[0] == "--smoke-test")
        {
            const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile(args[1].unquoted());
            const auto width = args.size() == 4 ? juce::jlimit(940, 2000, args[2].getIntValue()) : 1120;
            const auto height = args.size() == 4 ? juce::jlimit(740, 1700, args[3].getIntValue()) : 1280;
            juce::Timer::callAfterDelay(1500, [this, destination, width, height]
            {
                auto* content = window->getContentComponent();
                if (content != nullptr)
                    content->setSize(width, height);
                auto stream = destination.createOutputStream();
                if (stream != nullptr)
                {
                    stream->setPosition(0);
                    stream->truncate();
                }
                const auto saved = content != nullptr && stream != nullptr
                    && juce::PNGImageFormat().writeImageToStream(
                        content->createComponentSnapshot(content->getLocalBounds()), *stream);
                setApplicationReturnValue(saved ? 0 : 1);
                quit();
            });
        }
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    class Window final : public juce::DocumentWindow
    {
    public:
        explicit Window(const juce::String& name, bool smokeTest)
            : DocumentWindow(name, juce::Colour(0xff12171d), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(smokeTest ? juce::File() : SessionStore::defaultFile()), true);
            setResizable(true, false);
            setResizeLimits(940, 740, 2000, 1700);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
}

START_JUCE_APPLICATION(effecthack::Application)
