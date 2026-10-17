#include "Extractor.hpp"

#include <libSouls/BinaryReader.hpp>
#include <libSouls/Crypto.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace UnpackER {
    namespace {
        namespace fs = std::filesystem;

        constexpr std::array<const char*, 5> ArchiveNames = {"Data0", "Data1", "Data2", "Data3", "DLC"};

        std::vector<uint8_t> ReadAll(const fs::path& Path) {
            Souls::BinaryReader R(Path);
            return R.ReadBytes(static_cast<size_t>(R.Length()));
        }

        std::optional<std::string> ReadString(const fs::path& Path) {
            const std::ifstream File(Path);
            if (!File) return {};
            std::ostringstream Stream;
            Stream << File.rdbuf();
            return Stream.str();
        }

        std::unordered_map<std::string, std::string> GetRSAKeys(const fs::path& Path) {
            std::unordered_map<std::string, std::string> Keys;

            for (const auto& it : fs::directory_iterator(Path)) {
                if (it.is_regular_file() && it.path().extension() == ".pem") {
                    if (const auto Key = ReadString(it.path()); Key.has_value()) {
                        Keys.insert_or_assign(it.path().filename().stem().string(), *Key);
                    }
                }
            }

            return Keys;
        }

        // Elden Ring's 64-bit path hash: lowercased, '/'-separated, always with a leading '/'.
        uint64_t HashPath(std::string Path) {
            for (char& C : Path) {
                if (C == '\\') C = '/';
                else C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
            }
            if (Path.empty() || Path.front() != '/') Path.insert(Path.begin(), '/');

            uint64_t Hash = 0;
            for (const char C : Path)
                Hash = Hash * 0x85ull + static_cast<uint8_t>(C);
            return Hash;
        }

        // Loads Hash/<Name>.txt (one path per line) into a hash -> path lookup.
        std::unordered_map<uint64_t, std::string> LoadDictionary(const fs::path& Path) {
            std::unordered_map<uint64_t, std::string> Dict;
            std::ifstream In(Path);
            std::string Line;
            while (std::getline(In, Line)) {
                while (!Line.empty() && (Line.back() == '\r' || Line.back() == ' '))
                    Line.pop_back();
                if (Line.empty()) continue;
                Dict.emplace(HashPath(Line), Line);
            }
            return Dict;
        }

        void WriteFile(const fs::path& Path, const std::vector<uint8_t>& Bytes) {
            fs::create_directories(Path.parent_path());
            std::ofstream Out(Path, std::ios::binary);
            Out.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
        }
    }  // namespace

    Extractor::Extractor(fs::path KeyDir, fs::path HashDir) : _KeyDir(std::move(KeyDir)), _HashDir(std::move(HashDir)) {}

    void Extractor::Start(const fs::path& GameDataPath, const fs::path& OutPath, Events Handlers) {
        Stop();

        _Events = std::move(Handlers);
        _Archives.clear();
        _Total           = 0;
        _Done            = 0;
        _Failed          = 0;
        _ProgressPending = false;

        _Coordinator = std::jthread([this, GameDataPath, OutPath](const std::stop_token& St) {
            auto Keys = GetRSAKeys(_KeyDir);

            // Decrypt and parse every archive header in parallel to learn the total file count.
            {
                std::vector<std::jthread> Threads;
                for (const char* Name : ArchiveNames) {
                    const auto Key = Keys[Name];
                    if (Key.empty()) continue;
                    Threads.emplace_back([this, Name, &GameDataPath, Key] { Count(Name, GameDataPath, Key); });
                }
            }

            if (St.stop_requested()) return;
            if (_Events.Counted) _Events.Counted();

            // One extraction thread per archive.
            {
                std::vector<std::jthread> Threads;
                for (const auto& A : _Archives) {
                    Threads.emplace_back([this, &St, &A, &OutPath] { Extract(St, A, OutPath); });
                }
            }

            if (St.stop_requested()) return;
            if (_Events.Done) _Events.Done();
        });
    }

    void Extractor::Stop() {
        _Coordinator.request_stop();
        if (_Coordinator.joinable()) _Coordinator.join();
    }

    fs::path Extractor::TakeLastFile() {
        _ProgressPending = false;
        std::lock_guard Lock(_Mutex);
        return _LastFile;
    }

    void Extractor::Count(const std::string& Name, const fs::path& GameDataPath, const std::string& Key) {
        const fs::path BhdPath = (GameDataPath / Name).replace_extension(".bhd");
        const fs::path BdtPath = (GameDataPath / Name).replace_extension(".bdt");
        if (!exists(BhdPath) || !exists(BdtPath)) { return; }

        const auto EncryptedData    = ReadAll(BhdPath);
        std::vector<uint8_t> Header = Souls::Crypto::DecryptRsaBlocks(Key, EncryptedData);

        if (Header.empty()) { return; }

        Souls::BHD5 Bhd = Souls::BHD5::Read(Header, Souls::BHD5::Game::EldenRing);
        size_t Files    = 0;
        for (const auto& B : Bhd.Buckets) {
            Files += B.size();
        }

        _Total += static_cast<int64_t>(Files);

        {
            std::lock_guard Lock(_Mutex);
            _Archives.push_back({
              .Name    = Name,
              .Bhd     = std::move(Bhd),
              .BdtPath = BdtPath,
            });
        }
    }

    void Extractor::Extract(const std::stop_token& St, const Archive& A, const fs::path& OutPath) {
        const auto Dict = LoadDictionary((_HashDir / A.Name).replace_extension(".txt"));
        Souls::BinaryReader R(A.BdtPath);

        for (const auto& B : A.Bhd.Buckets) {
            for (const auto& File : B) {
                if (St.stop_requested()) return;

                try {
                    if (File.PaddedFileSize <= 0 || File.FileOffset + File.PaddedFileSize > R.Length()) { continue; }

                    fs::path FinalOut;
                    if (const auto it = Dict.find(File.FileNameHash); it != Dict.end()) {
                        FinalOut = OutPath / it->second.substr(1);  // remove beginning '/'
                    } else {
                        char Buffer[32];
                        std::snprintf(Buffer, sizeof(Buffer), "%016llX", static_cast<uint64_t>(File.FileNameHash));
                        FinalOut = OutPath / "_unknown" / A.Name / Buffer;
                    }

                    auto Bytes = File.ReadFile(R);
                    if (File.UnpaddedFileSize > 0 && static_cast<size_t>(File.UnpaddedFileSize) < Bytes.size()) {
                        Bytes.resize(static_cast<size_t>(File.UnpaddedFileSize));
                    }

                    WriteFile(FinalOut, Bytes);
                    ++_Done;

                    {
                        std::lock_guard Lock(_Mutex);
                        _LastFile = FinalOut.lexically_relative(OutPath);
                    }

                    // Coalesce: only notify if the UI has handled the previous one
                    if (!_ProgressPending.exchange(true) && _Events.Progress) { _Events.Progress(); }
                } catch (...) { ++_Failed; }
            }
        }
    }
}  // namespace UnpackER
