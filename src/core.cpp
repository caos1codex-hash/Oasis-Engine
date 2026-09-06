#include "core.hpp"

#include "cJSON.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
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

unsigned long long ProcessId() {
#ifdef _WIN32
    return static_cast<unsigned long long>(::GetCurrentProcessId());
#else
    return static_cast<unsigned long long>(::getpid());
#endif
}

std::atomic<unsigned long long> g_tmp_counter{0};

bool ValidNameImpl(const std::string& s) {
    if (s.empty() || s.size() >= kMaxName) return false;
    for (unsigned char ch : s) {
        if (!(std::isalnum(ch) != 0 || ch == '_' || ch == '-')) return false;
    }
    return true;
}

std::filesystem::path SceneFilePath(const Project& proj, const std::string& name) {
    return proj.root / "scenes" / (name + ".scene.json");
}

std::filesystem::path ProjectFilePath(const Project& proj) { return proj.root / "oasis.project"; }

bool ReadFileBytes(const std::filesystem::path& path, std::string& out, Error& err) {
    std::error_code ec;
    std::uintmax_t sz = std::filesystem::file_size(path, ec);
    if (ec) {
        err.set("IO", "No se pudo abrir '" + path.string() + "'.");
        return false;
    }
    if (sz > kMaxFileBytes) {
        err.set("LIMIT", "El archivo '" + path.string() + "' supera el límite de 64MB.");
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err.set("IO", "No se pudo abrir '" + path.string() + "'.");
        return false;
    }
    out.assign(static_cast<std::size_t>(sz), '\0');
    if (sz > 0) {
        in.read(out.data(), static_cast<std::streamsize>(sz));
        if (static_cast<std::uintmax_t>(in.gcount()) != sz) {
            err.set("IO", "No se pudo leer '" + path.string() + "'.");
            return false;
        }
    }
    return true;
}

bool AtomicReplace(const std::filesystem::path& tmp, const std::filesystem::path& dst, Error& err) {
#ifdef _WIN32
    if (::MoveFileExA(tmp.string().c_str(), dst.string().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
        return true;
    err.set("IO", "No se pudo reemplazar '" + dst.string() + "' de forma atómica.");
    return false;
#else
    std::error_code ec;
    std::filesystem::rename(tmp, dst, ec);
    if (!ec) return true;
    err.set("IO", "No se pudo reemplazar '" + dst.string() + "' de forma atómica.");
    return false;
#endif
}

bool AtomicWriteText(const std::filesystem::path& dst, const std::string& text, Error& err) {
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    // parent_path puede ser vacío si dst es relativo sin dir; create_directories("") falla, ignorar
    unsigned long long n = g_tmp_counter.fetch_add(1, std::memory_order_relaxed);
    std::filesystem::path tmp =
        dst.string() + ".tmp." + std::to_string(ProcessId()) + "." + std::to_string(n);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err.set("IO", "No se pudo guardar '" + dst.string() + "'.");
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);
            err.set("IO", "No se pudo guardar '" + dst.string() + "'.");
            return false;
        }
        out.close();
        if (!out) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);
            err.set("IO", "No se pudo cerrar '" + dst.string() + "'.");
            return false;
        }
    }
    if (!AtomicReplace(tmp, dst, err)) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);
        return false;
    }
    return true;
}

struct CJsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

cJSON* JsonMember(cJSON* obj, const char* name, Error& err) {
    if (obj == nullptr || cJSON_IsObject(obj) == 0) {
        err.set("BAD_FORMAT", "Se esperaba un objeto JSON.");
        return nullptr;
    }
    cJSON* found = nullptr;
    for (cJSON* c = obj->child; c != nullptr; c = c->next) {
        if (c->string != nullptr && std::strcmp(c->string, name) == 0) {
            if (found != nullptr) {
                err.set("BAD_FORMAT", std::string("El campo '") + name + "' está duplicado.");
                return nullptr;
            }
            found = c;
        }
    }
    if (found == nullptr) {
        err.set("BAD_FORMAT", std::string("Falta el campo obligatorio '") + name + "'.");
        return nullptr;
    }
    return found;
}

bool JsonCopyString(cJSON* item, std::string& out, const char* field, Error& err) {
    if (item == nullptr || cJSON_IsString(item) == 0 || item->valuestring == nullptr) {
        err.set("BAD_FORMAT", std::string("El campo '") + field + "' debe ser una cadena.");
        return false;
    }
    std::size_t len = std::strlen(item->valuestring);
    if (len >= kMaxName && (std::strcmp(field, "name") == 0 || std::strcmp(field, "id") == 0 ||
                            std::strcmp(field, "active_scene") == 0)) {
        err.set("BAD_FORMAT", std::string("El campo '") + field + "' es demasiado largo.");
        return false;
    }
    if (len >= 1024 && (std::strcmp(field, "Mesh.asset_id") == 0)) {
        err.set("BAD_FORMAT", std::string("El campo '") + field + "' es demasiado largo.");
        return false;
    }
    out.assign(item->valuestring, len);
    return true;
}

bool JsonVec3(cJSON* item, Vec3& out, const char* field, Error& err) {
    if (item == nullptr || cJSON_IsArray(item) == 0 || cJSON_GetArraySize(item) != 3) {
        err.set("BAD_FORMAT", std::string("El campo '") + field + "' debe ser un array de tres números.");
        return false;
    }
    float vals[3] = {};
    for (int i = 0; i < 3; ++i) {
        cJSON* n = cJSON_GetArrayItem(item, i);
        if (n == nullptr || cJSON_IsNumber(n) == 0 || std::isfinite(n->valuedouble) == 0) {
            err.set("BAD_FORMAT",
                    std::string("El campo '") + field + "' debe contener números finitos.");
            return false;
        }
        if (n->valuedouble < -3.4028235e38 || n->valuedouble > 3.4028235e38) {
            err.set("BAD_FORMAT",
                    std::string("El campo '") + field + "' está fuera de rango.");
            return false;
        }
        vals[i] = static_cast<float>(n->valuedouble);
    }
    out.x = vals[0];
    out.y = vals[1];
    out.z = vals[2];
    return true;
}

CJsonPtr ParseDocument(const std::string& content, const std::string& path_for_msg, Error& err) {
    const char* end = nullptr;
    cJSON* doc = cJSON_ParseWithLengthOpts(content.c_str(), content.size(), &end, 0);
    const char* p = end;
    if (p != nullptr) {
        while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)) != 0) ++p;
    }
    if (doc == nullptr || end == nullptr || *p != '\0') {
        std::size_t pos = (end == nullptr) ? 0 : static_cast<std::size_t>(end - content.c_str());
        err.set("BAD_FORMAT",
                "JSON inválido en '" + path_for_msg + "' cerca de la posición " + std::to_string(pos) +
                    ".");
        cJSON_Delete(doc);
        return CJsonPtr(nullptr);
    }
    return CJsonPtr(doc);
}

std::string FmtFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
    return std::string(buf);
}

void DefaultEntity(Entity& e, const std::string& id) {
    e.id = id;
    e.name = id;
    e.has_transform = false;
    e.has_mesh = false;
    e.has_camera = false;
    e.has_light = false;
    e.transform = Transform{};
    e.mesh = Mesh{};
    e.camera = Camera{};
    e.light = Light{};
}

bool WriteProjectFile(const Project& proj, Error& err) {
    std::ostringstream oss;
    oss << "{\n  \"format\": 1,\n  \"engine\": \"Oasis Engine\",\n  \"version\": \"" << kVersion
        << "\",\n  \"name\": \"" << JsonEscape(proj.name) << "\",\n";
    if (proj.active_scene.empty())
        oss << "  \"active_scene\": null\n}\n";
    else
        oss << "  \"active_scene\": \"" << JsonEscape(proj.active_scene) << "\"\n}\n";
    return AtomicWriteText(ProjectFilePath(proj), oss.str(), err);
}

}  // namespace

bool ValidId(const std::string& id) { return ValidNameImpl(id); }
bool ValidSceneName(const std::string& name) { return ValidNameImpl(name); }

bool ProjectCreate(const std::filesystem::path& root, const std::string& name, Error& err) {
    err.clear();
    if (!ValidId(name)) {
        err.set("INVALID_ARG", "Nombre de proyecto inválido. Usa letras, números, '_' o '-'.");
        return false;
    }
    if (root.empty()) {
        err.set("INVALID_ARG", "Ruta de proyecto inválida.");
        return false;
    }
    Project proj;
    proj.root = root;
    proj.name = name;
    std::error_code ec;
    std::filesystem::create_directories(proj.root, ec);
    if (ec) {
        err.set("IO", "No se pudo crear el directorio '" + proj.root.string() + "'.");
        return false;
    }
    std::filesystem::create_directories(proj.root / "scenes", ec);
    if (ec) {
        err.set("IO", "No se pudo crear el directorio de escenas.");
        return false;
    }
    std::filesystem::create_directories(proj.root / "assets", ec);
    if (ec) {
        err.set("IO", "No se pudo crear el directorio de assets.");
        return false;
    }
    return WriteProjectFile(proj, err);
}

bool ProjectLoad(const std::filesystem::path& root, Project& out, Error& err) {
    err.clear();
    if (root.empty()) {
        err.set("INVALID_ARG", "Ruta de proyecto inválida.");
        return false;
    }
    Project loaded;
    loaded.root = root;
    std::filesystem::path path = loaded.root / "oasis.project";
    std::string content;
    if (!ReadFileBytes(path, content, err)) return false;
    CJsonPtr doc = ParseDocument(content, path.string(), err);
    if (doc == nullptr) return false;
    cJSON* obj = doc.get();
    Error tmp;
    cJSON* format = JsonMember(obj, "format", tmp);
    cJSON* engine = JsonMember(obj, "engine", tmp);
    cJSON* version = JsonMember(obj, "version", tmp);
    cJSON* name = JsonMember(obj, "name", tmp);
    cJSON* active = JsonMember(obj, "active_scene", tmp);
    if (format == nullptr || engine == nullptr || version == nullptr || name == nullptr ||
        active == nullptr) {
        err = tmp;
        if (err.empty()) err.set("BAD_FORMAT", "El archivo de proyecto no cumple el formato Oasis.");
        return false;
    }
    if (cJSON_IsNumber(format) == 0 || format->valuedouble != 1.0 ||
        cJSON_IsString(engine) == 0 || std::strcmp(engine->valuestring, kEngineName) != 0 ||
        cJSON_IsString(version) == 0) {
        err.set("BAD_FORMAT", "El archivo de proyecto no cumple el formato Oasis.");
        return false;
    }
    std::string pname;
    if (!JsonCopyString(name, pname, "name", err)) return false;
    if (!ValidId(pname)) {
        err.set("BAD_FORMAT", "Nombre de proyecto inválido en disco.");
        return false;
    }
    loaded.name = pname;
    if (cJSON_IsNull(active) != 0) {
        loaded.active_scene.clear();
    } else {
        std::string aname;
        if (!JsonCopyString(active, aname, "active_scene", err)) return false;
        if (!ValidSceneName(aname)) {
            err.set("BAD_FORMAT", "Escena activa inválida en disco.");
            return false;
        }
        loaded.active_scene = aname;
    }
    out = std::move(loaded);
    return true;
}

bool SceneCreate(Project& proj, const std::string& name, Error& err) {
    err.clear();
    if (!ValidSceneName(name)) {
        err.set("INVALID_ARG", "Nombre de escena inválido.");
        return false;
    }
    std::string prev = proj.active_scene;
    proj.active_scene = name;
    Scene scene;
    scene.name = name;
    if (!SceneSaveActive(proj, scene, err)) {
        proj.active_scene = prev;
        return false;
    }
    if (!WriteProjectFile(proj, err)) {
        proj.active_scene = prev;
        return false;
    }
    return true;
}

bool SceneOpen(Project& proj, const std::string& name, Error& err) {
    err.clear();
    if (!ValidSceneName(name)) {
        err.set("INVALID_ARG", "Nombre de escena inválido.");
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(SceneFilePath(proj, name), ec)) {
        err.set("NOT_FOUND", "No existe la escena '" + name + "'.");
        return false;
    }
    std::string prev = proj.active_scene;
    proj.active_scene = name;
    if (!WriteProjectFile(proj, err)) {
        proj.active_scene = prev;
        return false;
    }
    return true;
}

bool SceneList(const Project& proj, std::vector<std::string>& out, Error& err) {
    err.clear();
    out.clear();
    std::error_code ec;
    std::filesystem::path dir = proj.root / "scenes";
    if (!std::filesystem::exists(dir, ec)) return true;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        err.set("IO", "No se pudieron listar las escenas.");
        return false;
    }
    const std::string suffix = ".scene.json";
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) continue;
        std::string fname = entry.path().filename().string();
        if (fname.size() <= suffix.size()) continue;
        if (fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        std::string sname = fname.substr(0, fname.size() - suffix.size());
        if (!ValidSceneName(sname)) continue;
        if (out.size() >= kMaxScenes) {
            err.set("LIMIT", "Se superó el límite de escenas.");
            return false;
        }
        out.push_back(sname);
    }
    std::sort(out.begin(), out.end());
    return true;
}

bool SceneDelete(Project& proj, const std::string& name, Error& err) {
    err.clear();
    if (!ValidSceneName(name)) {
        err.set("INVALID_ARG", "Nombre de escena inválido.");
        return false;
    }
    if (proj.active_scene == name) {
        err.set("INVALID_ARG",
                "No se puede eliminar la escena activa '" + name + "'. Abre otra escena primero.");
        return false;
    }
    std::error_code ec;
    bool removed = std::filesystem::remove(SceneFilePath(proj, name), ec);
    if (ec || !removed) {
        err.set("NOT_FOUND", "No existe la escena '" + name + "' o no se pudo eliminar.");
        return false;
    }
    return true;
}

bool SceneLoadActive(const Project& proj, Scene& out, Error& err) {
    err.clear();
    if (proj.active_scene.empty()) {
        err.set("INVALID_ARG", "No hay una escena activa.");
        return false;
    }
    std::filesystem::path path = SceneFilePath(proj, proj.active_scene);
    std::string content;
    if (!ReadFileBytes(path, content, err)) {
        if (err.code == "IO") err.set("NOT_FOUND", "No se pudo abrir '" + path.string() + "'.");
        return false;
    }
    CJsonPtr doc = ParseDocument(content, path.string(), err);
    if (doc == nullptr) return false;
    cJSON* obj = doc.get();
    Error tmp;
    cJSON* format = JsonMember(obj, "format", tmp);
    cJSON* sname = JsonMember(obj, "name", tmp);
    cJSON* entities = JsonMember(obj, "entities", tmp);
    if (format == nullptr || sname == nullptr || entities == nullptr) {
        err = tmp;
        return false;
    }
    if (cJSON_IsNumber(format) == 0 || format->valuedouble != 1.0 || cJSON_IsArray(entities) == 0 ||
        cJSON_GetArraySize(entities) > static_cast<int>(kMaxEntities)) {
        err.set("BAD_FORMAT", "El archivo de escena no cumple el formato Oasis.");
        return false;
    }
    Scene loaded;
    std::string lname;
    if (!JsonCopyString(sname, lname, "name", err)) return false;
    if (!ValidSceneName(lname)) {
        err.set("BAD_FORMAT", "Nombre de escena inválido en disco.");
        return false;
    }
    loaded.name = lname;
    // "sky" opcional (ausente = escena vieja sin cielo): null o {"asset_id": ID}.
    cJSON* sky = cJSON_GetObjectItemCaseSensitive(obj, "sky");
    if (sky != nullptr && cJSON_IsNull(sky) == 0) {
        if (cJSON_IsObject(sky) == 0) {
            err.set("BAD_FORMAT", "El campo 'sky' debe ser un objeto o null.");
            return false;
        }
        cJSON* said = cJSON_GetObjectItemCaseSensitive(sky, "asset_id");
        std::string sky_id;
        if (!JsonCopyString(said, sky_id, "sky.asset_id", err)) return false;
        if (!ValidId(sky_id)) {
            err.set("BAD_FORMAT", "Identificador de sky inválido en disco.");
            return false;
        }
        loaded.sky_asset = sky_id;
    }
    int count = cJSON_GetArraySize(entities);
    for (int i = 0; i < count; ++i) {
        cJSON* src = cJSON_GetArrayItem(entities, i);
        if (src == nullptr || cJSON_IsObject(src) == 0) {
            err.set("BAD_FORMAT", "Entidad inválida en escena.");
            return false;
        }
        Error e2;
        cJSON* id = JsonMember(src, "id", e2);
        cJSON* comps = JsonMember(src, "components", e2);
        if (id == nullptr || comps == nullptr) {
            err = e2;
            return false;
        }
        Entity e;
        DefaultEntity(e, "");
        std::string eid;
        if (!JsonCopyString(id, eid, "id", err)) return false;
        if (!ValidId(eid)) {
            err.set("BAD_FORMAT", "Identificador de entidad inválido en disco.");
            return false;
        }
        for (const auto& prev : loaded.entities) {
            if (prev.id == eid) {
                err.set("BAD_FORMAT", "La entidad '" + eid + "' está duplicada.");
                return false;
            }
        }
        e.id = eid;
        cJSON* ename = cJSON_GetObjectItemCaseSensitive(src, "name");
        if (ename == nullptr) {
            e.name = eid;
        } else {
            if (!JsonCopyString(ename, e.name, "name", err)) return false;
        }
        if (cJSON_IsObject(comps) == 0) {
            err.set("BAD_FORMAT", "El campo 'components' debe ser un objeto.");
            return false;
        }
        cJSON* t = cJSON_GetObjectItemCaseSensitive(comps, "Transform");
        cJSON* m = cJSON_GetObjectItemCaseSensitive(comps, "Mesh");
        cJSON* c = cJSON_GetObjectItemCaseSensitive(comps, "Camera");
        cJSON* l = cJSON_GetObjectItemCaseSensitive(comps, "Light");
        if (t != nullptr) {
            Error t2;
            cJSON* pos = JsonMember(t, "position", t2);
            cJSON* rot = JsonMember(t, "rotation", t2);
            cJSON* scl = JsonMember(t, "scale", t2);
            if (cJSON_IsObject(t) == 0 || pos == nullptr || rot == nullptr || scl == nullptr ||
                !JsonVec3(pos, e.transform.position, "Transform.position", err) ||
                !JsonVec3(rot, e.transform.rotation, "Transform.rotation", err) ||
                !JsonVec3(scl, e.transform.scale, "Transform.scale", err))
                return false;
            e.has_transform = true;
        }
        if (m != nullptr) {
            if (cJSON_IsObject(m) == 0) {
                err.set("BAD_FORMAT", "Mesh debe ser un objeto.");
                return false;
            }
            Error m2;
            cJSON* prim = JsonMember(m, "primitive", m2);
            if (prim == nullptr) {
                err = m2;
                return false;
            }
            if (cJSON_IsString(prim) == 0 || (std::strcmp(prim->valuestring, "cube") != 0 &&
                                              std::strcmp(prim->valuestring, "asset") != 0)) {
                err.set("BAD_FORMAT", "Mesh requiere primitive 'cube' o 'asset'.");
                return false;
            }
            e.mesh.primitive = prim->valuestring;
            cJSON* col = cJSON_GetObjectItemCaseSensitive(m, "color");
            if (col != nullptr && !JsonVec3(col, e.mesh.color, "Mesh.color", err)) return false;
            if (e.mesh.primitive == "asset") {
                cJSON* aid = cJSON_GetObjectItemCaseSensitive(m, "asset_id");
                if (aid == nullptr) {
                    err.set("BAD_FORMAT", "Mesh 'asset' requiere asset_id.");
                    return false;
                }
                std::string aids;
                if (!JsonCopyString(aid, aids, "Mesh.asset_id", err)) return false;
                if (!ValidId(aids)) {
                    err.set("BAD_FORMAT", "Identificador de asset inválido en disco.");
                    return false;
                }
                e.mesh.asset_id = aids;
            }
            e.has_mesh = true;
        }
        if (c != nullptr) {
            if (cJSON_IsObject(c) == 0) {
                err.set("BAD_FORMAT", "Camera debe ser un objeto.");
                return false;
            }
            cJSON* fov = cJSON_GetObjectItemCaseSensitive(c, "fov_degrees");
            if (fov != nullptr) {
                if (cJSON_IsNumber(fov) == 0 || std::isfinite(fov->valuedouble) == 0 ||
                    fov->valuedouble < 1.0 || fov->valuedouble > 179.0) {
                    err.set("BAD_FORMAT", "Camera.fov_degrees debe estar entre 1 y 179.");
                    return false;
                }
                e.camera.fov_degrees = static_cast<float>(fov->valuedouble);
            }
            e.has_camera = true;
        }
        if (l != nullptr) {
            if (cJSON_IsObject(l) == 0) {
                err.set("BAD_FORMAT", "Light debe ser un objeto.");
                return false;
            }
            cJSON* col = cJSON_GetObjectItemCaseSensitive(l, "color");
            cJSON* inten = cJSON_GetObjectItemCaseSensitive(l, "intensity");
            if (col != nullptr && !JsonVec3(col, e.light.color, "Light.color", err)) return false;
            if (inten != nullptr) {
                if (cJSON_IsNumber(inten) == 0 || std::isfinite(inten->valuedouble) == 0 ||
                    inten->valuedouble < 0.0) {
                    err.set("BAD_FORMAT",
                            "Light.intensity debe ser un número finito no negativo.");
                    return false;
                }
                e.light.intensity = static_cast<float>(inten->valuedouble);
            }
            e.has_light = true;
        }
        loaded.entities.push_back(std::move(e));
    }
    out = std::move(loaded);
    return true;
}

bool SceneSaveActive(const Project& proj, const Scene& scene, Error& err) {
    err.clear();
    if (proj.active_scene.empty()) {
        err.set("INVALID_ARG", "No hay una escena activa.");
        return false;
    }
    if (!ValidSceneName(scene.name)) {
        err.set("INVALID_ARG", "Nombre de escena inválido.");
        return false;
    }
    std::ostringstream oss;
    oss << "{\n  \"format\": 1,\n  \"name\": \"" << JsonEscape(scene.name) << "\",\n";
    if (scene.sky_asset.empty())
        oss << "  \"sky\": null,\n";
    else
        oss << "  \"sky\": {\"asset_id\": \"" << JsonEscape(scene.sky_asset) << "\"},\n";
    oss << "  \"entities\": [\n";
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        const Entity& e = scene.entities[i];
        oss << "    {\n      \"id\": \"" << JsonEscape(e.id) << "\",\n      \"name\": \""
            << JsonEscape(e.name) << "\",\n      \"components\": {";
        bool first = true;
        if (e.has_transform) {
            oss << "\n        \"Transform\": {\"position\": [" << FmtFloat(e.transform.position.x)
                << ", " << FmtFloat(e.transform.position.y) << ", " << FmtFloat(e.transform.position.z)
                << "], \"rotation\": [" << FmtFloat(e.transform.rotation.x) << ", "
                << FmtFloat(e.transform.rotation.y) << ", " << FmtFloat(e.transform.rotation.z)
                << "], \"scale\": [" << FmtFloat(e.transform.scale.x) << ", "
                << FmtFloat(e.transform.scale.y) << ", " << FmtFloat(e.transform.scale.z) << "]}";
            first = false;
        }
        if (e.has_mesh) {
            if (!first) oss << ",";
            if (e.mesh.primitive == "asset")
                oss << "\n        \"Mesh\": {\"primitive\": \"asset\", \"asset_id\": \""
                    << JsonEscape(e.mesh.asset_id)
                    << "\", \"color\": [" << FmtFloat(e.mesh.color.x) << ", " << FmtFloat(e.mesh.color.y)
                    << ", " << FmtFloat(e.mesh.color.z) << "]}";
            else
                oss << "\n        \"Mesh\": {\"primitive\": \"cube\", \"color\": ["
                    << FmtFloat(e.mesh.color.x) << ", " << FmtFloat(e.mesh.color.y) << ", "
                    << FmtFloat(e.mesh.color.z) << "]}";
            first = false;
        }
        if (e.has_camera) {
            if (!first) oss << ",";
            oss << "\n        \"Camera\": {\"fov_degrees\": " << FmtFloat(e.camera.fov_degrees) << "}";
            first = false;
        }
        if (e.has_light) {
            if (!first) oss << ",";
            oss << "\n        \"Light\": {\"color\": [" << FmtFloat(e.light.color.x) << ", "
                << FmtFloat(e.light.color.y) << ", " << FmtFloat(e.light.color.z)
                << "], \"intensity\": " << FmtFloat(e.light.intensity) << "}";
        }
        oss << "\n      }\n    }" << (i + 1 == scene.entities.size() ? "" : ",") << "\n";
    }
    oss << "  ]\n}\n";
    return AtomicWriteText(SceneFilePath(proj, proj.active_scene), oss.str(), err);
}

bool SceneAddEntity(Scene& scene, const std::string& id, Error& err) {
    err.clear();
    if (!ValidId(id)) {
        err.set("INVALID_ARG", "Identificador de entidad inválido.");
        return false;
    }
    if (SceneGetEntity(scene, id) != nullptr) {
        err.set("ALREADY_EXISTS", "La entidad '" + id + "' ya existe.");
        return false;
    }
    if (scene.entities.size() >= kMaxEntities) {
        err.set("LIMIT", "La escena alcanzó el límite de entidades.");
        return false;
    }
    Entity e;
    DefaultEntity(e, id);
    scene.entities.push_back(std::move(e));
    return true;
}

bool EntityAddComponent(Scene& scene, const std::string& id, const std::string& component,
                        Error& err) {
    err.clear();
    Entity* e = SceneGetEntityMut(scene, id);
    if (e == nullptr) {
        err.set("NOT_FOUND", "La entidad '" + id + "' no existe.");
        return false;
    }
    if (component == "Transform") {
        if (e->has_transform) {
            err.set("ALREADY_EXISTS", "La entidad '" + id + "' ya tiene Transform.");
            return false;
        }
        e->has_transform = true;
        return true;
    }
    if (component == "Mesh") {
        if (e->has_mesh) {
            err.set("ALREADY_EXISTS", "La entidad '" + id + "' ya tiene Mesh.");
            return false;
        }
        e->has_mesh = true;
        return true;
    }
    if (component == "Camera") {
        if (e->has_camera) {
            err.set("ALREADY_EXISTS", "La entidad '" + id + "' ya tiene Camera.");
            return false;
        }
        e->has_camera = true;
        return true;
    }
    if (component == "Light") {
        if (e->has_light) {
            err.set("ALREADY_EXISTS", "La entidad '" + id + "' ya tiene Light.");
            return false;
        }
        e->has_light = true;
        return true;
    }
    err.set("INVALID_ARG", "Componente '" + component + "' no disponible en V0.2.");
    return false;
}

bool EntityDelete(Scene& scene, const std::string& id, Error& err) {
    err.clear();
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        if (scene.entities[i].id == id) {
            scene.entities.erase(scene.entities.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    err.set("NOT_FOUND", "La entidad '" + id + "' no existe.");
    return false;
}

const Entity* SceneGetEntity(const Scene& scene, const std::string& id) {
    for (const auto& e : scene.entities)
        if (e.id == id) return &e;
    return nullptr;
}

Entity* SceneGetEntityMut(Scene& scene, const std::string& id) {
    for (auto& e : scene.entities)
        if (e.id == id) return &e;
    return nullptr;
}

bool EntitySetTransform(Scene& scene, const std::string& id, const std::string& property,
                        const Vec3& v, Error& err) {
    err.clear();
    Entity* e = SceneGetEntityMut(scene, id);
    if (e == nullptr) {
        err.set("NOT_FOUND", "La entidad '" + id + "' no existe.");
        return false;
    }
    if (!e->has_transform) {
        err.set("INVALID_ARG", "La entidad '" + id + "' no tiene Transform.");
        return false;
    }
    if (std::isfinite(v.x) == 0 || std::isfinite(v.y) == 0 || std::isfinite(v.z) == 0) {
        err.set("INVALID_ARG", "Los valores de Transform deben ser números finitos.");
        return false;
    }
    if (property == "position")
        e->transform.position = v;
    else if (property == "rotation")
        e->transform.rotation = v;
    else if (property == "scale")
        e->transform.scale = v;
    else {
        err.set("INVALID_ARG", "Propiedad Transform '" + property + "' no disponible.");
        return false;
    }
    return true;
}

bool EntitySetMeshAsset(Scene& scene, const std::string& id, const std::string& asset_id,
                        Error& err) {
    err.clear();
    Entity* e = SceneGetEntityMut(scene, id);
    if (e == nullptr) {
        err.set("NOT_FOUND", "La entidad '" + id + "' no existe.");
        return false;
    }
    if (!e->has_mesh) {
        err.set("INVALID_ARG", "La entidad '" + id + "' no tiene Mesh.");
        return false;
    }
    if (!ValidId(asset_id)) {
        err.set("INVALID_ARG", "Identificador de asset inválido.");
        return false;
    }
    e->mesh.primitive = "asset";
    e->mesh.asset_id = asset_id;
    return true;
}

bool EntitySetMeshColor(Scene& scene, const std::string& id, const Vec3& c, Error& err) {
    err.clear();
    Entity* e = SceneGetEntityMut(scene, id);
    if (e == nullptr) {
        err.set("NOT_FOUND", "La entidad '" + id + "' no existe.");
        return false;
    }
    if (!e->has_mesh) {
        err.set("INVALID_ARG", "La entidad '" + id + "' no tiene Mesh.");
        return false;
    }
    const float ch[3] = {c.x, c.y, c.z};
    for (int i = 0; i < 3; ++i) {
        if (std::isfinite(ch[i]) == 0 || ch[i] < 0.0f || ch[i] > 1.0f) {
            err.set("INVALID_ARG", "El color del Mesh requiere componentes RGB en 0..1.");
            return false;
        }
    }
    e->mesh.color = c;
    return true;
}

bool EntitySetLight(Scene& scene, const std::string& id, const Vec3& c, float intensity,
                    Error& err) {
    err.clear();
    Entity* e = SceneGetEntityMut(scene, id);
    if (e == nullptr) {
        err.set("NOT_FOUND", "La entidad '" + id + "' no existe.");
        return false;
    }
    if (!e->has_light) {
        err.set("INVALID_ARG", "La entidad '" + id + "' no tiene Light.");
        return false;
    }
    const float ch[3] = {c.x, c.y, c.z};
    for (int i = 0; i < 3; ++i) {
        if (std::isfinite(ch[i]) == 0 || ch[i] < 0.0f || ch[i] > 1.0f) {
            err.set("INVALID_ARG", "El color de la luz requiere componentes RGB en 0..1.");
            return false;
        }
    }
    if (std::isfinite(intensity) == 0 || intensity < 0.0f) {
        err.set("INVALID_ARG", "La intensidad de la luz debe ser un número finito no negativo.");
        return false;
    }
    e->light.color = c;
    e->light.intensity = intensity;
    return true;
}

bool EntitySetCamera(Scene& scene, const std::string& id, float fov_degrees, Error& err) {
    err.clear();
    Entity* e = SceneGetEntityMut(scene, id);
    if (e == nullptr) {
        err.set("NOT_FOUND", "La entidad '" + id + "' no existe.");
        return false;
    }
    if (!e->has_camera) {
        err.set("INVALID_ARG", "La entidad '" + id + "' no tiene Camera.");
        return false;
    }
    if (std::isfinite(fov_degrees) == 0 || fov_degrees < 1.0f || fov_degrees > 179.0f) {
        err.set("INVALID_ARG", "El FOV de la cámara debe estar entre 1 y 179 grados.");
        return false;
    }
    e->camera.fov_degrees = fov_degrees;
    return true;
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out += static_cast<char>(ch);
                }
                break;
        }
    }
    return out;
}

std::string EntityToJson(const Entity& e) {
    std::ostringstream oss;
    oss << "{\"id\":\"" << JsonEscape(e.id) << "\",\"name\":\"" << JsonEscape(e.name)
        << "\",\"components\":{";
    bool first = true;
    if (e.has_transform) {
        oss << "\"Transform\":{\"position\":[" << FmtFloat(e.transform.position.x) << ","
            << FmtFloat(e.transform.position.y) << "," << FmtFloat(e.transform.position.z)
            << "],\"rotation\":[" << FmtFloat(e.transform.rotation.x) << ","
            << FmtFloat(e.transform.rotation.y) << "," << FmtFloat(e.transform.rotation.z)
            << "],\"scale\":[" << FmtFloat(e.transform.scale.x) << "," << FmtFloat(e.transform.scale.y)
            << "," << FmtFloat(e.transform.scale.z) << "]}";
        first = false;
    }
    if (e.has_mesh) {
        if (!first) oss << ",";
        if (e.mesh.primitive == "asset")
            oss << "\"Mesh\":{\"primitive\":\"asset\",\"asset_id\":\"" << JsonEscape(e.mesh.asset_id)
                << "\",\"color\":[" << FmtFloat(e.mesh.color.x) << "," << FmtFloat(e.mesh.color.y)
                << "," << FmtFloat(e.mesh.color.z) << "]}";
        else
            oss << "\"Mesh\":{\"primitive\":\"cube\",\"color\":[" << FmtFloat(e.mesh.color.x) << ","
                << FmtFloat(e.mesh.color.y) << "," << FmtFloat(e.mesh.color.z) << "]}";
        first = false;
    }
    if (e.has_camera) {
        if (!first) oss << ",";
        oss << "\"Camera\":{\"fov_degrees\":" << FmtFloat(e.camera.fov_degrees) << "}";
        first = false;
    }
    if (e.has_light) {
        if (!first) oss << ",";
        oss << "\"Light\":{\"color\":[" << FmtFloat(e.light.color.x) << ","
            << FmtFloat(e.light.color.y) << "," << FmtFloat(e.light.color.z)
            << "],\"intensity\":" << FmtFloat(e.light.intensity) << "}";
    }
    oss << "}}";
    return oss.str();
}

std::string SceneListToJson(const Project& proj, const std::vector<std::string>& scenes) {
    std::ostringstream oss;
    oss << "{\"schema_version\":" << kSchemaVersion << ",\"ok\":true,\"result\":{\"active_scene\":";
    if (proj.active_scene.empty())
        oss << "null";
    else
        oss << "\"" << JsonEscape(proj.active_scene) << "\"";
    oss << ",\"scenes\":[";
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        if (i != 0) oss << ",";
        oss << "\"" << JsonEscape(scenes[i]) << "\"";
    }
    oss << "]}}";
    return oss.str();
}

std::string StateToJson(const Project& proj, const Scene& scene) {
    std::ostringstream oss;
    oss << "{\"schema_version\":" << kSchemaVersion << ",\"ok\":true,\"result\":{\"project\":\""
        << JsonEscape(proj.name) << "\",\"scene\":\"" << JsonEscape(scene.name) << "\",\"sky\":";
    if (scene.sky_asset.empty())
        oss << "null";
    else
        oss << "\"" << JsonEscape(scene.sky_asset) << "\"";
    oss << ",\"entities\":[";
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        if (i != 0) oss << ",";
        oss << EntityToJson(scene.entities[i]);
    }
    oss << "]}}";
    return oss.str();
}

std::string ProjectToJson(const Project& proj) {
    std::ostringstream oss;
    oss << "{\"schema_version\":" << kSchemaVersion << ",\"ok\":true,\"result\":{\"name\":\""
        << JsonEscape(proj.name) << "\",\"active_scene\":";
    if (proj.active_scene.empty())
        oss << "null";
    else
        oss << "\"" << JsonEscape(proj.active_scene) << "\"";
    oss << "}}";
    return oss.str();
}

std::string EntityResultToJson(const Entity& e) {
    std::ostringstream oss;
    oss << "{\"schema_version\":" << kSchemaVersion << ",\"ok\":true,\"result\":" << EntityToJson(e)
        << "}";
    return oss.str();
}

unsigned long long CurrentProcessId() { return ProcessId(); }

std::filesystem::path TempPathFor(const std::filesystem::path& dst) {
    unsigned long long n = g_tmp_counter.fetch_add(1, std::memory_order_relaxed);
    return std::filesystem::path(dst.string() + ".tmp." + std::to_string(ProcessId()) + "." +
                                 std::to_string(n));
}

bool AtomicReplaceFile(const std::filesystem::path& tmp, const std::filesystem::path& dst,
                       Error& err) {
    return AtomicReplace(tmp, dst, err);
}

bool AtomicWriteTextFile(const std::filesystem::path& dst, const std::string& text, Error& err) {
    return AtomicWriteText(dst, text, err);
}

}  // namespace oasis
