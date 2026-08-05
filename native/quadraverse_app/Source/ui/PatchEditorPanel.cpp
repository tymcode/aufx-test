#include "PatchEditorPanel.h"
#include "Utf8.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace qverse
{
namespace
{

constexpr int kAnalogInFunction = -1;
constexpr int kAnalogInPage = 0;
constexpr int kAnalogOutPage = 1;

constexpr int kKnobCellW = 78;
constexpr int kKnobCellH = 96;
constexpr int kKnobGap = 8;
constexpr int kSliderRowH = 26;

bool isEffectBusLevel (const ParamMeta& pm)
{
    if (! pm.id.startsWith (utf8 ("mix_")))
        return false;
    return pm.id.endsWith (utf8 ("_output_level"))
        || pm.id == utf8 ("mix_sample_playback_level");
}

bool isGlobalMixLevel (const ParamMeta& pm)
{
    return pm.id == utf8 ("mix_master_effects_level")
        || pm.id == utf8 ("mix_direct_signal_level");
}

bool isAnalogIoLevel (const ParamMeta& pm)
{
    return pm.address.function == kAnalogInFunction;
}

bool isLevelOrGainParam (const ParamMeta& pm)
{
    if (pm.choices.size() > 0 || (pm.max - pm.min) <= 1)
        return false;
    if (pm.id.contains (utf8 ("_input_mix")) || pm.id.contains (utf8 ("_predelay_mix")))
        return false;
    if (isGlobalMixLevel (pm) || isEffectBusLevel (pm) || isAnalogIoLevel (pm))
        return true;
    if (pm.id.contains (utf8 ("_level")) || pm.id.contains (utf8 ("_gain")))
        return true;
    if (pm.name.containsIgnoreCase (utf8 (" Level"))
        || pm.name.containsIgnoreCase (utf8 (" Gain")))
        return true;
    return false;
}

juce::String findSectionContaining (const juce::StringArray& sections, const juce::String& needle)
{
    for (const auto& s : sections)
        if (s.containsIgnoreCase (needle))
            return s;
    return {};
}

juce::String findFirstPresent (const juce::StringArray& sections,
                               std::initializer_list<const char*> names)
{
    for (const char* name : names)
    {
        const auto n = utf8 (name);
        if (sections.contains (n))
            return n;
    }
    return {};
}

juce::String remapDisplaySection (const ParamMeta& pm, const juce::StringArray& sections)
{
    if (! isEffectBusLevel (pm))
        return pm.section;

    if (pm.id == utf8 ("mix_pitch_output_level"))
    {
        if (auto s = findFirstPresent (sections, { "Pitch", "Reverb Chorus" }); s.isNotEmpty())
            return s;
    }
    else if (pm.id == utf8 ("mix_delay_output_level"))
    {
        if (auto s = findFirstPresent (sections, { "Delay", "Sampler" }); s.isNotEmpty())
            return s;
    }
    else if (pm.id == utf8 ("mix_reverb_output_level"))
    {
        if (auto s = findFirstPresent (sections, { "Reverb" }); s.isNotEmpty())
            return s;
    }
    else if (pm.id == utf8 ("mix_eq_output_level"))
    {
        if (auto s = findSectionContaining (sections, utf8 ("EQ")); s.isNotEmpty())
            return s;
    }
    else if (pm.id == utf8 ("mix_lezlie_output_level"))
    {
        if (auto s = findFirstPresent (sections, { "Lezlie" }); s.isNotEmpty())
            return s;
    }
    else if (pm.id == utf8 ("mix_ring_mod_output_level"))
    {
        if (auto s = findFirstPresent (sections, { "Ring Modulator" }); s.isNotEmpty())
            return s;
    }
    else if (pm.id == utf8 ("mix_resonator_output_level"))
    {
        if (auto s = findFirstPresent (sections, { "Resonator" }); s.isNotEmpty())
            return s;
    }
    else if (pm.id == utf8 ("mix_sample_playback_level"))
    {
        if (auto s = findFirstPresent (sections, { "Sampler" }); s.isNotEmpty())
            return s;
    }

    return pm.section;
}

int knobSortKey (const ParamMeta& pm)
{
    if (pm.id == utf8 ("mix_master_effects_level"))
        return 0;
    if (pm.id == utf8 ("mix_direct_signal_level"))
        return 1;
    if (pm.id == utf8 ("analog_input_level"))
        return 2;
    if (pm.id == utf8 ("analog_output_level"))
        return 3;
    if (isEffectBusLevel (pm))
        return 10;
    return 20;
}

juce::String knobCaption (const ParamMeta& pm)
{
    if (pm.id == utf8 ("mix_master_effects_level"))
        return utf8 ("Mix");
    if (pm.id == utf8 ("mix_direct_signal_level"))
        return utf8 ("Direct");
    if (pm.id == utf8 ("analog_input_level"))
        return utf8 ("Input");
    if (pm.id == utf8 ("analog_output_level"))
        return utf8 ("Output");

    auto n = pm.name;
    if (n.endsWithIgnoreCase (utf8 (" Output Level")))
        return n.dropLastCharacters ((int) std::strlen (" Output Level"));
    if (n.endsWithIgnoreCase (utf8 (" Level")))
        return n.dropLastCharacters ((int) std::strlen (" Level"));
    if (n.endsWithIgnoreCase (utf8 (" Gain")))
        return n.dropLastCharacters ((int) std::strlen (" Gain"));
    return n;
}

ParamMeta makeAnalogLevelMeta (bool isInput, int current, const juce::String& mixSection)
{
    ParamMeta pm;
    pm.address = { kAnalogInFunction, isInput ? kAnalogInPage : kAnalogOutPage, 0, 0 };
    pm.id = isInput ? utf8 ("analog_input_level") : utf8 ("analog_output_level");
    pm.name = isInput ? utf8 ("Input Level") : utf8 ("Output Level");
    pm.section = mixSection;
    pm.min = 0;
    pm.max = 99;
    pm.defaultValue = current;
    return pm;
}

} // namespace

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

int PatchEditorPanel::readParamValue (const ParamMeta& meta) const
{
    auto* ctx = manager != nullptr ? manager->getActive() : nullptr;
    if (ctx == nullptr)
        return meta.defaultValue;

    if (isAnalogIoLevel (meta))
        return meta.address.page == kAnalogInPage ? ctx->program.inLevel : ctx->program.outLevel;

    return ctx->program.getParam (meta.address.function, meta.address.page, meta.defaultValue);
}

void PatchEditorPanel::applyParamValue (const ParamMeta& meta, int value)
{
    if (manager == nullptr || manager->getActive() == nullptr)
        return;

    auto& prog = manager->getActive()->program;
    if (isAnalogIoLevel (meta))
    {
        if (meta.address.page == kAnalogInPage)
            prog.inLevel = value;
        else
            prog.outLevel = value;
        manager->markActiveDirty();
        return;
    }

    prog.setParam (meta.address.function, meta.address.page, value);
    manager->markActiveDirty();
    if (onParamChanged)
        onParamChanged (meta.address, value);
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
                            {
                                if (isAnalogIoLevel (meta))
                                {
                                    auto* from = manager->get (result - 2000);
                                    if (from != nullptr)
                                    {
                                        if (meta.address.page == kAnalogInPage)
                                            ctx->program.inLevel = from->program.inLevel;
                                        else
                                            ctx->program.outLevel = from->program.outLevel;
                                        manager->markActiveDirty();
                                    }
                                }
                                else
                                    manager->copyParamFrom (result - 2000, meta.address);
                            }
                            else if (result >= 3000 && result < 4000)
                            {
                                if (isAnalogIoLevel (meta))
                                {
                                    auto* to = manager->get (result - 3000);
                                    if (to != nullptr)
                                    {
                                        if (meta.address.page == kAnalogInPage)
                                            to->program.inLevel = ctx->program.inLevel;
                                        else
                                            to->program.outLevel = ctx->program.outLevel;
                                        to->dirty = true;
                                    }
                                }
                                else
                                    manager->copyParamTo (result - 3000, meta.address);
                            }
                            else if (result >= 4000 && result < 5000)
                                manager->copySectionFrom (result - 4000, meta.section);
                            else if (result >= 5000 && result < 6000)
                                manager->copySectionTo (result - 5000, meta.section);
                            rebuild();
                        });
}

void PatchEditorPanel::buildControl (const ParamMeta& meta, bool differs, ControlKind kind)
{
    ControlRow row;
    row.meta = meta;
    row.kind = kind;
    row.label = std::make_unique<juce::Label> (utf8 (""),
                                               kind == ControlKind::knob ? knobCaption (meta)
                                                                        : meta.name);
    row.label->setJustificationType (kind == ControlKind::knob
                                         ? juce::Justification::centred
                                         : juce::Justification::centredLeft);
    content.addAndMakeVisible (*row.label);

    const int cur = readParamValue (meta);
    auto* ctx = manager->getActive();
    const bool locked = ctx != nullptr
        && ctx->program.metaFor (meta.address).protectFromAdjustment;

    if (kind == ControlKind::combo)
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
            int v = meta.choices.size() > 0
                ? meta.min + cb->getSelectedItemIndex()
                : (cb->getSelectedItemIndex() > 0 ? 1 : 0);
            applyParamValue (meta, v);
            if (onParamGestureEnd)
                onParamGestureEnd (meta.address);
        };
        content.addAndMakeVisible (*box);
        row.control = std::move (box);
    }
    else
    {
        const auto style = kind == ControlKind::knob
            ? juce::Slider::RotaryHorizontalVerticalDrag
            : juce::Slider::LinearHorizontal;
        const auto textBox = kind == ControlKind::knob
            ? juce::Slider::TextBoxBelow
            : juce::Slider::TextBoxRight;
        auto slider = std::make_unique<juce::Slider> (style, textBox);
        slider->setRange ((double) meta.min, (double) meta.max, 1.0);
        slider->setValue ((double) cur, juce::dontSendNotification);
        slider->setEnabled (! locked);
        slider->setTextBoxIsEditable (false);
        if (kind == ControlKind::knob)
        {
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                         juce::MathConstants<float>::pi * 2.8f,
                                         true);
        }
        slider->setColour (juce::Slider::rotarySliderFillColourId,
                           differs ? juce::Colours::orange : juce::Colours::steelblue);
        slider->setColour (juce::Slider::rotarySliderOutlineColourId,
                           juce::Colours::grey.darker (0.2f));
        slider->setColour (juce::Slider::trackColourId,
                           differs ? juce::Colours::orange : juce::Colours::steelblue);
        slider->setColour (juce::Slider::thumbColourId,
                           differs ? juce::Colours::orange.brighter (0.2f) : juce::Colours::white);
        if (differs)
            row.label->setColour (juce::Label::textColourId, juce::Colours::orange);
        slider->onValueChange = [this, meta, s = slider.get()]
        {
            applyParamValue (meta, (int) std::lround (s->getValue()));
        };
        slider->onDragEnd = [this, meta]
        {
            if (onParamGestureEnd && ! isAnalogIoLevel (meta))
                onParamGestureEnd (meta.address);
        };
        content.addAndMakeVisible (*slider);
        row.control = std::move (slider);
    }

    row.label->addMouseListener (this, false);
    row.label->setComponentID (meta.address.key());
    row.label->setTooltip (meta.name + utf8 (" — right-click for Protect / Copy"));

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
    // Analog I/O diffs across compare patches
    if (manager->size() >= 2)
    {
        for (int i = 0; i < manager->size(); ++i)
        {
            if (i == manager->getActiveIndex())
                continue;
            auto* other = manager->get (i);
            if (other == nullptr || ! other->compare)
                continue;
            if (other->program.inLevel != prog.inLevel)
                diffKeys.add (ParamAddress { kAnalogInFunction, kAnalogInPage, 0, 0 }.key());
            if (other->program.outLevel != prog.outLevel)
                diffKeys.add (ParamAddress { kAnalogInFunction, kAnalogOutPage, 0, 0 }.key());
        }
    }

    std::map<std::string, ParamMeta> unique;
    juce::StringArray discoveredSections;

    const auto all = profile.parametersForConfig (prog.config);
    for (const auto& pm : all)
    {
        unique[pm.address.key().toStdString()] = pm;
        if (! discoveredSections.contains (pm.section))
            discoveredSections.add (pm.section);
    }

    for (auto& kv : unique)
        kv.second.section = remapDisplaySection (kv.second, discoveredSections);

    const auto mixSection = utf8 ("Mix");
    unique[ParamAddress { kAnalogInFunction, kAnalogInPage, 0, 0 }.key().toStdString()]
        = makeAnalogLevelMeta (true, prog.inLevel, mixSection);
    unique[ParamAddress { kAnalogInFunction, kAnalogOutPage, 0, 0 }.key().toStdString()]
        = makeAnalogLevelMeta (false, prog.outLevel, mixSection);
    if (! discoveredSections.contains (mixSection))
        discoveredSections.add (mixSection);

    juce::StringArray sectionOrder;
    if (discoveredSections.contains (utf8 ("Config")))
        sectionOrder.add (utf8 ("Config"));
    if (discoveredSections.contains (mixSection))
        sectionOrder.add (mixSection);
    for (const auto& section : discoveredSections)
        if (section != utf8 ("Config") && section != mixSection)
            sectionOrder.add (section);

    for (const auto& section : sectionOrder)
    {
        std::vector<ParamMeta> knobs;
        std::vector<ParamMeta> rest;
        for (const auto& kv : unique)
        {
            if (kv.second.section != section)
                continue;
            if (isLevelOrGainParam (kv.second))
                knobs.push_back (kv.second);
            else
                rest.push_back (kv.second);
        }

        if (knobs.empty() && rest.empty())
            continue;

        std::sort (knobs.begin(), knobs.end(),
                   [] (const ParamMeta& a, const ParamMeta& b)
                   {
                       const int ka = knobSortKey (a);
                       const int kb = knobSortKey (b);
                       if (ka != kb)
                           return ka < kb;
                       return a.address.page < b.address.page;
                   });

        std::sort (rest.begin(), rest.end(),
                   [] (const ParamMeta& a, const ParamMeta& b)
                   {
                       if (a.address.function != b.address.function)
                           return a.address.function < b.address.function;
                       return a.address.page < b.address.page;
                   });

        addSection (section);
        for (const auto& pm : knobs)
            buildControl (pm, diffKeys.contains (pm.address.key()), ControlKind::knob);
        for (const auto& pm : rest)
        {
            const ControlKind kind = (pm.choices.size() > 0 || (pm.max - pm.min) <= 1)
                ? ControlKind::combo
                : ControlKind::slider;
            buildControl (pm, diffKeys.contains (pm.address.key()), kind);
        }
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

    const int width = juce::jmax (400, viewport.getWidth() - 16);
    int y = 0;
    size_t rowIndex = 0;

    for (size_t si = 0; si < (size_t) sectionLabels.size(); ++si)
    {
        sectionLabels[(int) si]->setBounds (0, y, width, 24);
        y += 28;
        const auto title = sectionLabels[(int) si]->getText();

        // Knob strip at the top of the section
        int knobX = 4;
        int knobRowBottom = y;
        bool anyKnob = false;
        while (rowIndex < rows.size()
               && rows[rowIndex].meta.section == title
               && rows[rowIndex].kind == ControlKind::knob)
        {
            anyKnob = true;
            if (knobX + kKnobCellW > width - 4 && knobX > 4)
            {
                y = knobRowBottom + 4;
                knobX = 4;
                knobRowBottom = y;
            }

            auto& row = rows[rowIndex++];
            const int cellX = knobX;
            const int cellY = y;
            row.label->setBounds (cellX, cellY, kKnobCellW, 18);
            if (row.control != nullptr)
                row.control->setBounds (cellX + 4, cellY + 18, kKnobCellW - 8, kKnobCellH - 22);
            knobX += kKnobCellW + kKnobGap;
            knobRowBottom = juce::jmax (knobRowBottom, cellY + kKnobCellH);
        }
        if (anyKnob)
            y = knobRowBottom + 8;

        while (rowIndex < rows.size() && rows[rowIndex].meta.section == title)
        {
            auto& row = rows[rowIndex++];
            row.label->setBounds (0, y, 200, kSliderRowH);
            if (row.control != nullptr)
            {
                if (row.kind == ControlKind::combo)
                    row.control->setBounds (210, y + 1, juce::jmax (160, width - 220), kSliderRowH - 2);
                else
                    row.control->setBounds (210, y + 2, juce::jmax (160, width - 220), kSliderRowH - 4);
            }
            y += kSliderRowH + 2;
        }
    }
    content.setSize (width, y + 20);
}

} // namespace qverse
