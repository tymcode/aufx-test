#pragma once

#include <JuceHeader.h>
#include "../domain/PatchContextManager.h"
#include "../domain/DeviceProfile.h"

namespace qverse
{

class PatchEditorPanel : public juce::Component
{
public:
    PatchEditorPanel();

    void setManager (PatchContextManager* manager);
    void rebuild();
    void refreshDiffHighlights();

    std::function<void(const ParamAddress&, int)> onParamChanged;
    std::function<void(const ParamAddress&)> onParamGestureEnd;

    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    enum class ControlKind
    {
        combo,
        slider,
        knob
    };

    struct ControlRow
    {
        ParamMeta meta;
        ControlKind kind = ControlKind::slider;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
    };

    void clearControls();
    void addSection (const juce::String& title);
    void buildControl (const ParamMeta& meta, bool differs, ControlKind kind);
    void applyParamValue (const ParamMeta& meta, int value);
    int readParamValue (const ParamMeta& meta) const;
    void showParamMenu (const ParamMeta& meta);

    PatchContextManager* manager = nullptr;
    juce::ComboBox configBox;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<juce::Label> sectionLabels;
    std::vector<ControlRow> rows;
    juce::StringArray diffKeys;
};

} // namespace qverse
