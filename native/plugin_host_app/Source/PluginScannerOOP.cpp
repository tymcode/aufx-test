#include "PluginScannerOOP.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>

juce::String oopScanFailureReasonString (OopScanFailureReason reason)
{
    switch (reason)
    {
        case OopScanFailureReason::none:           return "ok";
        case OopScanFailureReason::timeout:        return "timeout";
        case OopScanFailureReason::connectionLost: return "subprocess_lost";
        case OopScanFailureReason::sendFailed:     return "send_failed";
        case OopScanFailureReason::cancelled:      return "cancelled";
    }

    return "unknown";
}

//==============================================================================
namespace
{
    std::atomic<int> gWorkerCrashLogFd { -1 };
    char gLastScannedPlugin[1024] = {};

    void setLastScannedPlugin (const juce::String& identifier)
    {
        const auto* utf8 = identifier.toRawUTF8();
        std::strncpy (gLastScannedPlugin, utf8 != nullptr ? utf8 : "", sizeof (gLastScannedPlugin) - 1);
        gLastScannedPlugin[sizeof (gLastScannedPlugin) - 1] = '\0';
    }

    void writeCrashLogLine (int fd, const char* text)
    {
        if (fd < 0 || text == nullptr)
            return;

        const auto len = std::strlen (text);
        if (len > 0)
            (void) write (fd, text, len);
    }

    /** Fatal-signal handler for the OOP scanner worker only.
        Logs briefly (async-signal-safe) and _exit()s so macOS Crash Reporter
        usually does not present a dialog for plugin aborts during scan. */
    void scannerWorkerFatalSignal (int signalNumber)
    {
        const int fd = gWorkerCrashLogFd.load (std::memory_order_relaxed);

        writeCrashLogLine (fd, "scanner-worker fatal signal ");
        {
            char digits[16];
            int n = signalNumber;
            if (n < 0)
                n = 0;
            int i = 0;
            do
            {
                digits[i++] = char ('0' + (n % 10));
                n /= 10;
            }
            while (n > 0 && i < 15);
            while (i > 0)
            {
                const char c = digits[--i];
                (void) write (fd, &c, 1);
            }
        }
        writeCrashLogLine (fd, " while scanning: ");
        writeCrashLogLine (fd, gLastScannedPlugin[0] != '\0' ? gLastScannedPlugin : "(unknown)");
        writeCrashLogLine (fd, "\n");

        _exit (128 + signalNumber);
    }

    void installScannerWorkerCrashGuards()
    {
        juce::File logFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                 .getChildFile ("AU Effects Explorer")
                                 .getChildFile ("plugin-scan-worker-crashes.log");
        logFile.getParentDirectory().createDirectory();

        const int fd = ::open (logFile.getFullPathName().toRawUTF8(),
                               O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
                               0644);
        if (fd >= 0)
        {
            gWorkerCrashLogFd.store (fd, std::memory_order_relaxed);
            const auto stamp = juce::Time::getCurrentTime().toString (true, true, true, true);
            const auto header = "---- scanner worker start " + stamp + " pid="
                                + juce::String (::getpid()) + " ----\n";
            const auto* utf8 = header.toRawUTF8();
            (void) write (fd, utf8, std::strlen (utf8));
        }

        struct sigaction action {};
        action.sa_handler = scannerWorkerFatalSignal;
        sigemptyset (&action.sa_mask);
        action.sa_flags = 0; // no SA_RESTART; never return from handler

        for (int signalNumber : { SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP })
            sigaction (signalNumber, &action, nullptr);
    }
}

//==============================================================================
class OutOfProcessPluginScanner::Superprocess final : private juce::ChildProcessCoordinator
{
public:
    Superprocess()
    {
        launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                             kAuPluginScannerProcessUID,
                             0,
                             0);
    }

    enum class State
    {
        timeout,
        gotResult,
        connectionLost,
    };

    struct Response
    {
        State state {};
        std::unique_ptr<juce::XmlElement> xml;
    };

    Response getResponse()
    {
        std::unique_lock<std::mutex> lock { mutex };

        if (! condvar.wait_for (lock, std::chrono::milliseconds { 50 }, [&]
                                {
                                    return gotResult || connectionLost;
                                }))
            return { State::timeout, nullptr };

        const auto state = connectionLost ? State::connectionLost : State::gotResult;
        connectionLost = false;
        gotResult = false;

        return { state, std::move (pluginDescription) };
    }

    using juce::ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        pluginDescription = juce::parseXML (mb.toString());
        gotResult = true;
        condvar.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        connectionLost = true;
        condvar.notify_one();
    }

    std::mutex mutex;
    std::condition_variable condvar;
    std::unique_ptr<juce::XmlElement> pluginDescription;
    bool connectionLost = false;
    bool gotResult = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Superprocess)
};

//==============================================================================
OutOfProcessPluginScanner::OutOfProcessPluginScanner (std::atomic<bool>* cancelFlagIn,
                                                      int pluginTimeoutMsIn)
    : cancelFlag (cancelFlagIn),
      pluginTimeoutMs (pluginTimeoutMsIn > 0 ? pluginTimeoutMsIn : kAuPluginScanTimeoutMs)
{
}

OutOfProcessPluginScanner::~OutOfProcessPluginScanner() = default;

bool OutOfProcessPluginScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                                    juce::OwnedArray<juce::PluginDescription>& result,
                                                    const juce::String& fileOrIdentifier)
{
    if (addPluginDescriptions (format.getName(), fileOrIdentifier, result))
        return true;

    // Subprocess crashed, hung, or became unreachable - tear it down for the next plugin.
    superprocess = nullptr;
    return false;
}

void OutOfProcessPluginScanner::scanFinished()
{
    superprocess = nullptr;
}

bool OutOfProcessPluginScanner::addPluginDescriptions (const juce::String& formatName,
                                                       const juce::String& fileOrIdentifier,
                                                       juce::OwnedArray<juce::PluginDescription>& result)
{
    lastFailureReason = OopScanFailureReason::none;
    lastScanDurationMs = 0;

    const auto scanStarted = std::chrono::steady_clock::now();

    if (superprocess == nullptr)
        superprocess = std::make_unique<Superprocess>();

    juce::MemoryBlock block;
    juce::MemoryOutputStream stream { block, true };
    stream.writeString (formatName);
    stream.writeString (fileOrIdentifier);

    if (! superprocess->sendMessageToWorker (block))
    {
        lastFailureReason = OopScanFailureReason::sendFailed;
        lastScanDurationMs = (int) std::chrono::duration_cast<std::chrono::milliseconds> (
                                   std::chrono::steady_clock::now() - scanStarted)
                                   .count();
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds (pluginTimeoutMs);

    for (;;)
    {
        if (shouldExit() || (cancelFlag != nullptr && cancelFlag->load()))
        {
            lastFailureReason = OopScanFailureReason::cancelled;
            lastScanDurationMs = (int) std::chrono::duration_cast<std::chrono::milliseconds> (
                                       std::chrono::steady_clock::now() - scanStarted)
                                       .count();
            return true;
        }

        if (std::chrono::steady_clock::now() >= deadline)
        {
            // Hung plugin: kill the worker so the next scan can proceed.
            superprocess = nullptr;
            lastFailureReason = OopScanFailureReason::timeout;
            lastScanDurationMs = (int) std::chrono::duration_cast<std::chrono::milliseconds> (
                                       std::chrono::steady_clock::now() - scanStarted)
                                       .count();
            return false;
        }

        const auto response = superprocess->getResponse();

        if (response.state == Superprocess::State::timeout)
            continue;

        lastScanDurationMs = (int) std::chrono::duration_cast<std::chrono::milliseconds> (
                                   std::chrono::steady_clock::now() - scanStarted)
                                   .count();

        if (response.xml != nullptr)
        {
            for (const auto* item : response.xml->getChildIterator())
            {
                auto desc = std::make_unique<juce::PluginDescription>();

                if (desc->loadFromXml (*item))
                    result.add (std::move (desc));
            }
        }

        if (response.state == Superprocess::State::gotResult)
            return true;

        lastFailureReason = OopScanFailureReason::connectionLost;
        return false;
    }
}

//==============================================================================
class PluginScannerSubprocess final : private juce::ChildProcessWorker,
                                      private juce::AsyncUpdater
{
public:
    PluginScannerSubprocess()
    {
        juce::addDefaultFormatsToManager (formatManager);
    }

    using juce::ChildProcessWorker::initialiseFromCommandLine;

private:
    void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
    {
        if (mb.isEmpty())
            return;

        const std::lock_guard<std::mutex> lock (mutex);

        if (const auto results = doScan (mb); ! results.isEmpty())
        {
            sendResults (results);
        }
        else
        {
            // Some formats need the message thread - defer and try again there.
            pendingBlocks.emplace (mb);
            triggerAsyncUpdate();
        }
    }

    void handleConnectionLost() override
    {
        juce::JUCEApplicationBase::quit();
    }

    void handleAsyncUpdate() override
    {
        for (;;)
        {
            const std::lock_guard<std::mutex> lock (mutex);

            if (pendingBlocks.empty())
                return;

            sendResults (doScan (pendingBlocks.front()));
            pendingBlocks.pop();
        }
    }

    juce::OwnedArray<juce::PluginDescription> doScan (const juce::MemoryBlock& block)
    {
        juce::MemoryInputStream stream { block, false };
        const auto formatName = stream.readString();
        const auto identifier = stream.readString();
        setLastScannedPlugin (identifier);

        juce::PluginDescription pd;
        pd.fileOrIdentifier = identifier;
        pd.uniqueId = pd.deprecatedUid = 0;

        juce::AudioPluginFormat* matchingFormat = nullptr;
        for (auto* format : formatManager.getFormats())
        {
            if (format->getName() == formatName)
            {
                matchingFormat = format;
                break;
            }
        }

        juce::OwnedArray<juce::PluginDescription> results;

        if (matchingFormat != nullptr
            && (juce::MessageManager::getInstance()->isThisTheMessageThread()
                || matchingFormat->requiresUnblockedMessageThreadDuringCreation (pd)))
        {
            matchingFormat->findAllTypesForFile (results, identifier);
        }

        return results;
    }

    void sendResults (const juce::OwnedArray<juce::PluginDescription>& results)
    {
        juce::XmlElement xml ("LIST");

        for (const auto& desc : results)
            xml.addChildElement (desc->createXml().release());

        const auto str = xml.toString();
        sendMessageToCoordinator ({ str.toRawUTF8(), str.getNumBytesAsUTF8() });
    }

    std::mutex mutex;
    std::queue<juce::MemoryBlock> pendingBlocks;
    juce::AudioPluginFormatManager formatManager;
};

bool tryInitialiseAsPluginScannerWorker (const juce::String& commandLine)
{
    auto scannerSubprocess = std::make_unique<PluginScannerSubprocess>();

    if (! scannerSubprocess->initialiseFromCommandLine (commandLine, kAuPluginScannerProcessUID))
        return false;

#if JUCE_MAC
    aufxSetScannerWorkerProcessName();
#endif
    installScannerWorkerCrashGuards();

    // Keep the worker alive for the lifetime of the process.
    static std::unique_ptr<PluginScannerSubprocess> stored;
    stored = std::move (scannerSubprocess);
    return true;
}
