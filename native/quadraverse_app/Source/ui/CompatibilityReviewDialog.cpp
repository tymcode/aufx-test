#include "CompatibilityReviewDialog.h"
#include "Utf8.h"
#include "../domain/PatchTranslator.h"

namespace qverse
{

bool runCompatibilityReview (TranslationReport& report, juce::Component* parent)
{
    if (report.isClean())
        return true;

    juce::String text;
    text << utf8 ("Cross-device translation from ")
         << utf8 (toString (report.sourceModel)) << utf8 (" to ")
         << utf8 (toString (report.targetModel)) << utf8 (":\n\n");

    for (const auto& item : report.items)
    {
        const char* cls = item.classification == TranslationClass::approximated
            ? "approximated" : "no equivalent";
        text << utf8 ("[") << utf8 (cls) << utf8 ("] ") << item.section << utf8 (" / ") << item.name
             << utf8 ("  src=") << item.sourceValue
             << utf8 ("  proposed=") << item.proposedValue
             << utf8 ("  default=") << item.targetDefault << utf8 ("\n");
    }
    text << utf8 ("\nAccept proposed values? (Cancel aborts the transfer.)");

    juce::AlertWindow w (utf8 ("Compatibility Review"), text, juce::AlertWindow::WarningIcon, parent);
    w.addButton (utf8 ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w.addButton (utf8 ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    return w.runModalLoop() == 1;
}

bool translateIfNeeded (const QuadraverbProgram& source,
                        DeviceModel target,
                        QuadraverbProgram& out,
                        juce::Component* parent)
{
    auto report = PatchTranslator::translate (source, target, out);
    if (report.isClean())
        return true;
    return runCompatibilityReview (report, parent);
}

} // namespace qverse
