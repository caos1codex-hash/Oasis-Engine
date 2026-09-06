#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core.hpp"

namespace oasis {

inline constexpr std::size_t kMaxAssets = 1024;
inline constexpr std::uint64_t kMaxAssetBytes = 64ULL * 1024ULL * 1024ULL;

struct Asset {
    std::string id;
    std::string source;  // posix relativa: assets/source/<id>.glb | .hdr
    std::string cooked;  // posix relativa: assets/cooked/<id>.oasisasset
    std::string hash;    // 16 hex FNV-1a 64
    std::uint64_t bytes = 0;
    std::string kind = "model";  // "model" | "sky" (ausente en manifiestos viejos = model)
};

struct AssetList {
    std::vector<Asset> items;
};

bool AssetListLoad(const Project& proj, AssetList& out, Error& err);
bool AssetImportGlb(const Project& proj, const std::string& id,
                    const std::filesystem::path& source_path, Error& err);
bool AssetImportSky(const Project& proj, const std::string& id,
                    const std::filesystem::path& source_path, Error& err);
bool AssetConvertObj(const Project& proj, const std::string& id,
                     const std::filesystem::path& source_path, Error& err);
std::string AssetListToJson(const AssetList& assets);

}  // namespace oasis
