#include "PatchEditorPanel.h"
#include "Utf8.h"

namespace qverse
{

PatchEditorPanel::PatchEditorPanel()
{
    addAndMakeVisible (configBox);
    configBox.onChange = [this]
    {
        if (auto* ctx = manager != nullptr ? manager->getActive() : nullptr)
        {
            const int cfg = configBox.getSelectedItemIndex();
            ctx->program.setParam (7, 0, cfg);
            ctx->program.config = cfg;
            manager->markActiveDirty();
            if (onParamChanged)
                onParamChanged ({ 7, 0, 0, 0 }, cfg);
            rebuild();
        }
    };

    viewport.setViewedComponent (&content, false);
    addAndMakeVisible (viewport);
}

void PatchEditorPanel::setManager (PatchContextManager* m)
{
    manager = m;
    rebuild();
}

void PatchEditorPanel::clearControls()
{
    rows.clear();
    sectionLabels.clear();
    content.removeAllChildren();
}

void PatchEditorPanel::addSection (const juce::String& title)
{
    auto* lab = sectionLabels.add (new juce::Label (utf8 (""), title));
    lab->setFont (juce::FontOptions (16.0f).withStyle (utf8 ("Bold")));
    content.addAndMakeVisible (lab);
}

void PatchEditorPanel::showParamMenu (const ParamMeta& meta)
{
    if (manager == nullptr)
        return;

    juce::PopupMenu menu;

    juce::PopupMenu protect;
    auto* metaPtr = manager->getActive() != nullptr
        ? &manager->getActive()->program.metaFor (meta.address)
        : nullptr;
    const bool adj = metaPtr != nullptr && metaPtr->protectFromAdjustment;
    const bool rnd = metaPtr != nullptr && metaPtr->protectFromRandomization;
    protect.addItem (1001, utf8 ("From adjustment"), true, adj);
    protect.addItem (1002, utf8 ("From randomization"), true, rnd);
    menu.addSubMenu (utf8 ("Protect"), protect);

    juce::PopupMenu copyFrom, copyTo, copyFromSec, copyToSec;
    for (int i = 0; i < manager->size(); ++i)
    {
        if (i == manager->getActiveIndex())
            continue;
        const auto name = manager->get (i)->name;
        copyFrom.addItem (2000 + i, name);
        copyTo.addItem (3000 + i, name);
        copyFromSec.addItem (4000 + i, name);
        copyToSec.addItem (5000 + i, name);
    }
    if (copyFrom.getNumItems() > 0)
    {
        menu.addSubMenu (utf8 ("Copy from"), copyFrom);
        menu.addSubMenu (utf8 ("Copy to"), copyTo);
        juce::PopupMenu sec;
        sec.addSubMenu (utf8 ("Copy section from"), copyFromSec);
        sec.addSubMenu (utf8 ("Copy section to"), copyToSec);
        menu.addSubMenu (utf8 ("Section"), sec);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, meta] (int result)
                        {
                            if (manager == nullptr || result == 0)
                                return;
                            auto* ctx = manager->getActive();
                            if (ctx == nullptr)
                                return;
                            if (result == 1001)
                            {
                                auto& m = ctx->program.metaFor (meta.address);
                                m.protectFromAdjustment = ! m.protectFromAdjustment;
                                rebuild();
                            }
                            else if (result == 1002)
                            {
                                auto& m = ctx->program.metaFor (meta.address);
                                m.protectFromRandomization = ! m.protectFromRandomization;
                            }
                            else if (result >= 2000 && result < 3000)
                                manager->copyParamFrom (result - 2000, meta.address);
                            else if (result >= 3000 && result < 4000)
                                manager->copyParamTo (result - 3000, meta.address);
                            else if (result >= 4000 && result < 5000)
                                manager->copySectionFrom (result - 4000, meta.section);
                            else if (result >= 5000 && result < 6000)
                                manager->copySectionTo (result - 5000, meta.section);
                            rebuild();
                        });
}

void PatchEditorPanel::buildControl (const ParamMeta& meta, bool differs)
{
    ControlRow row;
    row.meta = meta;
    row.label = std::make_unique<juce::Label> ("", meta.name);
    row.label->setJustificationType (juce::Justification::centredLeft);
    content.addAndMakeVisible (*row.label);

    auto* ctx = manager->getActive();
    const int cur = ctx != nullptr
        ? ctx->program.getParam (meta.address.function, meta.address.page, meta.defaultValue)
        : meta.defaultValue;

    const bool locked = ctx != nullptr
        && ctx->program.metaFor (meta.address).protectFromAdjustment;

    if (meta.choices.size() > 0 || (meta.max - meta.min) <= 1)
    {
        auto box = std::make_unique<juce::ComboBox>();
        if (meta.choices.size() > 0)
        {
            for (int i = 0; i < meta.choices.size(); ++i)
                box->addItem (meta.choices[i], i + 1);
            box->setSelectedItemIndex (juce::jlimit (0, meta.choices.size() - 1, cur - meta.min),
                                       juce::dontSendNotification);
        }
        else
        {
            box->addItem (utf8 ("Off"), 1);
            box->addItem (utf8 ("On"), 2);
            box->setSelectedItemIndex (cur != 0 ? 1 : 0, juce::dontSendNotification);
        }
        box->setEnabled (! locked);
        box->onChange = [this, meta, cb = box.get()]
        {
            if (manager == nullptr || manager->getActive() == nullptr)
                return;
            int v = meta.choices.size() > 0
                ? meta.min + cb->getSelectedItemIndex()
                : (cb->getSelectedItemIndex() > 0 ? 1 : 0);
            manager->getActive()->program.setParam (meta.address.function, meta.address.page, v);
            manager->markActiveDirty();
            if (onParamChanged)
                onParamChanged (meta.address, v);
            if (onParamGestureEnd)
                onParamGestureEnd (meta.address);
        };
        content.addAndMakeVisible (*box);
        row.control = std::move (box);
    }
    else
    {
        auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                      juce::Slider::TextBoxRight);
        slider->setRange ((double) meta.min, (double) meta.max, 1.0);
        slider->setValue ((double) cur, juce::dontSendNotification);
        slider->setEnabled (! locked);
        slider->setTextBoxIsEditable (false);
        slider->setColour (juce::Slider::trackColourId,
                           differs ? juce::Colours::orange : juce::Colours::steelblue);
        slider->setColour (juce::Slider::thumbColourId,
                           differs ? juce::Colours::orange.brighter (0.2f) : juce::Colours::white);
        if (differs)
            row.label->setColour (juce::Label::textColourId, juce::Colours::orange);
        slider->onValueChange = [this, meta, s = slider.get()]
        {
            if (manager == nullptr || manager->getActive() == nullptr)
                return;
            const int v = (int) std::lround (s->getValue());
            manager->getActive()->program.setParam (meta.address.function, meta.address.page, v);
            manager->markActiveDirty();
            if (onParamChanged)
                onParamChanged (meta.address, v);
        };
        slider->onDragEnd = [this, meta]
        {
            if (onParamGestureEnd)
                onParamGestureEnd (meta.address);
        };
        content.addAndMakeVisible (*slider);
        row.control = std::move (slider);
    }

    row.label->addMouseListener (this, false);
    // Store meta on label via component ID for menu
    row.label->setComponentID (meta.address.key());
    row.label->setTooltip (utf8 ("Right-click for Protect / Copy"));

    content.addAndMakeVisible (*row.label);
    rows.push_back (std::move (row));
}

void PatchEditorPanel::rebuild()
{
    clearControls();
    if (manager == nullptr || manager->getActive() == nullptr)
        return;

    auto& prog = manager->getActive()->program;
    const auto& profile = profileFor (prog.model);

    configBox.clear (juce::dontSendNotification);
    const auto names = profile.configNames();
    for (int i = 0; i < names.size(); ++i)
        configBox.addItem (names[i], i + 1);
    configBox.setSelectedItemIndex (juce::jlimit (0, names.size() - 1, prog.config),
                                    juce::dontSendNotification);

    diffKeys.clear();
    for (const auto& d : manager->differingParams())
        diffKeys.add (d.key());

    // Deduplicate parameters by address — modes expand overlapping ids.
    std::map<std::string, ParamMeta> unique;
    juce::StringArray sectionOrder;

    const auto all = profile.parametersForConfig (prog.config);
    for (const auto& pm : all)
    {
        unique[pm.address.key().toStdString()] = pm;
        if (! sectionOrder.contains (pm.section))
            sectionOrder.add (pm.section);
    }

    for (const auto& section : sectionOrder)
    {
        addSection (section);
        for (const auto& kv : unique)
            if (kv.second.section == section)
                buildControl (kv.second, diffKeys.contains (kv.second.address.key()));
    }

    resized();
}

void PatchEditorPanel::refreshDiffHighlights()
{
    rebuild();
}

void PatchEditorPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;
    if (auto* c = e.eventComponent)
    {
        const auto key = c->getComponentID();
        for (const auto& row : rows)
            if (row.meta.address.key() == key)
            {
                showParamMenu (row.meta);
                return;
            }
    }
}

void PatchEditorPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    configBox.setBounds (r.removeFromTop (28).removeFromLeft (360));
    r.removeFromTop (6);
    viewport.setBounds (r);

    int y = 0;
    const int width = juce::jmax (400, viewport.getWidth() - 16);
    for (auto* lab : sectionLabels)
    {
        lab->setBounds (0, y, width, 24);
        y += 28;
        // Place controls belonging to this section by scanning rows until next section...
    }

    // Flatten: section labels and rows were added in order — lay out by y order of children.
    // Simpler: lay out section labels then all rows in creation order interleaved.
    // Rebuild layout using content children order is hard; lay out explicitly:
    y = 0;
    size_t rowIndex = 0;
    // We don't track which rows belong to which section in a structure —
    // lay out all section labels at their insertion points by reconstructing:
    // Walk sectionLabels and for each, emit label then subsequent rows until next section name change.
    juce::String currentSection;
    for (size_t si = 0; si < (size_t) sectionLabels.size(); ++si)
    {
        sectionLabels[(int) si]->setBounds (0, y, width, 24);
        y += 28;
        const auto title = sectionLabels[(int) si]->getText();
        while (rowIndex < rows.size() && rows[rowIndex].meta.section == title)
        {
            auto& row = rows[rowIndex++];
            constexpr int rowH = 26;
            row.label->setBounds (0, y, 200, rowH);
            if (row.control != nullptr)
            {
                if (dynamic_cast<juce::ComboBox*> (row.control.get()) != nullptr)
                    row.control->setBounds (210, y + 1, juce::jmax (160, width - 220), rowH - 2);
                else
                    row.control->setBounds (210, y + 2, juce::jmax (160, width - 220), rowH - 4);
            }
            y += rowH + 2;
        }
    }
    content.setSize (width, y + 20);
}

} // namespace qverse
