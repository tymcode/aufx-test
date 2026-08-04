#include "SysexPatchPickerDialog.h"
#include "Utf8.h"

namespace qverse
{

namespace
{

class ToggleListContent : public juce::Component
{
public:
    explicit ToggleListContent (const juce::StringArray& names, bool selectAll)
    {
        for (int i = 0; i < names.size(); ++i)
        {
            auto* tb = toggles.add (new juce::ToggleButton (names[i]));
            tb->setToggleState (selectAll, juce::dontSendNotification);
            addAndMakeVisible (tb);
        }
        setSize (400, juce::jmax (24, names.size() * 26));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        for (auto* tb : toggles)
            tb->setBounds (r.removeFromTop (26));
    }

    void setAll (bool on)
    {
        for (auto* tb : toggles)
            tb->setToggleState (on, juce::dontSendNotification);
    }

    juce::Array<int> selectedIndices() const
    {
        juce::Array<int> out;
        for (int i = 0; i < toggles.size(); ++i)
            if (toggles[i]->getToggleState())
                out.add (i);
        return out;
    }

private:
    juce::OwnedArray<juce::ToggleButton> toggles;
};

class PatchPickerContent : public juce::Component
{
public:
    explicit PatchPickerContent (const juce::StringArray& names, bool selectAll)
        : list (names, selectAll)
    {
        selectAllButton.setButtonText (utf8 ("Select All"));
        selectNoneButton.setButtonText (utf8 ("Select None"));
        selectAllButton.onClick = [this] { list.setAll (true); };
        selectNoneButton.onClick = [this] { list.setAll (false); };
        addAndMakeVisible (selectAllButton);
        addAndMakeVisible (selectNoneButton);

        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        // Fixed dialog height; the viewport scrolls through the full bank.
        setSize (440, 420);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (28);
        selectAllButton.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        selectNoneButton.setBounds (top.removeFromLeft (110));
        r.removeFromTop (6);
        viewport.setBounds (r);
        list.setSize (juce::jmax (0, viewport.getWidth() - viewport.getScrollBarThickness()),
                      list.getHeight());
    }

    juce::Array<int> selectedIndices() const { return list.selectedIndices(); }

private:
    juce::TextButton selectAllButton, selectNoneButton;
    ToggleListContent list;
    juce::Viewport viewport;
};

} // namespace

SysexPatchPickResult runSysexPatchPicker (const juce::StringArray& patchNames,
                                          juce::Component* parent,
                                          bool selectAllByDefault)
{
    SysexPatchPickResult result;
    if (patchNames.isEmpty())
        return result;

    juce::AlertWindow w (utf8 ("Select Patches"),
                         utf8 ("Choose one or more patches to import into new patch contexts:"),
                         juce::AlertWindow::QuestionIcon,
                         parent);

    PatchPickerContent content (patchNames, selectAllByDefault);
    w.addCustomComponent (&content);
    w.addButton (utf8 ("Import"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w.addButton (utf8 ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (w.runModalLoop() != 1)
        return result;

    result.selectedIndices = content.selectedIndices();
    result.ok = ! result.selectedIndices.isEmpty();
    return result;
}

} // namespace qverse
