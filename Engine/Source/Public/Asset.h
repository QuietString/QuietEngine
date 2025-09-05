#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <filesystem>
#include "qmeta_runtime.h"

// Binary .qasset serializer/deserializer.
// Only QPROPERTY/QFUNCTION marked members are stored.
// Properties store values; functions store metadata (name/ret/params).

namespace qasset {

    // Magic + version for the .qasset format
    static constexpr uint32_t kMagic = 0x51534154; // 'Q''S''A''T' (QAST)
    static constexpr uint16_t kVersion = 1;

    // Decide default dir from TypeInfo.meta["Module"] -> "Engine/Contents" or "Game/Contents".
    std::filesystem::path DefaultAssetDirFor(const qmeta::TypeInfo& Ti);

    // Save object properties (values) + functions (metadata only) to a .qasset file.
    // If outPath is a directory, fileName is used; if outPath has extension, it's used directly.
    // Returns true on success.
    bool Save(const void* Obj, const qmeta::TypeInfo& Ti, const std::filesystem::path& OutPath, const std::string& FileNameIfDir = "");

    // Load object properties (values) from a .qasset file into an existing instance.
    // Functions section (if present) is read and ignored.
    // Returns true on success.
    bool Load(void* Obj, const qmeta::TypeInfo& Ti, const std::filesystem::path& InFile);

    // Optional helpers (throwing versions)
    void SaveOrThrow(const void* Obj, const qmeta::TypeInfo& Ti, const std::filesystem::path& OutPath, const std::string& FileNameIfDir = "");
    void LoadOrThrow(void* Obj, const qmeta::TypeInfo& Ti, const std::filesystem::path& InFile);

}
