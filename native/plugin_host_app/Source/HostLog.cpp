#include "HostLog.h"

namespace
{
    juce::CriticalSection lock;
    juce::File currentLogFile;
    std::unique_ptr<juce::FileOutputStream> stream;

    juce::String timestamp()
    {
        return juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
    }
}

void HostLog::open (const juce::File& logFile)
{
    const juce::ScopedLock sl (lock);

    stream.reset();
    currentLogFile = logFile;

    if (currentLogFile.getFullPathName().isEmpty())
        return;

    currentLogFile.getParentDirectory().createDirectory();

    auto owned = std::make_unique<juce::FileOutputStream> (currentLogFile);
    if (! owned->openedOk())
    {
        currentLogFile = juce::File();
        return;
    }

    stream = std::move (owned);
    stream->setPosition (currentLogFile.getSize());
    stream->writeText ("\n==== Plugin Host session " + timestamp() + " ====\n", false, false, nullptr);
    stream->flush();
}

void HostLog::close()
{
    const juce::ScopedLock sl (lock);
    stream.reset();
    currentLogFile = juce::File();
}

void HostLog::info (const juce::String& message)
{
    write ("INFO", message);
}

void HostLog::error (const juce::String& message)
{
    write ("ERROR", message);
}

juce::File HostLog::getLogFile()
{
    const juce::ScopedLock sl (lock);
    return currentLogFile;
}

void HostLog::write (const juce::String& level, const juce::String& message)
{
    const auto line = "[" + timestamp() + "] " + level + "  " + message.trim() + "\n";

    // Always mirror to the JUCE logger (console / system log).
    juce::Logger::writeToLog (line.trimEnd());

    const juce::ScopedLock sl (lock);
    if (stream == nullptr)
        return;

    stream->writeText (line, false, false, nullptr);
    stream->flush();
}
