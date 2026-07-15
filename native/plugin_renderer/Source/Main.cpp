#include "CommandLine.h"
#include "PluginRenderer.h"

#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String (argv[i]));

    if (args.isEmpty())
    {
        std::cerr << "Usage: plugin_renderer --plugin PATH --input IN.wav --output OUT.wav --preset STATE.aupreset [--tail-silence SECS]" << std::endl;
        return 1;
    }

    CommandLineOptions options;
    juce::String error;

    if (! options.parse (args, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    return PluginRenderer::run (options);
}
