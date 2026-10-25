#include "../src/sharing/sharedfolders.h"
#include <chrono>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
void expect(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejected(F action)
{
    try { action(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Unsafe or invalid request was accepted");
}

struct Fixture
{
    fs::path root = fs::temp_directory_path() / ("mousedroid-folders-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path shared = root / "shared";
    fs::path outside = root / "outside";
    fs::path nested = shared / fs::u8path(u8"folder \u7528\u6237");
    Fixture()
    {
        fs::create_directories(nested);
        fs::create_directories(outside);
        std::ofstream(shared / "hello.txt") << "hello";
        std::ofstream(nested / "nested.txt") << "nested";
        std::ofstream(outside / "private.txt") << "not shared";
    }
    ~Fixture()
    {
        std::error_code e;
        // Only remove the exact files/directories created by this fixture.
        for (const auto& p : {shared / "link", shared / "hello.txt", nested / "nested.txt",
            outside / "private.txt", nested, shared, outside, root}) fs::remove(p, e);
    }
};

int main()
{
    try
    {
        Fixture f;
        Sharing::SharedFolders catalog;
        expect(catalog.listRoots().empty(), "No directories should be shared automatically");
        catalog.add("docs", "Selected documents", f.shared);
        const auto roots = catalog.listRoots();
        expect(roots.size() == 1 && roots[0].first == "docs" && roots[0].second == "Selected documents",
            "Peer root listing must contain aliases, not native paths");
        rejected([&] { catalog.add("docs", "duplicate", f.outside); });
        rejected([&] { catalog.list("unknown"); });
        for (const auto* name : {"..", ".", "", "../outside", "C:\\", "a/b", "a\\b", "a:b", "...", "folder "})
            rejected([&] { catalog.list("docs", {name}); });
        rejected([&] { catalog.list("docs", {std::string("bad\0name", 8)}); });
        rejected([&] { catalog.list("docs", {}, 0, 0); });
        rejected([&] { catalog.list("docs", {}, 0, 201); });
        rejected([&] { catalog.list("docs", {"hello.txt"}); });

        const auto first = catalog.list("docs", {}, 0, 1);
        const auto second = catalog.list("docs", {}, first.nextOffset, 1);
        expect(first.entries.size() == 1 && first.hasMore && second.entries.size() == 1 && !second.hasMore,
            "Pagination must bound returned metadata");
        expect(first.entries[0].name != second.entries[0].name, "Pages repeated the same item");
        const auto nested = catalog.list("docs", {u8"folder \u7528\u6237"});
        expect(nested.entries.size() == 1 && nested.entries[0].name == "nested.txt" &&
            nested.entries[0].kind == Sharing::EntryKind::File && nested.entries[0].size == 6,
            "Unicode folder traversal or file metadata failed");

        std::error_code e;
        fs::create_directory_symlink(f.outside, f.shared / "link", e);
        if (!e)
        {
            rejected([&] { catalog.list("docs", {"link"}); });
            const auto entries = catalog.list("docs").entries;
            for (const auto& entry : entries)
                if (entry.name == "link") expect(entry.kind == Sharing::EntryKind::Unsupported,
                    "Links must not be advertised as browsable directories");
        }
        else std::cout << "NOTE: directory symlink creation unavailable on this runner\n";

        catalog.remove("docs");
        rejected([&] { catalog.list("docs"); });
        expect(catalog.listRoots().empty(), "Revocation did not remove the root");
        std::cout << "PASS: opt-in roots, aliases, pagination, Unicode metadata, traversal rejection, links, revocation\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
