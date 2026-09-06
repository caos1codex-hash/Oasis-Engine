#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace oasis {

inline constexpr const char* kVersion = "0.2.0";
inline constexpr int kSchemaVersion = 1;
inline constexpr int kFormat = 1;
inline constexpr const char* kEngineName = "Oasis Engine";
inline constexpr std::size_t kMaxName = 64;  // incluye terminador: id válido 1..63
inline constexpr std::size_t kMaxEntities = 1024;
inline constexpr std::size_t kMaxScenes = 1024;
inline constexpr std::uint64_t kMaxFileBytes = 64ULL * 1024ULL * 1024ULL;

struct Error {
    std::string code = "INTERNAL";  // INVALID_ARG, NOT_FOUND, ALREADY_EXISTS, IO, BAD_FORMAT, LIMIT, GPU, INTERNAL
    std::string message;
    void set(std::string c, std::string m) {
        code = std::move(c);
        message = std::move(m);
    }
    void clear() {
        code = "INTERNAL";
        message.clear();
    }
    bool empty() const { return message.empty(); }
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Transform {
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct Mesh {
    std::string primitive = "cube";  // "cube" | "asset"
    std::string asset_id;            // solo si primitive == "asset"
    Vec3 color{1.0f, 0.55f, 0.15f};
};

struct Camera {
    float fov_degrees = 60.0f;
};

struct Light {
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

struct Entity {
    std::string id;
    std::string name;
    bool has_transform = false;
    bool has_mesh = false;
    bool has_camera = false;
    bool has_light = false;
    Transform transform{};
    Mesh mesh{};
    Camera camera{};
    Light light{};
};

struct Scene {
    std::string name;
    std::vector<Entity> entities;
};

struct Project {
    std::filesystem::path root = ".";
    std::string name;
    std::string active_scene;  // vacío = ninguna
};

// Validación
bool ValidId(const std::string& id);
bool ValidSceneName(const std::string& name);

// Proyecto / escena
bool ProjectCreate(const std::filesystem::path& root, const std::string& name, Error& err);
bool ProjectLoad(const std::filesystem::path& root, Project& out, Error& err);
bool SceneCreate(Project& proj, const std::string& name, Error& err);
bool SceneOpen(Project& proj, const std::string& name, Error& err);
bool SceneList(const Project& proj, std::vector<std::string>& out, Error& err);
bool SceneDelete(Project& proj, const std::string& name, Error& err);
bool SceneLoadActive(const Project& proj, Scene& out, Error& err);
bool SceneSaveActive(const Project& proj, const Scene& scene, Error& err);

// Entidades
bool SceneAddEntity(Scene& scene, const std::string& id, Error& err);
bool EntityAddComponent(Scene& scene, const std::string& id, const std::string& component, Error& err);
bool EntityDelete(Scene& scene, const std::string& id, Error& err);
const Entity* SceneGetEntity(const Scene& scene, const std::string& id);
Entity* SceneGetEntityMut(Scene& scene, const std::string& id);
bool EntitySetTransform(Scene& scene, const std::string& id, const std::string& property, const Vec3& v,
                        Error& err);
// Nota: valida formato de asset_id pero no su existencia en el manifiesto.
// La CLI (main.cpp set-model) verifica AssetListLoad y devuelve NOT_FOUND si falta.
bool EntitySetMeshAsset(Scene& scene, const std::string& id, const std::string& asset_id, Error& err);
// Color del Mesh (componentes RGB en 0..1). Exige entidad con Mesh.
bool EntitySetMeshColor(Scene& scene, const std::string& id, const Vec3& c, Error& err);
// Color (RGB 0..1) e intensidad (finita no negativa) de la luz. Exige Light.
bool EntitySetLight(Scene& scene, const std::string& id, const Vec3& c, float intensity,
                    Error& err);

// JSON para CLI (siempre válido, con escape)
std::string JsonEscape(const std::string& s);
std::string EntityToJson(const Entity& e);
std::string SceneListToJson(const Project& proj, const std::vector<std::string>& scenes);
std::string StateToJson(const Project& proj, const Scene& scene);
std::string ProjectToJson(const Project& proj);
std::string EntityResultToJson(const Entity& e);

// Escritura atómica compartida (core.cpp) — reutilizada por assets.cpp.
// Evita duplicar ProcessId/contador/rename entre módulos.
unsigned long long CurrentProcessId();
std::filesystem::path TempPathFor(const std::filesystem::path& dst);
bool AtomicReplaceFile(const std::filesystem::path& tmp, const std::filesystem::path& dst, Error& err);
bool AtomicWriteTextFile(const std::filesystem::path& dst, const std::string& text, Error& err);

}  // namespace oasis
