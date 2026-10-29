// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Minimal shim replacing shadPS4's common/io_file.h: write-only file used for shader dumps.

#pragma once

#include <cstdio>
#include <filesystem>
#include <span>
#include <string_view>
#include "common/types.h"

namespace Common::FS {

enum class FileAccessMode { Read, Write, Create, Append };
enum class FileType { BinaryFile, TextFile };

class IOFile {
public:
    IOFile(const std::filesystem::path& path, FileAccessMode mode,
           FileType type = FileType::BinaryFile) {
        const char* m = mode == FileAccessMode::Read     ? "rb"
                        : mode == FileAccessMode::Append ? "ab"
                                                         : "wb";
        file = std::fopen(path.string().c_str(), m);
    }
    ~IOFile() {
        if (file) {
            std::fclose(file);
        }
    }
    IOFile(const IOFile&) = delete;
    IOFile& operator=(const IOFile&) = delete;

    bool IsOpen() const {
        return file != nullptr;
    }
    size_t WriteString(std::string_view s) const {
        return file ? std::fwrite(s.data(), 1, s.size(), file) : 0;
    }
    template <typename T>
    size_t WriteRaw(const void* data, size_t count) const {
        return file ? std::fwrite(data, sizeof(T), count, file) : 0;
    }
    template <typename T>
    size_t WriteSpan(std::span<const T> data) const {
        return file ? std::fwrite(data.data(), sizeof(T), data.size(), file) : 0;
    }

private:
    std::FILE* file{};
};

} // namespace Common::FS
