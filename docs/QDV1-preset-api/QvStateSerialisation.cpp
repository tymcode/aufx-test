// QDV1 -- plugin state serialisation.
//
// Excerpt from the JUCE AudioProcessor: the two functions the host calls to
// save and restore a session, and the same blob QvPresetStore stores (base64)
// inside a user preset's JSON.  Reference only -- it is lifted out of the
// processor and will not compile stand-alone.
//
// It depends on four things from the processor, all of which are ordinary
// accessors over the plugin's current state:
//
//   snapshot()        -> QvSnapshot, below: the current program, configuration
//                        and every parameter value, indexed [function][page].
//   queueEdit(f,p,v)  -> set one (function, page) parameter to v.
//   queueProgram(n)   -> load stored program n (0..99).
//   inputLevel() / outputLevel() / setInputLevel() / setOutputLevel()
//                     -> the front panel's two analog trims, 0..99.
//
// Ordering matters in one place, and setStateInformation handles it: page 0
// of a block is its MODE selector, and the meaning of every other page
// depends on it, so all page-0 edits are replayed before anything else.
//
// See QvParameters.h for what (function, page) pairs exist per configuration,
// and MidiLearn.h for the target identity used by the MIDI_MAP section.

// --- for reference, the snapshot fields these two functions read ---------
struct QvSnapshot {
    int  program = 0;           // the stored program this state came from, 0..99
    int  config  = 4;           // the active configuration, 0..7
    int  value[10][32]  = {};   // parameter values, indexed [function][page]
    bool known[10][32]  = {};   // true where this snapshot carries a value
};

// State: the program it came from, then every parameter of the active
// configuration as (function, page, value).
void QDV1Processor::getStateInformation(juce::MemoryBlock& dest) {
    auto s = snapshot();
    juce::XmlElement xml("QDV1");
    xml.setAttribute("program", s.program);
    xml.setAttribute("config", s.config);
    xml.setAttribute("inLevel", inputLevel());
    xml.setAttribute("outLevel", outputLevel());
    for (int f = 0; f < 10; ++f)
        for (int p = 0; p < 32; ++p)
            if (s.known[f][p] && !(f == qv::kConfigFunction && p == qv::kConfigPage)) {
                auto* e = xml.createNewChildElement("P");
                e->setAttribute("f", f);
                e->setAttribute("p", p);
                e->setAttribute("v", s.value[f][p]);
            }

    // MIDI mappings.  The container is always written, even when empty, so a
    // deliberately cleared map stays cleared across a host/project restore.
    auto* midiMap = xml.createNewChildElement("MIDI_MAP");
    auto writeBinding = [this, midiMap](const QvMidiTarget& target) {
        const auto b = getMidiBinding(target);
        if (!b.active) return;
        auto* e = midiMap->createNewChildElement("M");
        e->setAttribute("kind", (int) target.kind);
        e->setAttribute("cfg", target.config);
        e->setAttribute("f", target.function);
        e->setAttribute("mode", target.mode);
        e->setAttribute("pg", target.page);
        e->setAttribute("ch", b.channel);
        e->setAttribute("cc", b.cc);
    };
    for (int c = 0; c < kQvMidiConfigs; ++c)
        for (int f = 1; f < kQvMidiFunctions; ++f)
            for (int m = 0; m < kQvMidiModes; ++m)
                for (int pg = (m == 0 ? 0 : 1); pg < kQvMidiPages; ++pg)
                    writeBinding(QvMidiTarget::param(c, f, m, pg));
    writeBinding(QvMidiTarget::inputLevel());
    writeBinding(QvMidiTarget::outputLevel());
    writeBinding(QvMidiTarget::bypass());

    copyXmlToBinary(xml, dest);
}

void QDV1Processor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("QDV1")) return;
    setInputLevel(xml->getIntAttribute("inLevel", 50));
    setOutputLevel(xml->getIntAttribute("outLevel", 84));
    queueProgram(xml->getIntAttribute("program", 0));
    queueEdit(qv::kConfigFunction, qv::kConfigPage, xml->getIntAttribute("config", 4));
    // Page 0 (mode selectors) first, so later pages land in the right mode.
    for (auto* e : xml->getChildIterator())
        if (e->hasTagName("P") && e->getIntAttribute("p") == 0)
            queueEdit(e->getIntAttribute("f"), 0, e->getIntAttribute("v"));
    for (auto* e : xml->getChildIterator())
        if (e->hasTagName("P") && e->getIntAttribute("p") != 0)
            queueEdit(e->getIntAttribute("f"), e->getIntAttribute("p"), e->getIntAttribute("v"));

    // MIDI mappings.  A state with no container leaves the current map alone
    // (older QDV1 sessions); a container replaces it wholesale, empty included.
    if (auto* midiMap = xml->getChildByName("MIDI_MAP")) {
        clearAllMidiBindings();
        for (auto* e : midiMap->getChildWithTagNameIterator("M")) {
            const int kind = e->getIntAttribute("kind", -1);
            if (kind < (int) QvMidiTargetKind::parameter
                || kind > (int) QvMidiTargetKind::bypass)
                continue;
            QvMidiTarget target;
            if (kind == (int) QvMidiTargetKind::parameter)
                target = QvMidiTarget::param(e->getIntAttribute("cfg", -1),
                                             e->getIntAttribute("f", -1),
                                             e->getIntAttribute("mode", 0),
                                             e->getIntAttribute("pg", -1));
            else
                target.kind = (QvMidiTargetKind) kind;
            if (target.isValid())
                setMidiBinding(target, e->getIntAttribute("ch", 0), e->getIntAttribute("cc", 0));
        }
    }
}
