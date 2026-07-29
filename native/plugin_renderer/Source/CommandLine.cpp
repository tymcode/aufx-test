#include "CommandLine.h"

namespace
{
    bool looksLikeFlag (const juce::String& arg)
    {
        return arg.startsWithChar ('-');
    }
}

bool CommandLineOptions::parse (const juce::StringArray& args, juce::String& error)
{
    for (int i = 0; i < args.size(); ++i)
    {
        const auto arg = args[i];

        if (arg == "--plugin")
        {
            if (++i >= args.size()) { error = "Missing value for --plugin"; return false; }
            pluginRef = args[i];
        }
        else if (arg == "--input")
        {
            if (++i >= args.size()) { error = "Missing value for --input"; return false; }
            inputPath = juce::File (args[i]);
        }
        else if (arg == "--output")
        {
            if (++i >= args.size()) { error = "Missing value for --output"; return false; }
            outputPath = juce::File (args[i]);
        }
        else if (arg == "--preset")
        {
            if (++i >= args.size()) { error = "Missing value for --preset"; return false; }
            presetPath = juce::File (args[i]);
        }
        else if (arg == "--param")
        {
            if (++i >= args.size()) { error = "Missing value for --param"; return false; }
            const auto& spec = args[i];
            const auto eq = spec.indexOfChar ('=');
            if (eq <= 0) { error = "Expected --param name=value"; return false; }
            paramOverrides.emplace_back (spec.substring (0, eq).trim(),
                                         spec.substring (eq + 1).trim());
        }
        else if (arg == "--sample-rate")
        {
            if (++i >= args.size()) { error = "Missing value for --sample-rate"; return false; }
            sampleRate = args[i].getDoubleValue();
        }
        else if (arg == "--block-size")
        {
            if (++i >= args.size()) { error = "Missing value for --block-size"; return false; }
            blockSize = args[i].getIntValue();
        }
        else if (arg == "--tail-silence")
        {
            if (++i >= args.size()) { error = "Missing value for --tail-silence"; return false; }
            tailSilenceSeconds = args[i].getDoubleValue();
        }
        else if (arg == "--silence-threshold-db")
        {
            if (++i >= args.size()) { error = "Missing value for --silence-threshold-db"; return false; }
            silenceThresholdDb = args[i].getDoubleValue();
        }
        else if (arg == "--max-tail")
        {
            if (++i >= args.size()) { error = "Missing value for --max-tail"; return false; }
            maxTailSeconds = args[i].getDoubleValue();
        }
        else if (arg == "--dump-parameters")
        {
            dumpParameters = true;
        }
        else if (arg == "--format")
        {
            if (++i >= args.size()) { error = "Missing value for --format"; return false; }
            jsonOutput = args[i].equalsIgnoreCase ("json");
        }
        else if (looksLikeFlag (arg))
        {
            error = "Unknown option: " + arg;
            return false;
        }
        else
        {
            error = "Unexpected argument: " + arg;
            return false;
        }
    }

    return true;
}

bool CommandLineOptions::validateForRender (juce::String& error) const
{
    if (pluginRef.trim().isEmpty())
    {
        error = "Missing --plugin";
        return false;
    }

    if (! pluginRef.startsWithIgnoreCase ("AudioUnit:"))
    {
        const juce::File pluginPath (pluginRef);
        if (! pluginPath.exists())
        {
            error = "Plugin not found: " + pluginPath.getFullPathName();
            return false;
        }
    }

    if (! inputPath.existsAsFile())
    {
        error = "Input WAV not found: " + inputPath.getFullPathName();
        return false;
    }

    if (outputPath.getFullPathName().isEmpty())
    {
        error = "Missing --output path";
        return false;
    }

    if (! presetPath.existsAsFile())
    {
        error = "Preset not found: " + presetPath.getFullPathName();
        return false;
    }

    if (blockSize <= 0)
    {
        error = "Block size must be > 0";
        return false;
    }

    if (tailSilenceSeconds < 0.0)
    {
        error = "Tail silence must be >= 0";
        return false;
    }

    if (maxTailSeconds <= 0.0)
    {
        error = "Max tail must be > 0";
        return false;
    }

    return true;
}

bool CommandLineOptions::validateForDump (juce::String& error) const
{
    if (pluginRef.trim().isEmpty())
    {
        error = "Missing --plugin";
        return false;
    }

    if (! pluginRef.startsWithIgnoreCase ("AudioUnit:"))
    {
        const juce::File pluginPath (pluginRef);
        if (! pluginPath.exists())
        {
            error = "Plugin not found: " + pluginPath.getFullPathName();
            return false;
        }
    }

    if (presetPath.existsAsFile() == false && presetPath.getFullPathName().isNotEmpty())
    {
        error = "Preset not found: " + presetPath.getFullPathName();
        return false;
    }

    return true;
}
