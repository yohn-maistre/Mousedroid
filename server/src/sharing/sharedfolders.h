#pragma once

#include <algorithm>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Sharing
{
    namespace fs = std::filesystem;

    struct SharedFolder
    {
        std::string id;
        std::string label;
        // Local-only: never serialize this path to a peer.
        fs::path path;
    };

    enum class EntryKind { File, Folder, Unsupported };

    struct FolderEntry
    {
        std::string name;
        EntryKind kind;
        std::uintmax_t size = 0;
    };

    struct FolderPage
    {
        std::vector<FolderEntry> entries;
        std::size_t nextOffset = 0;
        bool hasMore = false;
    };

    // Read-only catalog foundation. No network listener and no automatic roots:
    // callers must explicitly register a selected directory. Actual file reads
    // and writes need a separate handle-based permission/containment check.
    class SharedFolders
    {
    public:
        void add(const std::string& id, const std::string& label, const fs::path& path)
        {
            if (id.empty() || label.empty() || roots.count(id))
                throw std::runtime_error("Invalid or duplicate shared-folder ID");
            std::error_code error;
            if (isLink(path) || !fs::is_directory(path, error) || error)
                throw std::runtime_error("Select an accessible directory, not a link");
            const auto canonical = fs::canonical(path, error);
            if (error)
                throw std::runtime_error("Shared folder is unavailable");
            roots.emplace(id, SharedFolder{id, label, canonical});
        }

        void remove(const std::string& id) { roots.erase(id); }

        std::vector<std::pair<std::string, std::string>> listRoots() const
        {
            std::vector<std::pair<std::string, std::string>> result;
            for (const auto& item : roots)
                result.emplace_back(item.first, item.second.label);
            return result;
        }

        FolderPage list(const std::string& id, const std::vector<std::string>& segments = {},
            std::size_t offset = 0, std::size_t limit = 100) const
        {
            if (limit == 0 || limit > 200 || offset > 1000000)
                throw std::runtime_error("Invalid directory page");
            const auto found = roots.find(id);
            if (found == roots.end())
                throw std::runtime_error("Shared folder is unavailable or was revoked");
            auto path = found->second.path;
            checkDirectory(path);
            for (const auto& segment : segments)
            {
                checkSegment(segment);
                try { path /= fs::u8path(segment); }
                catch (const fs::filesystem_error&) { throw std::runtime_error("Invalid folder name"); }
                checkDirectory(path);
            }

            std::error_code error;
            fs::directory_iterator iterator(path, error), end;
            if (error)
                throw std::runtime_error("Cannot list this shared folder");
            FolderPage page;
            std::size_t index = 0;
            // Do not collect or sort the entire directory in memory. The UI can
            // request another bounded page, and refresh after directory changes.
            while (iterator != end)
            {
                if (index++ >= offset)
                {
                    if (page.entries.size() == limit)
                    {
                        page.hasMore = true;
                        break;
                    }
                    FolderEntry entry{iterator->path().filename().u8string(), EntryKind::Unsupported};
                    if (!isLink(iterator->path()))
                    {
                        const auto status = iterator->symlink_status(error);
                        if (!error && fs::is_directory(status))
                            entry.kind = EntryKind::Folder;
                        else if (!error && fs::is_regular_file(status))
                        {
                            entry.size = iterator->file_size(error);
                            if (!error) entry.kind = EntryKind::File;
                            else entry.size = 0;
                        }
                    }
                    page.entries.push_back(std::move(entry));
                }
                error.clear();
                iterator.increment(error);
                if (error) throw std::runtime_error("Directory changed or became unavailable");
            }
            page.nextOffset = offset + page.entries.size();
            return page;
        }

    private:
        std::map<std::string, SharedFolder> roots;

        static bool isLink(const fs::path& path)
        {
            #ifdef _WIN32
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                return true;
            #endif
            std::error_code error;
            return fs::is_symlink(fs::symlink_status(path, error));
        }

        static void checkDirectory(const fs::path& path)
        {
            std::error_code error;
            if (isLink(path) || !fs::is_directory(path, error) || error)
                throw std::runtime_error("Directory is unavailable or is a link");
        }

        static void checkSegment(const std::string& name)
        {
            if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
                name.find_first_of("/\\:") != std::string::npos ||
                std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
                throw std::runtime_error("Invalid folder name");
        }
    };
}
