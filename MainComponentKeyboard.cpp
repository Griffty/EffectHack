#include "MainComponent.h"

namespace effecthack
{
namespace
{
bool editingTextOrSelection(juce::Component* component)
{
    for (; component != nullptr; component = component->getParentComponent())
        if (dynamic_cast<juce::TextEditor*>(component) != nullptr || dynamic_cast<juce::ComboBox*>(component) != nullptr) return true;
    return false;
}
}
bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component* origin)
{
    return !editingTextOrSelection(origin) && keyPressed(key);
}
void MainComponent::installKeyboardControls(juce::Component& component)
{
    // Listen before focused buttons consume Space. Dynamic slider editors still
    // get normal typing because the focused-component guard runs on each event.
    component.addKeyListener(this);
    for (auto* child : component.getChildren()) installKeyboardControls(*child);
}
void MainComponent::releaseShortcutKeys()
{
    for (auto key = heldShortcutKeys.begin(); key != heldShortcutKeys.end();)
        if (!juce::KeyPress::isKeyCurrentlyDown(*key)) key = heldShortcutKeys.erase(key); else ++key;
}
bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    if (modifiers.isCtrlDown() || modifiers.isAltDown() || modifiers.isCommandDown()
        || isCurrentlyBlockedByAnotherModalComponent()) return false;
    if (editingTextOrSelection(juce::Component::getCurrentlyFocusedComponent())) return false;
    auto code = key.getKeyCode();
    if (code >= 'a' && code <= 'z') code += 'A' - 'a';
    const bool copy = code == 'X' && modifiers.isShiftDown();
    if (modifiers.isShiftDown() && !copy) return false;
    if (code != '1' && code != '2' && code != 'X' && code != 'B' && code != 'M' && code != 'T' && code != 'R' && code != 'L'
        && code != juce::KeyPress::spaceKey && code != juce::KeyPress::homeKey) return false;
    if (!heldShortcutKeys.insert(code).second) return true; // Ignore key repeat.
    if (code == '1' || code == '2') selectToneSlot(code - '1');
    else if (code == 'X') { if (copy) copyToneSlot(); else selectToneSlot(1 - toneSlots.active()); }
    else if (code == 'B')
    {
        rackBypassButton.setToggleState(!engine.rackBypass.load(), juce::dontSendNotification);
        rackBypassButton.onClick();
    }
    else if (code == 'M')
    {
        masterMuteButton.setToggleState(!engine.masterMute.load(), juce::dontSendNotification);
        masterMuteButton.onClick();
    }
    else if (code == 'T') tunerButton.onClick();
    else if (code == 'R' && phrasePanel.record.isEnabled()) togglePhraseRecording();
    else if (code == 'L' && phrasePanel.audition.isEnabled()) togglePhraseAudition();
    else if (code == juce::KeyPress::spaceKey && monitoringButton.isEnabled()) togglePlayback();
    else if (code == juce::KeyPress::homeKey && restartButton.isEnabled()) restartButton.onClick();
    return true;
}
bool MainComponent::keyStateChanged(bool)
{
    releaseShortcutKeys();
    return false;
}
}
