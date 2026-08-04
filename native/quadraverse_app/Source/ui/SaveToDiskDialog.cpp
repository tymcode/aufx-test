#include "SaveToDiskDialog.h"
#include "Utf8.h"
#include "../formats/Qdv1StateIO.h"
#include "../formats/SyxIO.h"
#include "../domain/AlesisCodec.h"
#include "../QuadraversePrefs.h"

namespace qverse
{

SaveToDiskResult runSaveToDiskDialog (const QuadraverbProgram& program,
                                      const juce::File& patchDir,
                                      juce::Component* parent)
{
    SaveToDiskResult result;
    juce::AlertWindow w (utf8 ("Save to Disk"),
                         utf8 ("Choose outputs for this patch context:"),
                         juce::AlertWindow::QuestionIcon,
                         parent);
    w.addTextEditor ("name",
                     program.name.isNotEmpty() ? program.name.trim() : utf8 ("Untitled"),
                     utf8 ("Name"));
    w.addComboBox ("opts", {}, utf8 ("Outputs"));
    w.addTextEditor ("flags",
                     (QuadraversePrefs::getSaveQdv1Default() ? "Q" : "")
                         + juce::String (QuadraversePrefs::getSaveSyxDefault() ? "S" : "")
                         + juce::String (QuadraversePrefs::getSaveAupresetDefault() ? "A" : ""),
                     utf8 ("Flags: Q=QDV1 preset, S=.syx, A=.aupreset"));

    w.addButton (utf8 ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w.addButton (utf8 ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (w.runModalLoop() != 1)
        return result;

    const auto name = w.getTextEditorContents ("name").trim();
    const auto flags = w.getTextEditorContents ("flags").toUpperCase();
    const bool doQ = flags.containsChar ('Q');
    const bool doS = flags.containsChar ('S');
    const bool doA = flags.containsChar ('A');

    QuadraversePrefs::setSaveQdv1Default (doQ);
    QuadraversePrefs::setSaveSyxDefault (doS);
    QuadraversePrefs::setSaveAupresetDefault (doA);

    patchDir.createDirectory();
    juce::StringArray notes;
    juce::String error;

    if (doQ)
    {
        juce::String id;
        if (Qdv1StateIO::saveUserPreset (name, program, id, error))
        {
            result.wroteQdv1 = true;
            notes.add (utf8 ("QDV-1 preset ") + id);
        }
        else
            notes.add (utf8 ("QDV-1 failed: ") + error);
    }

    if (doS)
    {
        auto safe = juce::File::createLegalFileName (name);
        if (safe.isEmpty())
            safe = "patch";
        const auto file = patchDir.getChildFile (safe + ".syx");
        if (SyxIO::saveSingle (file, program, AlesisCodec::kEditBuffer, error))
        {
            result.wroteSyx = true;
            notes.add (file.getFileName());
        }
        else
            notes.add (utf8 (".syx failed: ") + error);
    }

    if (doA)
    {
        auto safe = juce::File::createLegalFileName (name);
        if (safe.isEmpty())
            safe = "patch";
        const auto file = patchDir.getChildFile (safe + ".aupreset");
        if (Qdv1StateIO::saveAupreset (file, program, error))
        {
            result.wroteAupreset = true;
            notes.add (file.getFileName());
        }
        else
            notes.add (utf8 (".aupreset failed: ") + error);
    }

    result.ok = result.wroteQdv1 || result.wroteSyx || result.wroteAupreset;
    result.message = notes.joinIntoString (", ");
    return result;
}

} // namespace qverse
