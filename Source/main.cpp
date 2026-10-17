#include "Extractor.hpp"
#include "Resource.h"

#include <WinApp/WinApp.hpp>

#include <libSouls/Oodle26.hpp>

#include <cstdlib>
#include <format>

using namespace WinApp;
using UnpackER::Extractor;
namespace fs = std::filesystem;

namespace {
    fs::path GetExecutableDirectory() {
        std::wstring Path(MAX_PATH, L'\0');
        DWORD Length;
        while ((Length = GetModuleFileNameW(nullptr, Path.data(), static_cast<DWORD>(Path.size()))) == Path.size()) {
            Path.resize(Path.size() * 2);
        }
        Path.resize(Length);
        return fs::path(Path).parent_path();
    }

    fs::path GetDefaultGameDirectory() {
        const wchar_t* ProgramFiles86 = _wgetenv(L"ProgramFiles(x86)");
        if (!ProgramFiles86) return {};
        return fs::path(ProgramFiles86) / "Steam" / "steamapps" / "common" / "ELDEN RING" / "Game";
    }
}  // namespace

APP_ENTRY(HINSTANCE, HINSTANCE, PWSTR, int) {
    const fs::path DataPath = GetExecutableDirectory() / "Data";
    if (!exists(DataPath)) {
        MessageBoxW(nullptr, L"'Data' directory is missing.", L"UnpackER", MB_ICONERROR);
        return EXIT_FAILURE;
    }

    App App({.Title = L"UnpackER", .Width = 600, .Height = 200, .Resizable = false, .Icon = APPICON});

    const auto DefaultGamePath = GetDefaultGameDirectory().wstring();

    App.Add<Label>(L"Data Directory:", Rect {16, 16, 300, 20});
    auto& GamePath   = App.Add<TextBox>(Rect {16, 36, 528, 26}, TextBoxOptions {.Text = DefaultGamePath});
    auto& BrowseGame = App.Add<Button>(L"...", Rect {552, 35, 32, 28});

    App.Add<Label>(L"Output Directory:", Rect {16, 72, 300, 20});
    auto& OutPath   = App.Add<TextBox>(Rect {16, 92, 528, 26}, TextBoxOptions {.Text = DefaultGamePath});
    auto& BrowseOut = App.Add<Button>(L"...", Rect {552, 91, 32, 28});

    auto& Progress = App.Add<ProgressBar>(Rect {16, 134, 436, 24});
    auto& Extract  = App.Add<Button>(L"Extract", Rect {464, 132, 120, 28});
    auto& Status =
      App.Add<Label>(L"Nothing to do.", Rect {16, 168, 568, 20}, TextAlign::Left, TextOverflow::Path);

    // Hidden until the first file is extracted.
    Progress.SetVisible(false);

    Extractor Job(DataPath / "Key", DataPath / "Hash");

    BrowseGame.OnClick([&] {
        const auto Folder =
          FileDialog::PickFolder(App, {.Title = L"ELDEN RING Game Directory", .InitialFolder = GamePath.GetText()});
        if (Folder) {
            GamePath.SetText(Folder->wstring());
            OutPath.SetText(Folder->wstring());
        }
    });

    BrowseOut.OnClick([&] {
        const auto Folder = FileDialog::PickFolder(App, {.Title = L"Extract to...", .InitialFolder = OutPath.GetText()});
        if (Folder) { OutPath.SetText(Folder->wstring()); }
    });

    const auto Fail = [&](const std::wstring& Heading, const std::wstring& Text = {}) {
        App.ShowMessage(Heading, Text, MessageIcon::Error);
    };

    Extract.OnClick([&] {
        if (!exists(DataPath / "Hash")) return Fail(L"Missing 'Data/Hash' directory.");
        if (!exists(DataPath / "Key")) return Fail(L"Missing 'Data/Key' directory.");

        const fs::path GameDataPath = GamePath.GetText();
        if (GameDataPath.empty() || !exists(GameDataPath)) return Fail(L"Invalid game data directory provided.");

        const auto OodleDll = GameDataPath / "oo2core_6_win64.dll";
        if (!exists(OodleDll)) {
            return Fail(L"Missing oo2core_6_win64.dll.", L"Did you provide the correct game data path?");
        }
        Souls::Oodle26::SetDLLPath(OodleDll);
        if (!Souls::Oodle26::IsAvailable()) return Fail(L"Couldn't load oo2core_6_win64.dll.");

        const fs::path Destination = OutPath.GetText();
        std::error_code Ec;
        if (Destination.empty() || (fs::create_directories(Destination, Ec), Ec)) {
            return Fail(L"Couldn't create the output directory.", Destination.wstring());
        }

        Extract.SetEnabled(false);
        Progress.SetVisible(false);
        Status.SetText(L"Reading archive headers...");

        // The extractor reports from its worker threads; App.Post() hands each update to the UI thread.
        Job.Start(GameDataPath,
                  Destination,
                  {
                    .Counted =
                      [&] {
                          App.Post([&] {
                              Progress.SetRange(0, static_cast<int>(Job.GetTotal()));
                              Progress.SetValue(0);
                          });
                      },
                    .Progress =
                      [&] {
                          App.Post([&] {
                              const auto LastFile = Job.TakeLastFile();
                              Progress.SetValue(static_cast<int>(Job.GetDone()));
                              Progress.SetVisible(true);
                              Status.SetText(L"Extracted '" + LastFile.wstring() + L"'");
                          });
                      },
                    .Done =
                      [&] {
                          App.Post([&] {
                              Progress.SetValue(static_cast<int>(Job.GetDone()));
                              const auto Summary =
                                std::format(L"Extracted {} files ({} failed)", Job.GetDone(), Job.GetFailed());
                              Status.SetText(Summary);
                              App.ShowMessage(Summary);
                              Extract.SetEnabled(true);
                          });
                      },
                  });
    });

    App.OnClose([&] {
        Job.Stop();
        return true;
    });

    return App.Run();
}
