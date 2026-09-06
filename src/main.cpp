#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "assets.hpp"
#include "core.hpp"
#include "renderer.hpp"
#include "runtime.hpp"

namespace {

using oasis::Error;

void PrintUsageHuman() {
    std::fprintf(stderr,
                 "Oasis Engine %s\n"
                 "Uso:\n"
                 "  oasis version\n"
                 "  oasis project create NOMBRE [--path RUTA]\n"
                 "  oasis project open [RUTA | --project RUTA]\n"
                 "  oasis scene create NOMBRE [--project RUTA]\n"
                 "  oasis scene open NOMBRE [--project RUTA]\n"
                 "  oasis scene list [--project RUTA]\n"
                 "  oasis scene delete NOMBRE [--project RUTA]\n"
                 "  oasis entity create ID [--project RUTA]\n"
                 "  oasis entity add-component ID Transform|Mesh|Camera|Light [--project RUTA]\n"
                  "  oasis entity set ID Transform.position|Transform.rotation|Transform.scale X Y "
                  "Z [--project RUTA]\n"
                  "  oasis entity set-color ID R G B [--project RUTA]\n"
                  "  oasis entity set-light ID R G B INTENSIDAD [--project RUTA]\n"
                 "  oasis entity set-model ID ASSET [--project RUTA]\n"
                 "  oasis entity get ID [--project RUTA]\n"
                 "  oasis entity delete ID [--project RUTA]\n"
                 "  oasis asset import ID ARCHIVO.glb [--project RUTA]\n"
                 "  oasis asset convert-obj ID ARCHIVO.obj [--project RUTA]\n"
                 "  oasis asset list [--project RUTA]\n"
                  "  oasis state [--project RUTA]\n"
                  "  oasis run [--project RUTA] [--ticks N] [--window] [--modo ventana|completa|barra] [--vsync 0|1]\n"
                  "  oasis stop [--project RUTA] [--save]\n",
                 oasis::kVersion);
}

int FailJson(const Error& err, int exit_code) {
    std::string code = err.code.empty() ? "INTERNAL" : err.code;
    std::string msg = err.message.empty() ? "operación no válida" : err.message;
    std::printf("{\"schema_version\":%d,\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
                oasis::kSchemaVersion, oasis::JsonEscape(code).c_str(),
                oasis::JsonEscape(msg).c_str());
    std::fprintf(stderr, "Error [%s]: %s\n", code.c_str(), msg.c_str());
    return exit_code;
}

int FailUsage(const std::string& msg) {
    Error e;
    e.set("INVALID_ARG", msg);
    PrintUsageHuman();
    return FailJson(e, 2);
}

int FailOp(const Error& err) {
    int code = (err.code == "INVALID_ARG") ? 2 : 1;
    // Uso inválido de negocio (nombre malo) también es 2; resto 1.
    if (err.code == "INVALID_ARG" || err.code == "ALREADY_EXISTS") code = 2;
    return FailJson(err, code);
}

void SuccessOp(const std::string& operation) {
    std::printf("{\"schema_version\":%d,\"ok\":true,\"result\":{\"operation\":\"%s\"}}\n",
                oasis::kSchemaVersion, oasis::JsonEscape(operation).c_str());
}

bool ParseFloatStrict(const char* text, float& out) {
    if (text == nullptr || *text == '\0') return false;
    if (std::isspace(static_cast<unsigned char>(text[0])) != 0) return false;
    char* end = nullptr;
    errno = 0;
    float v = std::strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0') return false;
    if (std::isfinite(v) == 0) return false;
    out = v;
    return true;
}

bool ParseUintStrict(const char* text, std::uint64_t& out, std::uint64_t max_allowed) {
    if (text == nullptr || *text == '\0') return false;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    if (std::strlen(text) > 7) return false;  // evita wrap; max 9.999.999
    char* end = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    if (v > max_allowed) return false;
    out = static_cast<std::uint64_t>(v);
    return true;
}

// Extrae [--project RUTA] del final. idx apunta al primer arg opcional.
// Devuelve root ("." por defecto). Si hay tokens inesperados, ok=false.
bool ParseProjectFlag(int& idx, int argc, char** argv, std::string& root_out, Error& err) {
    root_out = ".";
    if (idx >= argc) return true;
    if (std::strcmp(argv[idx], "--project") == 0) {
        if (idx + 1 >= argc) {
            err.set("INVALID_ARG", "Falta la ruta después de --project.");
            return false;
        }
        if (argv[idx + 1][0] == '\0' || std::strcmp(argv[idx + 1], "--window") == 0 ||
            std::strcmp(argv[idx + 1], "--ticks") == 0) {
            err.set("INVALID_ARG", "Ruta de --project inválida.");
            return false;
        }
        root_out = argv[idx + 1];
        idx += 2;
    }
    if (idx != argc) {
        err.set("INVALID_ARG", "Argumentos inesperados.");
        return false;
    }
    return true;
}

// Resuelve el DemoGame junto al ejecutable o en uno de sus directorios padre.
// Esto permite que Oasis.exe funcione al abrirse con doble clic desde la raíz
// del proyecto, pero también desde la carpeta de compilación.
std::string FindBundledDemo(const char* executable_path) {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::absolute(executable_path, ec).parent_path();
    if (ec) return {};
    for (int level = 0; level < 4; ++level) {
        std::filesystem::path candidate = dir / "DemoGame";
        if (std::filesystem::is_regular_file(candidate / "oasis.project", ec) && !ec)
            return candidate.string();
        std::filesystem::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    const bool launch_demo = argc == 1;
    std::string cmd = launch_demo ? "run" : argv[1];

    if (cmd == "version") {
        if (argc != 2) return FailUsage("Uso: oasis version");
        std::printf("{\"schema_version\":%d,\"ok\":true,\"result\":{\"version\":\"%s\"}}\n",
                    oasis::kSchemaVersion, oasis::kVersion);
        return 0;
    }

    if (cmd == "project") {
        if (argc < 3) return FailUsage("Uso: oasis project create|open ...");
        std::string sub = argv[2];
        if (sub == "create") {
            if (argc != 4 && argc != 6) return FailUsage("Uso: oasis project create NOMBRE [--path RUTA]");
            std::string name = argv[3];
            std::string root = name;
            if (argc == 6) {
                if (std::strcmp(argv[4], "--path") != 0 && std::strcmp(argv[4], "--project") != 0)
                    return FailUsage("Uso: oasis project create NOMBRE [--path RUTA]");
                root = argv[5];
                if (root.empty()) return FailUsage("Ruta de proyecto inválida.");
            }
            Error err;
            if (!oasis::ProjectCreate(root, name, err)) return FailOp(err);
            SuccessOp("project.create");
            return 0;
        }
        if (sub == "open") {
            std::string root = ".";
            if (argc == 3) {
                root = ".";
            } else if (argc == 4) {
                root = argv[3];
            } else if (argc == 5 && std::strcmp(argv[3], "--project") == 0) {
                root = argv[4];
            } else {
                return FailUsage("Uso: oasis project open [RUTA | --project RUTA]");
            }
            Error err;
            oasis::Project proj;
            if (!oasis::ProjectLoad(root, proj, err)) return FailOp(err);
            std::printf("%s\n", oasis::ProjectToJson(proj).c_str());
            return 0;
        }
        return FailUsage("Uso: oasis project create|open ...");
    }

    if (cmd == "scene") {
        if (argc < 3) return FailUsage("Uso: oasis scene create|open|list|delete ...");
        std::string sub = argv[2];
        if (sub == "list") {
            int idx = 3;
            std::string root;
            Error err;
            if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
            oasis::Project proj;
            if (!oasis::ProjectLoad(root, proj, err)) return FailOp(err);
            std::vector<std::string> list;
            if (!oasis::SceneList(proj, list, err)) return FailOp(err);
            std::printf("%s\n", oasis::SceneListToJson(proj, list).c_str());
            return 0;
        }
        if (argc < 4) return FailUsage("Uso: oasis scene create|open|delete NOMBRE [--project RUTA]");
        std::string name = argv[3];
        int idx = 4;
        std::string root;
        Error err;
        if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
        oasis::Project proj;
        if (!oasis::ProjectLoad(root, proj, err)) return FailOp(err);
        if (sub == "create") {
            if (!oasis::SceneCreate(proj, name, err)) return FailOp(err);
            SuccessOp("scene.create");
            return 0;
        }
        if (sub == "open") {
            if (!oasis::SceneOpen(proj, name, err)) return FailOp(err);
            SuccessOp("scene.open");
            return 0;
        }
        if (sub == "delete") {
            if (!oasis::SceneDelete(proj, name, err)) return FailOp(err);
            SuccessOp("scene.delete");
            return 0;
        }
        return FailUsage("Uso: oasis scene create|open|list|delete ...");
    }

    if (cmd == "entity") {
        if (argc < 4) return FailUsage("Uso: oasis entity ...");
        std::string sub = argv[2];
        std::string id = argv[3];
        if (sub == "create" || sub == "get" || sub == "delete") {
            int idx = 4;
            std::string root;
            Error err;
            if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
            oasis::Project proj;
            oasis::Scene scene;
            if (!oasis::ProjectLoad(root, proj, err) || !oasis::SceneLoadActive(proj, scene, err))
                return FailOp(err);
            if (sub == "create") {
                if (!oasis::SceneAddEntity(scene, id, err)) return FailOp(err);
                if (!oasis::SceneSaveActive(proj, scene, err)) return FailOp(err);
                SuccessOp("entity.create");
                return 0;
            }
            if (sub == "delete") {
                if (!oasis::EntityDelete(scene, id, err)) return FailOp(err);
                if (!oasis::SceneSaveActive(proj, scene, err)) return FailOp(err);
                SuccessOp("entity.delete");
                return 0;
            }
            const oasis::Entity* e = oasis::SceneGetEntity(scene, id);
            if (e == nullptr) {
                Error ne;
                ne.set("NOT_FOUND", "La entidad '" + id + "' no existe.");
                return FailOp(ne);
            }
            std::printf("%s\n", oasis::EntityResultToJson(*e).c_str());
            return 0;
        }
        if (sub == "add-component") {
            if (argc < 5) return FailUsage("Uso: oasis entity add-component ID Comp [--project RUTA]");
            std::string comp = argv[4];
            int idx = 5;
            std::string root;
            Error err;
            if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
            oasis::Project proj;
            oasis::Scene scene;
            if (!oasis::ProjectLoad(root, proj, err) || !oasis::SceneLoadActive(proj, scene, err))
                return FailOp(err);
            if (!oasis::EntityAddComponent(scene, id, comp, err)) return FailOp(err);
            if (!oasis::SceneSaveActive(proj, scene, err)) return FailOp(err);
            SuccessOp("entity.add_component");
            return 0;
        }
        if (sub == "set") {
            if (argc < 8) return FailUsage("Uso: oasis entity set ID Transform.xxx X Y Z [--project RUTA]");
            std::string prop_full = argv[4];
            const char* prefix = "Transform.";
            if (prop_full.rfind(prefix, 0) != 0) {
                Error e;
                e.set("INVALID_ARG", "Solo se admite Transform.* en V0.2.");
                return FailOp(e);
            }
            std::string prop = prop_full.substr(std::strlen(prefix));
            float vals[3];
            for (int i = 0; i < 3; ++i) {
                if (!ParseFloatStrict(argv[5 + i], vals[i])) {
                    Error e;
                    e.set("INVALID_ARG", "Los valores de Transform deben ser números finitos.");
                    return FailOp(e);
                }
            }
            int idx = 8;
            std::string root;
            Error err;
            if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
            oasis::Project proj;
            oasis::Scene scene;
            if (!oasis::ProjectLoad(root, proj, err) || !oasis::SceneLoadActive(proj, scene, err))
                return FailOp(err);
            oasis::Vec3 v{vals[0], vals[1], vals[2]};
            if (!oasis::EntitySetTransform(scene, id, prop, v, err)) return FailOp(err);
            if (!oasis::SceneSaveActive(proj, scene, err)) return FailOp(err);
            SuccessOp("entity.set");
            return 0;
        }
        if (sub == "set-color") {
            if (argc < 7) return FailUsage("Uso: oasis entity set-color ID R G B [--project RUTA]");
            float rgb[3];
            for (int i = 0; i < 3; ++i) {
                if (!ParseFloatStrict(argv[4 + i], rgb[i])) {
                    Error e;
                    e.set("INVALID_ARG", "El color requiere componentes RGB en 0..1.");
                    return FailOp(e);
                }
            }
            int idx = 7;
            std::string root;
            Error err;
            if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
            oasis::Project proj;
            oasis::Scene scene;
            if (!oasis::ProjectLoad(root, proj, err) || !oasis::SceneLoadActive(proj, scene, err))
                return FailOp(err);
            oasis::Vec3 c{rgb[0], rgb[1], rgb[2]};
            if (!oasis::EntitySetMeshColor(scene, id, c, err)) return FailOp(err);
            if (!oasis::SceneSaveActive(proj, scene, err)) return FailOp(err);
            SuccessOp("entity.set_color");
            return 0;
        }
        if (sub == "set-light") {
            if (argc < 8) return FailUsage("Uso: oasis entity set-light ID R G B INTENSIDAD [--project RUTA]");
            float vals[4];
            for (int i = 0; i < 4; ++i) {
                if (!ParseFloatStrict(argv[4 + i], vals[i])) {
                    Error e;
                    e.set("INVALID_ARG", "La luz requiere R G B en 0..1 e intensidad no negativa.");
                    return FailOp(e);
                }
            }
            int idx = 8;
            std::string root;
            Error err;
            if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
            oasis::Project proj;
            oasis::Scene scene;
            if (!oasis::ProjectLoad(root, proj, err) || !oasis::SceneLoadActive(proj, scene, err))
                return FailOp(err);
            oasis::Vec3 c{vals[0], vals[1], vals[2]};
            if (!oasis::EntitySetLight(scene, id, c, vals[3], err)) return FailOp(err);
            if (!oasis::SceneSaveActive(proj, scene, err)) return FailOp(err);
            SuccessOp("entity.set_light");
            return 0;
        }
        if (sub == "set-model") {            if (argc < 5) return FailUsage("Uso: oasis entity set-model ID ASSET [--project RUTA]");
            std::string asset = argv[4];
            int idx = 5;
            std::string root;
            Error err;
            if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
            oasis::Project proj;
            oasis::Scene scene;
            if (!oasis::ProjectLoad(root, proj, err) || !oasis::SceneLoadActive(proj, scene, err))
                return FailOp(err);
            // Validar que el asset exista en el manifiesto antes de referenciarlo.
            // Sin esto se guardaría un asset_id fantasma que el renderer saltaría en silencio.
            {
                oasis::AssetList assets;
                if (!oasis::AssetListLoad(proj, assets, err)) return FailOp(err);
                bool found = false;
                for (const auto& a : assets.items) {
                    if (a.id == asset) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    Error ne;
                    ne.set("NOT_FOUND", "El asset '" + asset + "' no existe en el manifiesto.");
                    return FailOp(ne);
                }
            }
            if (!oasis::EntitySetMeshAsset(scene, id, asset, err)) return FailOp(err);
            if (!oasis::SceneSaveActive(proj, scene, err)) return FailOp(err);
            SuccessOp("entity.set_model");
            return 0;
        }
        return FailUsage("Uso: oasis entity ...");
    }

    if (cmd == "state") {
        int idx = 2;
        std::string root;
        Error err;
        if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
        oasis::Project proj;
        oasis::Scene scene;
        if (!oasis::ProjectLoad(root, proj, err) || !oasis::SceneLoadActive(proj, scene, err))
            return FailOp(err);
        std::printf("%s\n", oasis::StateToJson(proj, scene).c_str());
        return 0;
    }

    if (cmd == "asset") {
        if (argc < 3) return FailUsage("Uso: oasis asset list|import|convert-obj ...");
        std::string sub = argv[2];
        if (sub == "list") {
            int idx = 3;
            std::string root;
            Error err;
            if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
            oasis::Project proj;
            if (!oasis::ProjectLoad(root, proj, err)) return FailOp(err);
            oasis::AssetList list;
            if (!oasis::AssetListLoad(proj, list, err)) return FailOp(err);
            std::printf("%s\n", oasis::AssetListToJson(list).c_str());
            return 0;
        }
        if ((sub == "import" || sub == "convert-obj") && argc >= 5) {
            std::string aid = argv[3];
            std::string src = argv[4];
            int idx = 5;
            std::string root;
            Error err;
            if (!ParseProjectFlag(idx, argc, argv, root, err)) return FailOp(err);
            oasis::Project proj;
            if (!oasis::ProjectLoad(root, proj, err)) return FailOp(err);
            bool ok = (sub == "import") ? oasis::AssetImportGlb(proj, aid, src, err)
                                        : oasis::AssetConvertObj(proj, aid, src, err);
            if (!ok) return FailOp(err);
            SuccessOp(sub == "import" ? "asset.import" : "asset.convert_obj");
            return 0;
        }
        return FailUsage("Uso: oasis asset list|import|convert-obj ...");
    }

    if (cmd == "run") {
        std::string root = launch_demo ? FindBundledDemo(argv[0]) : ".";
        if (launch_demo && root.empty()) {
            Error e;
            e.set("NOT_FOUND", "No se encontró DemoGame junto a Oasis.exe.");
            return FailOp(e);
        }
        bool window = launch_demo;
        bool fullscreen_flag = false;  // alias de --modo completa
        std::string modo = launch_demo ? "barra" : "";
        bool modo_given = launch_demo;
        bool ticks_given = false;
        std::uint64_t ticks = 1;
        bool vsync = true;
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--window") {
                window = true;
            } else if (a == "--fullscreen") {
                fullscreen_flag = true;
            } else if (a == "--modo") {
                if (i + 1 >= argc) return FailUsage("Falta el valor después de --modo.");
                modo = argv[i + 1];
                if (modo != "ventana" && modo != "completa" && modo != "barra")
                    return FailUsage("Uso: --modo ventana|completa|barra.");
                modo_given = true;
                ++i;
            } else if (a == "--project") {
                if (i + 1 >= argc) return FailUsage("Falta la ruta después de --project.");
                std::string v = argv[i + 1];
                if (v.empty() || v.rfind("--", 0) == 0) return FailUsage("Ruta de --project inválida.");
                root = v;
                ++i;
            } else if (a == "--ticks") {
                if (i + 1 >= argc) return FailUsage("Falta el valor después de --ticks.");
                std::uint64_t v = 0;
                if (!ParseUintStrict(argv[i + 1], v, 1000000)) {
                    Error e;
                    e.set("INVALID_ARG", "--ticks requiere un entero 0..1000000.");
                    return FailOp(e);
                }
                ticks = v;
                ticks_given = true;
                ++i;
            } else if (a == "--vsync") {
                if (i + 1 >= argc) return FailUsage("Falta el valor después de --vsync.");
                std::string v = argv[i + 1];
                if (v != "0" && v != "1") return FailUsage("Uso: --vsync 0|1.");
                vsync = (v == "1");
                ++i;
            } else {
                return FailUsage("Flag desconocido en run: " + a);
            }
        }
        if ((fullscreen_flag || modo_given) && !window)
            return FailUsage("Uso: --fullscreen/--modo requieren --window.");
        if (fullscreen_flag && modo_given && modo != "completa")
            return FailUsage("Uso: --fullscreen equivale a --modo completa (conflicto).");
        Error err;
        oasis::Project proj;
        oasis::Scene scene;
        if (!oasis::ProjectLoad(root, proj, err) || !oasis::SceneLoadActive(proj, scene, err))
            return FailOp(err);
        oasis::Runtime rt;
        if (!rt.init(&scene, err)) return FailOp(err);
        if (!window) {
            for (std::uint64_t t = 0; t < ticks; ++t) {
                if (!rt.update(1.0 / 60.0, err)) return FailOp(err);
            }
            char elapsed[32];
            std::snprintf(elapsed, sizeof(elapsed), "%.6f", rt.elapsed_seconds);
            std::printf(
                "{\"schema_version\":%d,\"ok\":true,\"result\":{\"project\":\"%s\",\"scene\":\"%s\","
                "\"ticks\":%llu,\"elapsed_seconds\":%s}}\n",
                oasis::kSchemaVersion, oasis::JsonEscape(proj.name).c_str(),
                oasis::JsonEscape(scene.name).c_str(),
                static_cast<unsigned long long>(rt.tick_count), elapsed);
            return 0;
        }
        oasis::RenderConfig cfg;
        cfg.max_ticks = ticks_given ? ticks : 0;
        cfg.vsync = vsync;
        if (fullscreen_flag || (modo_given && modo == "completa"))
            cfg.mode = oasis::WindowMode::Fullscreen;
        else if (modo_given && modo == "barra")
            cfg.mode = oasis::WindowMode::WorkArea;
        std::string backend;
        int rc = oasis::RendererRun(proj, rt, cfg, backend, err);
        if (rc != 0) return FailOp(err);
        const char* mode_name = cfg.mode == oasis::WindowMode::Fullscreen ? "completa"
                                : cfg.mode == oasis::WindowMode::WorkArea  ? "barra"
                                                                          : "ventana";
        std::printf(
            "{\"schema_version\":%d,\"ok\":true,\"result\":{\"project\":\"%s\",\"scene\":\"%s\","
            "\"ticks\":%llu,\"elapsed_seconds\":%.6f,\"backend\":\"%s\",\"mode\":\"%s\",\"vsync\":%s}}\n",
            oasis::kSchemaVersion, oasis::JsonEscape(proj.name).c_str(),
            oasis::JsonEscape(scene.name).c_str(),
            static_cast<unsigned long long>(rt.tick_count), rt.elapsed_seconds,
            oasis::JsonEscape(backend).c_str(), mode_name, cfg.vsync ? "true" : "false");
        return 0;
    }

    if (cmd == "stop") {
        std::string root = ".";
        bool save = false;
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--save") {
                save = true;
            } else if (a == "--project") {
                if (i + 1 >= argc) return FailUsage("Falta la ruta después de --project.");
                std::string v = argv[i + 1];
                if (v.empty() || v.rfind("--", 0) == 0) return FailUsage("Ruta de --project inválida.");
                root = v;
                ++i;
            } else {
                return FailUsage("Flag desconocido en stop: " + a);
            }
        }
        Error err;
        bool signaled = false;
        if (!oasis::RequestStopForRoot(root, signaled, err, save)) return FailOp(err);
        std::printf(
            "{\"schema_version\":%d,\"ok\":true,\"result\":{\"operation\":\"stop\",\"stopped\":%s,"
            "\"save\":%s}}\n",
            oasis::kSchemaVersion, signaled ? "true" : "false", save ? "true" : "false");
        if (!signaled)
            std::fprintf(stderr, "Sin ventana escuchando para ese proyecto.\n");
        else if (save)
            std::fprintf(stderr, "Parada con guardado señalada a la ventana.\n");
        else
            std::fprintf(stderr, "Parada señalada a la ventana (sale sin guardar).\n");
        return 0;
    }

    return FailUsage("Comando desconocido: " + cmd);
}
