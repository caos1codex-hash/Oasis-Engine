#include "assets.hpp"

#include "cJSON.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace oasis {
namespace {

std::filesystem::path RegistryPath(const Project& proj) { return proj.root / "assets" / "oasis.assets.json"; }

// Escritura atómica compartida: ver core.hpp (CurrentProcessId/TempPathFor/
// AtomicReplaceFile). Se eliminaron los duplicados ProcId/g_asset_tmp/
// AtomicReplaceAsset/TempFor para usar el contador único de core.cpp.

bool HasTraversal(const std::string& rel) {
    if (rel.empty()) return true;
    if (rel[0] == '/' || rel[0] == '\\') return true;
    if (rel.find("..") != std::string::npos) return true;
    if (rel.find(':') != std::string::npos) return true;
    if (rel.find('\\') != std::string::npos) return true;  // manifiesto usa '/'
    return false;
}

std::uint32_t ReadU32Le(const unsigned char b[4]) {
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8U) |
           (static_cast<std::uint32_t>(b[2]) << 16U) | (static_cast<std::uint32_t>(b[3]) << 24U);
}

struct CJsonDel {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDel>;

bool WriteRegistry(const Project& proj, const AssetList& assets, Error& err) {
    std::ostringstream oss;
    oss << "{\n  \"format\": 1,\n  \"assets\": [\n";
    for (std::size_t i = 0; i < assets.items.size(); ++i) {
        const Asset& a = assets.items[i];
        oss << "    {\"id\": \"" << JsonEscape(a.id) << "\", \"source\": \"" << JsonEscape(a.source)
            << "\", \"cooked\": \"" << JsonEscape(a.cooked) << "\", \"hash\": \"" << JsonEscape(a.hash)
            << "\", \"bytes\": " << a.bytes << ", \"kind\": \"" << JsonEscape(a.kind) << "\"}"
            << (i + 1 == assets.items.size() ? "" : ",") << "\n";
    }
    oss << "  ]\n}\n";
    std::string text = oss.str();
    std::filesystem::path dst = RegistryPath(proj);
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    std::filesystem::path tmp = TempPathFor(dst);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err.set("IO", "No se pudo guardar el manifiesto de assets.");
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            err.set("IO", "No se pudo guardar el manifiesto de assets.");
            return false;
        }
        out.close();
    }
    if (!AtomicReplaceFile(tmp, dst, err)) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool CopyAndValidateGlb(const std::filesystem::path& src, const std::filesystem::path& dst,
                        std::uint64_t& out_bytes, std::string& out_hash, Error& err) {
    std::error_code ec;
    std::uintmax_t fsize = std::filesystem::file_size(src, ec);
    if (ec) {
        err.set("NOT_FOUND", "No se pudo abrir el asset fuente '" + src.string() + "'.");
        return false;
    }
    if (fsize < 20 || fsize > kMaxAssetBytes) {
        err.set("BAD_FORMAT", "El asset debe ser un GLB 2.0 válido de menos de 64MB.");
        return false;
    }
    std::ifstream in(src, std::ios::binary);
    if (!in) {
        err.set("NOT_FOUND", "No se pudo abrir el asset fuente '" + src.string() + "'.");
        return false;
    }
    std::array<unsigned char, 20> head{};
    in.read(reinterpret_cast<char*>(head.data()), 20);
    if (static_cast<std::size_t>(in.gcount()) != 20) {
        err.set("BAD_FORMAT", "El asset debe ser un GLB 2.0 válido y autocontenido.");
        return false;
    }
    std::uint32_t json_len = ReadU32Le(head.data() + 12);
    if (std::memcmp(head.data(), "glTF", 4) != 0 || ReadU32Le(head.data() + 4) != 2U ||
        ReadU32Le(head.data() + 8) != static_cast<std::uint32_t>(fsize) ||
        std::memcmp(head.data() + 16, "JSON", 4) != 0 || json_len > fsize - 20U) {
        err.set("BAD_FORMAT", "El asset debe ser un GLB 2.0 válido y autocontenido.");
        return false;
    }
    in.close();
    // Copia con hash FNV-1a 64
    std::uint64_t h = 1469598103934665603ULL;
    in.open(src, std::ios::binary);
    if (!in) {
        err.set("IO", "No se pudo leer el asset GLB.");
        return false;
    }
    std::error_code ec2;
    std::filesystem::create_directories(dst.parent_path(), ec2);
    std::filesystem::path tmp = TempPathFor(dst);
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        err.set("IO", "No se pudo preparar el asset importado.");
        return false;
    }
    std::array<char, 8192> buf{};
    std::uint64_t total = 0;
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize got = in.gcount();
        if (got < 0) got = 0;
        for (std::streamsize i = 0; i < got; ++i) {
            h ^= static_cast<unsigned char>(buf[static_cast<std::size_t>(i)]);
            h *= 1099511628211ULL;
        }
        if (got > 0) {
            out.write(buf.data(), got);
            if (!out) {
                in.close();
                out.close();
                std::filesystem::remove(tmp, ec2);
                err.set("IO", "No se pudo copiar el asset importado.");
                return false;
            }
            total += static_cast<std::uint64_t>(got);
        }
    }
    if (in.bad()) {
        in.close();
        out.close();
        std::filesystem::remove(tmp, ec2);
        err.set("IO", "No se pudo copiar el asset importado.");
        return false;
    }
    out.flush();
    out.close();
    in.close();
    if (!out || total != fsize) {
        std::filesystem::remove(tmp, ec2);
        err.set("IO", "No se pudo copiar el asset importado.");
        return false;
    }
    if (!AtomicReplaceFile(tmp, dst, err)) {
        std::filesystem::remove(tmp, ec2);
        return false;
    }
    out_bytes = fsize;
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    out_hash.assign(hex, 16);
    return true;
}

bool WriteCooked(const Project& proj, const Asset& a, const char* type, Error& err) {
    std::filesystem::path rel(a.cooked, std::filesystem::path::format::generic_format);
    std::filesystem::path dst = proj.root / rel;
    std::ostringstream oss;
    oss << "{\"format\":1,\"type\":\"" << type << "\",\"source\":\"" << JsonEscape(a.source)
        << "\",\"hash\":\"" << JsonEscape(a.hash) << "\",\"bytes\":" << a.bytes << "}\n";
    std::string text = oss.str();
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    std::filesystem::path tmp = TempPathFor(dst);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err.set("IO", "No se pudo generar el asset cocinado.");
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            err.set("IO", "No se pudo generar el asset cocinado.");
            return false;
        }
        out.close();
    }
    if (!AtomicReplaceFile(tmp, dst, err)) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

// --- OBJ -> GLB mínimo ---
struct ObjMesh {
    std::vector<float> positions;  // x,y,z planos
    std::vector<std::uint32_t> indices;
    float mn[3] = {0, 0, 0};
    float mx[3] = {0, 0, 0};
    bool has_bounds = false;
};

bool ObjParsePosition(const std::string& s, float v[3]) {
    const char* p = s.c_str();
    char* end = nullptr;
    for (int i = 0; i < 3; ++i) {
        while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)) != 0) ++p;
        float f = std::strtof(p, &end);
        if (end == p || std::isfinite(f) == 0) return false;
        v[i] = f;
        p = end;
    }
    return true;
}

bool ObjParseIndex(const std::string& tok, std::size_t pos_count, std::uint32_t& out) {
    if (tok.empty()) return false;
    std::string head = tok;
    std::size_t slash = head.find('/');
    if (slash != std::string::npos) head = head.substr(0, slash);
    if (head.empty()) return false;
    char* end = nullptr;
    long val = std::strtol(head.c_str(), &end, 10);
    if (end == head.c_str() || *end != '\0' || val == 0) return false;
    long resolved = val;
    if (val < 0) resolved = static_cast<long>(pos_count) + val + 1L;
    if (resolved <= 0 || static_cast<std::uint64_t>(resolved) > pos_count ||
        static_cast<std::uint64_t>(resolved - 1L) > std::numeric_limits<std::uint32_t>::max())
        return false;
    out = static_cast<std::uint32_t>(resolved - 1L);
    return true;
}

bool ObjRead(const std::filesystem::path& path, ObjMesh& mesh, Error& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err.set("NOT_FOUND", "No se pudo abrir el OBJ '" + path.string() + "'.");
        return false;
    }
    std::string line;
    line.reserve(256);
    constexpr std::size_t kMaxVerts = 1000000;
    constexpr std::size_t kMaxIdx = 4000000;
    while (std::getline(in, line)) {
        if (line.size() > 4096) {
            err.set("BAD_FORMAT", "El OBJ contiene una línea demasiado larga.");
            return false;
        }
        std::size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0) ++i;
        if (i >= line.size() || line[i] == '#') continue;
        if (line[i] == 'v' && i + 1 < line.size() &&
            std::isspace(static_cast<unsigned char>(line[i + 1])) != 0) {
            float v[3];
            if (!ObjParsePosition(line.substr(i + 1), v)) {
                err.set("BAD_FORMAT", "El OBJ contiene vértices inválidos.");
                return false;
            }
            if (mesh.positions.size() / 3 >= kMaxVerts) {
                err.set("LIMIT", "El OBJ supera el límite de vértices.");
                return false;
            }
            mesh.positions.insert(mesh.positions.end(), {v[0], v[1], v[2]});
            if (!mesh.has_bounds) {
                mesh.mn[0] = mesh.mx[0] = v[0];
                mesh.mn[1] = mesh.mx[1] = v[1];
                mesh.mn[2] = mesh.mx[2] = v[2];
                mesh.has_bounds = true;
            } else {
                for (int k = 0; k < 3; ++k) {
                    if (v[k] < mesh.mn[k]) mesh.mn[k] = v[k];
                    if (v[k] > mesh.mx[k]) mesh.mx[k] = v[k];
                }
            }
        } else if (line[i] == 'f' && i + 1 < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[i + 1])) != 0) {
            std::istringstream iss(line.substr(i + 1));
            std::vector<std::uint32_t> face;
            std::string tok;
            while (iss >> tok) {
                std::uint32_t idx = 0;
                if (face.size() >= 256 ||
                    !ObjParseIndex(tok, mesh.positions.size() / 3, idx)) {
                    err.set("BAD_FORMAT", "El OBJ contiene una cara inválida.");
                    return false;
                }
                face.push_back(idx);
            }
            if (face.size() < 3) {
                err.set("BAD_FORMAT", "El OBJ contiene una cara con menos de tres vértices.");
                return false;
            }
            for (std::size_t k = 1; k + 1 < face.size(); ++k) {
                if (mesh.indices.size() + 3 > kMaxIdx) {
                    err.set("LIMIT", "El OBJ supera el límite de índices.");
                    return false;
                }
                mesh.indices.insert(mesh.indices.end(), {face[0], face[k], face[k + 1]});
            }
        }
    }
    if (in.bad()) {
        err.set("IO", "No se pudo leer el OBJ.");
        return false;
    }
    if (mesh.positions.empty() || mesh.indices.empty()) {
        err.set("BAD_FORMAT", "El OBJ debe contener vértices y caras triangulables.");
        return false;
    }
    return true;
}

void WriteU32(std::ofstream& out, std::uint32_t v) {
    unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8U),
                          static_cast<unsigned char>(v >> 16U),
                          static_cast<unsigned char>(v >> 24U)};
    out.write(reinterpret_cast<const char*>(b), 4);
}

bool ObjWriteGlb(const std::filesystem::path& dst, const ObjMesh& mesh, Error& err) {
    std::size_t vcount = mesh.positions.size() / 3;
    std::size_t icount = mesh.indices.size();
    std::size_t vbytes = vcount * 12;
    std::size_t ibytes = icount * 4;
    std::size_t bin_bytes = (vbytes + ibytes + 3) & ~(std::size_t)3;
    std::ostringstream j;
    j << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Oasis OBJ converter\"},\"buffers\":[{"
         "\"byteLength\":"
      << bin_bytes
      << "}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" << vbytes
      << ",\"target\":34962},{\"buffer\":0,\"byteOffset\":" << vbytes << ",\"byteLength\":" << ibytes
      << ",\"target\":34963}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":"
      << vcount << ",\"type\":\"VEC3\",\"min\":[" << mesh.mn[0] << "," << mesh.mn[1] << ","
      << mesh.mn[2] << "],\"max\":[" << mesh.mx[0] << "," << mesh.mx[1] << "," << mesh.mx[2]
      << "]},{\"bufferView\":1,\"componentType\":5125,\"count\":" << icount
      << ",\"type\":\"SCALAR\"}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},"
         "\"indices\":1,\"mode\":4}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],"
         "\"scene\":0}";
    std::string json = j.str();
    if (json.size() > 1024 * 1024 || bin_bytes > kMaxAssetBytes) {
        err.set("LIMIT", "El OBJ es demasiado grande para GLB 2.0.");
        return false;
    }
    std::size_t json_bytes = (json.size() + 3) & ~(std::size_t)3;
    if (json_bytes + bin_bytes > UINT32_MAX - 28U) {
        err.set("LIMIT", "El OBJ es demasiado grande para GLB 2.0.");
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    std::filesystem::path tmp = TempPathFor(dst);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err.set("IO", "No se pudo crear el GLB convertido.");
            return false;
        }
        out.write("glTF", 4);
        WriteU32(out, 2U);
        WriteU32(out, static_cast<std::uint32_t>(28U + json_bytes + bin_bytes));
        WriteU32(out, static_cast<std::uint32_t>(json_bytes));
        out.write("JSON", 4);
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        for (std::size_t p = json.size(); p < json_bytes; ++p) out.put(' ');
        WriteU32(out, static_cast<std::uint32_t>(bin_bytes));
        out.write("BIN\0", 4);
        out.write(reinterpret_cast<const char*>(mesh.positions.data()),
                  static_cast<std::streamsize>(vbytes));
        out.write(reinterpret_cast<const char*>(mesh.indices.data()),
                  static_cast<std::streamsize>(ibytes));
        for (std::size_t p = vbytes + ibytes; p < bin_bytes; ++p) out.put('\0');
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            err.set("IO", "No se pudo escribir el GLB convertido.");
            return false;
        }
        out.close();
    }
    if (!AtomicReplaceFile(tmp, dst, err)) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace

bool AssetListLoad(const Project& proj, AssetList& out, Error& err) {
    err.clear();
    out.items.clear();
    std::filesystem::path path = RegistryPath(proj);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return true;
    std::uintmax_t sz = std::filesystem::file_size(path, ec);
    if (ec) {
        err.set("IO", "No se pudo leer el manifiesto de assets.");
        return false;
    }
    if (sz > kMaxFileBytes) {
        err.set("LIMIT", "El manifiesto de assets supera el límite.");
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err.set("IO", "No se pudo leer el manifiesto de assets.");
        return false;
    }
    std::string content(static_cast<std::size_t>(sz), '\0');
    if (sz > 0) {
        in.read(content.data(), static_cast<std::streamsize>(sz));
        if (static_cast<std::uintmax_t>(in.gcount()) != sz) {
            err.set("IO", "No se pudo leer el manifiesto de assets.");
            return false;
        }
    }
    const char* end = nullptr;
    cJSON* doc = cJSON_ParseWithLengthOpts(content.c_str(), content.size(), &end, 0);
    CJsonPtr guard(doc);
    if (doc == nullptr) {
        err.set("BAD_FORMAT", "El manifiesto de assets no es JSON válido.");
        return false;
    }
    cJSON* format = cJSON_GetObjectItemCaseSensitive(doc, "format");
    cJSON* items = cJSON_GetObjectItemCaseSensitive(doc, "assets");
    if (cJSON_IsObject(doc) == 0 || cJSON_IsNumber(format) == 0 || format->valuedouble != 1.0 ||
        cJSON_IsArray(items) == 0 || cJSON_GetArraySize(items) > static_cast<int>(kMaxAssets)) {
        err.set("BAD_FORMAT", "El manifiesto de assets no cumple el formato Oasis.");
        return false;
    }
    int n = cJSON_GetArraySize(items);
    for (int i = 0; i < n; ++i) {
        cJSON* it = cJSON_GetArrayItem(items, i);
        cJSON* id = cJSON_GetObjectItemCaseSensitive(it, "id");
        cJSON* src = cJSON_GetObjectItemCaseSensitive(it, "source");
        cJSON* cok = cJSON_GetObjectItemCaseSensitive(it, "cooked");
        cJSON* h = cJSON_GetObjectItemCaseSensitive(it, "hash");
        cJSON* b = cJSON_GetObjectItemCaseSensitive(it, "bytes");
        cJSON* k = cJSON_GetObjectItemCaseSensitive(it, "kind");
        if (cJSON_IsObject(it) == 0 || cJSON_IsString(id) == 0 || !ValidId(id->valuestring) ||
            cJSON_IsString(src) == 0 || cJSON_IsString(cok) == 0 || cJSON_IsString(h) == 0 ||
            std::strlen(h->valuestring) != 16U || HasTraversal(src->valuestring) ||
            HasTraversal(cok->valuestring)) {
            err.set("BAD_FORMAT", "El manifiesto de assets contiene una entrada inválida.");
            return false;
        }
        // kind ausente = manifiesto viejo -> "model".
        std::string kind = "model";
        if (k != nullptr) {
            if (cJSON_IsString(k) == 0 || (std::strcmp(k->valuestring, "model") != 0 &&
                                           std::strcmp(k->valuestring, "sky") != 0)) {
                err.set("BAD_FORMAT", "El manifiesto de assets contiene un kind inválido.");
                return false;
            }
            kind = k->valuestring;
        }
        if (cJSON_IsNumber(b) == 0 || std::isfinite(b->valuedouble) == 0 || b->valuedouble < 0.0 ||
            b->valuedouble != static_cast<double>(static_cast<std::uint64_t>(b->valuedouble))) {
            err.set("BAD_FORMAT", "El manifiesto de assets contiene bytes inválidos.");
            return false;
        }
        for (const auto& prev : out.items) {
            if (prev.id == id->valuestring) {
                err.set("BAD_FORMAT", "El manifiesto contiene un id duplicado.");
                return false;
            }
        }
        Asset a;
        a.id = id->valuestring;
        a.source = src->valuestring;
        a.cooked = cok->valuestring;
        a.hash = h->valuestring;
        a.bytes = static_cast<std::uint64_t>(b->valuedouble);
        a.kind = kind;
        out.items.push_back(std::move(a));
    }
    return true;
}

bool AssetImportGlb(const Project& proj, const std::string& id,
                    const std::filesystem::path& source_path, Error& err) {
    err.clear();
    if (!ValidId(id)) {
        err.set("INVALID_ARG", "Uso: asset import ID archivo.glb");
        return false;
    }
    if (source_path.empty()) {
        err.set("INVALID_ARG", "Uso: asset import ID archivo.glb");
        return false;
    }
    AssetList list;
    if (!AssetListLoad(proj, list, err)) return false;
    std::error_code ec;
    std::filesystem::create_directories(proj.root / "assets" / "source", ec);
    std::filesystem::create_directories(proj.root / "assets" / "cooked", ec);
    Asset* slot = nullptr;
    for (auto& a : list.items) {
        if (a.id == id) {
            slot = &a;
            break;
        }
    }
    if (slot != nullptr && slot->kind != "model") {
        // El id existía con otro kind: normalizar rutas a .glb y huérfano fuera.
        slot->source = "assets/source/" + id + ".glb";
        slot->cooked = "assets/cooked/" + id + ".oasisasset";
        std::error_code rm_ec;
        std::filesystem::remove(proj.root / "assets" / "source" / (id + ".hdr"), rm_ec);
    }
    if (slot == nullptr) {
        if (list.items.size() >= kMaxAssets) {
            err.set("LIMIT", "Se alcanzó el límite de assets.");
            return false;
        }
        Asset a;
        a.id = id;
        a.source = "assets/source/" + id + ".glb";
        a.cooked = "assets/cooked/" + id + ".oasisasset";
        list.items.push_back(std::move(a));
        slot = &list.items.back();
    }
    std::filesystem::path rel_src(slot->source, std::filesystem::path::format::generic_format);
    std::filesystem::path dst = proj.root / rel_src;
    std::uint64_t bytes = 0;
    std::string hash;
    if (!CopyAndValidateGlb(source_path, dst, bytes, hash, err)) return false;
    slot->bytes = bytes;
    slot->hash = hash;
    slot->kind = "model";
    if (!WriteCooked(proj, *slot, "model", err)) return false;
    if (!WriteRegistry(proj, list, err)) return false;
    return true;
}

// Valida Radiance HDR (.hdr RGBE): magia + tope 32MB, copia con hash FNV-1a 64.
bool CopyAndValidateHdr(const std::filesystem::path& src, const std::filesystem::path& dst,
                        std::uint64_t& out_bytes, std::string& out_hash, Error& err) {
    std::error_code ec;
    std::uintmax_t fsize = std::filesystem::file_size(src, ec);
    if (ec) {
        err.set("NOT_FOUND", "No se pudo abrir el HDRI '" + src.string() + "'.");
        return false;
    }
    constexpr std::uint64_t kMaxHdrBytes = 32ULL * 1024ULL * 1024ULL;
    if (fsize < 32 || fsize > kMaxHdrBytes) {
        err.set("BAD_FORMAT", "El HDRI debe ser un Radiance .hdr válido de menos de 32MB.");
        return false;
    }
    std::ifstream in(src, std::ios::binary);
    if (!in) {
        err.set("NOT_FOUND", "No se pudo abrir el HDRI '" + src.string() + "'.");
        return false;
    }
    char magic[11] = {};
    in.read(magic, 10);
    if (static_cast<std::size_t>(in.gcount()) != 10 ||
        (std::memcmp(magic, "#?RADIANCE", 10) != 0 && std::memcmp(magic, "#?RGBE", 6) != 0)) {
        err.set("BAD_FORMAT", "El HDRI debe ser un Radiance .hdr válido (magia #?RADIANCE).");
        return false;
    }
    in.close();
    std::uint64_t h = 1469598103934665603ULL;
    in.open(src, std::ios::binary);
    if (!in) {
        err.set("IO", "No se pudo leer el HDRI.");
        return false;
    }
    std::error_code ec2;
    std::filesystem::create_directories(dst.parent_path(), ec2);
    std::filesystem::path tmp = TempPathFor(dst);
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        err.set("IO", "No se pudo preparar el HDRI importado.");
        return false;
    }
    std::array<char, 8192> buf{};
    std::uint64_t total = 0;
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize got = in.gcount();
        if (got < 0) got = 0;
        for (std::streamsize i = 0; i < got; ++i) {
            h ^= static_cast<unsigned char>(buf[static_cast<std::size_t>(i)]);
            h *= 1099511628211ULL;
        }
        if (got > 0) {
            out.write(buf.data(), got);
            if (!out) {
                in.close();
                out.close();
                std::filesystem::remove(tmp, ec2);
                err.set("IO", "No se pudo copiar el HDRI importado.");
                return false;
            }
            total += static_cast<std::uint64_t>(got);
        }
    }
    if (in.bad()) {
        in.close();
        out.close();
        std::filesystem::remove(tmp, ec2);
        err.set("IO", "No se pudo copiar el HDRI importado.");
        return false;
    }
    out.flush();
    out.close();
    in.close();
    if (!out || total != fsize) {
        std::filesystem::remove(tmp, ec2);
        err.set("IO", "No se pudo copiar el HDRI importado.");
        return false;
    }
    if (!AtomicReplaceFile(tmp, dst, err)) {
        std::filesystem::remove(tmp, ec2);
        return false;
    }
    out_bytes = fsize;
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    out_hash.assign(hex, 16);
    return true;
}

bool AssetImportSky(const Project& proj, const std::string& id,
                    const std::filesystem::path& source_path, Error& err) {
    err.clear();
    if (!ValidId(id)) {
        err.set("INVALID_ARG", "Uso: asset import-sky ID archivo.hdr");
        return false;
    }
    if (source_path.empty()) {
        err.set("INVALID_ARG", "Uso: asset import-sky ID archivo.hdr");
        return false;
    }
    AssetList list;
    if (!AssetListLoad(proj, list, err)) return false;
    std::error_code ec;
    std::filesystem::create_directories(proj.root / "assets" / "source", ec);
    std::filesystem::create_directories(proj.root / "assets" / "cooked", ec);
    Asset* slot = nullptr;
    for (auto& a : list.items) {
        if (a.id == id) {
            slot = &a;
            break;
        }
    }
    if (slot != nullptr && slot->kind != "sky") {
        // El id existía con otro kind: normalizar rutas a .hdr y huérfano fuera.
        slot->source = "assets/source/" + id + ".hdr";
        slot->cooked = "assets/cooked/" + id + ".oasisasset";
        std::error_code rm_ec;
        std::filesystem::remove(proj.root / "assets" / "source" / (id + ".glb"), rm_ec);
    }
    if (slot == nullptr) {
        if (list.items.size() >= kMaxAssets) {
            err.set("LIMIT", "Se alcanzó el límite de assets.");
            return false;
        }
        Asset a;
        a.id = id;
        a.source = "assets/source/" + id + ".hdr";
        a.cooked = "assets/cooked/" + id + ".oasisasset";
        a.kind = "sky";
        list.items.push_back(std::move(a));
        slot = &list.items.back();
    }
    std::filesystem::path rel_src(slot->source, std::filesystem::path::format::generic_format);
    std::filesystem::path dst = proj.root / rel_src;
    std::uint64_t bytes = 0;
    std::string hash;
    if (!CopyAndValidateHdr(source_path, dst, bytes, hash, err)) return false;
    slot->bytes = bytes;
    slot->hash = hash;
    slot->kind = "sky";
    if (!WriteCooked(proj, *slot, "sky", err)) return false;
    if (!WriteRegistry(proj, list, err)) return false;
    return true;
}

bool AssetConvertObj(const Project& proj, const std::string& id,
                     const std::filesystem::path& source_path, Error& err) {
    err.clear();
    if (!ValidId(id)) {
        err.set("INVALID_ARG", "Uso: asset convert-obj ID archivo.obj");
        return false;
    }
    if (source_path.empty()) {
        err.set("INVALID_ARG", "Uso: asset convert-obj ID archivo.obj");
        return false;
    }
    ObjMesh mesh;
    if (!ObjRead(source_path, mesh, err)) return false;
    std::filesystem::path dst = proj.root / "assets" / "source" / (id + ".glb");
    if (!ObjWriteGlb(dst, mesh, err)) return false;
    return AssetImportGlb(proj, id, dst, err);
}

std::string AssetListToJson(const AssetList& assets) {
    std::ostringstream oss;
    oss << "{\"schema_version\":" << kSchemaVersion << ",\"ok\":true,\"result\":{\"assets\":[";
    for (std::size_t i = 0; i < assets.items.size(); ++i) {
        const Asset& a = assets.items[i];
        if (i != 0) oss << ",";
        oss << "{\"id\":\"" << JsonEscape(a.id) << "\",\"type\":\"" << JsonEscape(a.kind)
            << "\",\"source\":\"" << JsonEscape(a.source) << "\",\"cooked\":\"" << JsonEscape(a.cooked)
            << "\",\"hash\":\"" << JsonEscape(a.hash) << "\",\"bytes\":" << a.bytes << "}";
    }
    oss << "]}}";
    return oss.str();
}

}  // namespace oasis
