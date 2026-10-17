#pragma once

#include <libSouls/Formats/BHD5.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace UnpackER {
    /// Unpacks ELDEN RING's encrypted BHD5/BDT archives on background threads. Knows nothing about the UI: progress is
    /// reported through Events and read back with the getters.
    class Extractor {
    public:
        /// Every event fires on a worker thread; marshal to the UI thread before touching any controls.
        struct Events {
            /// Every archive header has been read, so GetTotal() is final.
            std::function<void()> Counted;
            /// A file finished extracting. Coalesced: it doesn't fire again until TakeLastFile() is called.
            std::function<void()> Progress;
            /// Every archive has been extracted. Not fired if the job was stopped.
            std::function<void()> Done;
        };

        Extractor(std::filesystem::path KeyDir, std::filesystem::path HashDir);
        ~Extractor() { Stop(); }

        Extractor(const Extractor&)            = delete;
        Extractor& operator=(const Extractor&) = delete;

        /// Starts extracting every archive in GameDataPath into OutPath. Stops any job that is still running first.
        void Start(const std::filesystem::path& GameDataPath, const std::filesystem::path& OutPath, Events Handlers);
        /// Cancels the running job (if any) and waits for its threads to finish.
        void Stop();

        [[nodiscard]] int64_t GetTotal() const { return _Total; }
        [[nodiscard]] int64_t GetDone() const { return _Done; }
        [[nodiscard]] int64_t GetFailed() const { return _Failed; }

        /// The most recently extracted file, relative to the output directory. Re-arms the Progress event.
        std::filesystem::path TakeLastFile();

    private:
        struct Archive {
            std::string Name;
            Souls::BHD5 Bhd;
            std::filesystem::path BdtPath;
        };

        void Count(const std::string& Name, const std::filesystem::path& GameDataPath, const std::string& Key);
        void Extract(const std::stop_token& St, const Archive& A, const std::filesystem::path& OutPath);

        std::filesystem::path _KeyDir;
        std::filesystem::path _HashDir;
        Events _Events;

        std::vector<Archive> _Archives;
        std::mutex _Mutex;
        std::atomic<int64_t> _Total {0};
        std::atomic<int64_t> _Done {0};
        std::atomic<int64_t> _Failed {0};
        std::atomic<bool> _ProgressPending {false};
        std::filesystem::path _LastFile;

        // Declared last so it is joined before any of the state above is destroyed.
        std::jthread _Coordinator;
    };
}  // namespace UnpackER
