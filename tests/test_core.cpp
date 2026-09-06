// Tests sin framework: cada fallo imprime a stderr y retorna 1.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "assets.hpp"
#include "core.hpp"
#include "runtime.hpp"

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

namespace fs = std::filesystem;

static int g_failures = 0;
#define EXPECT(cond, msg)                                                                  \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::fprintf(stderr, "FALLO [%s:%d]: %s\n", __FILE__, __LINE__, msg);           \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

static fs::path TempRoot() {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec) base = fs::current_path(ec);
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
#else
    long pid = static_cast<long>(::getpid());
#endif
    return base / ("OasisV2Test_" + std::to_string(pid));
}

static bool WriteText(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write(s.data(), static_cast<std::streamsize>(s.size()));
    o.close();
    return static_cast<bool>(o);
}

static bool WriteMinimalGlb(const fs::path& p) {
    // GLB 2.0 mínimo: header 12 + chunk JSON "{}  " (4+4+4)
    unsigned char glb[] = {'g', 'l', 'T', 'F', 2, 0, 0, 0, 24, 0, 0, 0,
                           4,   0,   0,   0,   'J', 'S', 'O', 'N', '{', '}', ' ', ' '};
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write(reinterpret_cast<const char*>(glb), sizeof(glb));
    o.close();
    return static_cast<bool>(o);
}

int main() {
    oasis::Error err;
    fs::path root = TempRoot();
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // 1. Proyecto + escenas
    EXPECT(oasis::ProjectCreate(root, "TestGame", err), err.message.c_str());
    oasis::Project proj;
    EXPECT(oasis::ProjectLoad(root, proj, err), err.message.c_str());
    EXPECT(proj.name == "TestGame", "nombre proyecto");
    std::vector<std::string> lst;
    EXPECT(oasis::SceneList(proj, lst, err) && lst.empty(), "proyecto nuevo sin escenas");
    EXPECT(oasis::SceneCreate(proj, "Main", err), err.message.c_str());
    EXPECT(oasis::SceneCreate(proj, "Second", err), err.message.c_str());
    EXPECT(oasis::SceneList(proj, lst, err), err.message.c_str());
    EXPECT(lst.size() == 2 && lst[0] == "Main" && lst[1] == "Second", "lista ordenada");
    EXPECT(oasis::SceneOpen(proj, "Main", err), err.message.c_str());
    EXPECT(oasis::SceneDelete(proj, "Second", err), err.message.c_str());
    EXPECT(oasis::SceneList(proj, lst, err) && lst.size() == 1, "delete escena");

    // No se puede borrar la activa
    EXPECT(!oasis::SceneDelete(proj, "Main", err), "delete activa debe fallar");
    EXPECT(!err.message.empty(), "delete activa con mensaje");

    // 2. Entidades + componentes + persistencia
    oasis::Scene scene;
    EXPECT(oasis::SceneLoadActive(proj, scene, err), err.message.c_str());
    EXPECT(oasis::SceneAddEntity(scene, "Cube", err), err.message.c_str());
    EXPECT(!oasis::SceneAddEntity(scene, "Cube", err), "duplicado debe fallar");
    EXPECT(!oasis::SceneAddEntity(scene, "mal id!", err), "id inválido debe fallar");
    EXPECT(oasis::EntityAddComponent(scene, "Cube", "Transform", err), err.message.c_str());
    EXPECT(oasis::EntityAddComponent(scene, "Cube", "Mesh", err), err.message.c_str());
    EXPECT(oasis::EntityAddComponent(scene, "Cube", "Camera", err), err.message.c_str());
    EXPECT(oasis::EntityAddComponent(scene, "Cube", "Light", err), err.message.c_str());
    EXPECT(!oasis::EntityAddComponent(scene, "Cube", "Physics", err), "comp inválido debe fallar");
    EXPECT(!oasis::EntityAddComponent(scene, "Nope", "Mesh", err), "entidad ausente debe fallar");
    oasis::Vec3 p{5.0f, 0.0f, 0.0f};
    EXPECT(oasis::EntitySetTransform(scene, "Cube", "position", p, err), err.message.c_str());
    EXPECT(!oasis::EntitySetTransform(scene, "Cube", "bogus", p, err), "prop inválida debe fallar");
    oasis::Vec3 nanv{std::numeric_limits<float>::quiet_NaN(), 0, 0};
    EXPECT(!oasis::EntitySetTransform(scene, "Cube", "position", nanv, err), "NaN debe fallar");
    EXPECT(oasis::SceneSaveActive(proj, scene, err), err.message.c_str());
    // Sin .tmp residual
    EXPECT(!fs::exists(root / "scenes" / "Main.scene.json.tmp.0.0", ec), "sin tmp determinista");
    bool any_tmp = false;
    for (auto& e : fs::directory_iterator(root / "scenes", ec)) {
        if (e.path().string().find(".tmp.") != std::string::npos) any_tmp = true;
    }
    EXPECT(!any_tmp, "guardado atómico sin temporales");

    oasis::Scene reloaded;
    EXPECT(oasis::SceneLoadActive(proj, reloaded, err), err.message.c_str());
    EXPECT(reloaded.entities.size() == 1, "persiste entidad");
    const oasis::Entity* cube = oasis::SceneGetEntity(reloaded, "Cube");
    EXPECT(cube != nullptr && cube->has_mesh && cube->has_camera && cube->has_light,
           "flags persisten");
    EXPECT(cube != nullptr && cube->transform.position.x == 5.0f, "position persiste");
    EXPECT(cube != nullptr && cube->transform.scale.x == 1.0f, "scale default persiste");

    // JSON escape válido
    std::string st = oasis::StateToJson(proj, reloaded);
    EXPECT(st.find("\"Cube\"") != std::string::npos, "state contiene Cube");
    EXPECT(st.find("\"schema_version\":1") != std::string::npos, "state schema");

    // 3. Negativos JSON: carga fallida no muta
    fs::path spath = root / "scenes" / "Main.scene.json";
    EXPECT(WriteText(spath, "{\"format\":1,\"name\":\"Main\",\"entities\":[}"), "escribir inválido");
    EXPECT(!oasis::SceneLoadActive(proj, reloaded, err), "JSON inválido debe fallar");
    EXPECT(reloaded.entities.size() == 1, "carga fallida no muta");
    EXPECT(WriteText(spath, "{\"format\":1,\"name\":\"Main\",\"entities\":[{\"components\":{}}]}"),
           "escribir sin id");
    EXPECT(!oasis::SceneLoadActive(proj, reloaded, err), "sin id debe fallar");
    EXPECT(WriteText(spath, "{\"format\":1,\"name\":\"Main\",\"entities\":[{\"id\":\"Cube\","
                            "\"components\":{\"Transform\":{\"position\":\"wrong\",\"rotation\":[0,0,"
                            "0],\"scale\":[1,1,1]}}}]}"),
           "escribir tipo malo");
    EXPECT(!oasis::SceneLoadActive(proj, reloaded, err), "tipo malo debe fallar");
    // Traversal en id
    EXPECT(WriteText(spath, "{\"format\":1,\"name\":\"Main\",\"entities\":[{\"id\":\"../evil\","
                            "\"components\":{}}]}"),
           "escribir traversal");
    EXPECT(!oasis::SceneLoadActive(proj, reloaded, err), "traversal debe fallar");

    // Restaurar escena válida para runtime/assets
    EXPECT(oasis::SceneLoadActive(proj, scene, err) || true, "intentoload");
    {
        // Reconstruir Main válido desde memoria 'scene' original (tiene Cube)
        // 'scene' aún tiene Cube en memoria (no fue mutada por loads fallidos sobre 'reloaded')
        EXPECT(oasis::SceneSaveActive(proj, scene, err), err.message.c_str());
    }
    EXPECT(oasis::SceneLoadActive(proj, reloaded, err), err.message.c_str());

    // 4. Runtime
    oasis::Runtime rt;
    EXPECT(oasis::SceneLoadActive(proj, reloaded, err), err.message.c_str());
    EXPECT(rt.init(&reloaded, err), err.message.c_str());
    EXPECT(rt.update(1.0 / 60.0, err) && rt.update(1.0 / 60.0, err), err.message.c_str());
    EXPECT(rt.tick_count == 2 && rt.elapsed_seconds > 0.0, "ticks acumulan");
    rt.shutdown();
    EXPECT(!rt.update(1.0 / 60.0, err), "apagado debe rechazar update");
    EXPECT(!rt.init(nullptr, err), "init null debe fallar");

    // 5. Assets
    fs::path glb = root / "sample.glb";
    EXPECT(WriteMinimalGlb(glb), "glb mínimo");
    EXPECT(oasis::AssetImportGlb(proj, "TestModel", glb, err), err.message.c_str());
    EXPECT(oasis::AssetImportGlb(proj, "TestModel", glb, err), "reimport idempotente");
    fs::path obj = root / "tri.obj";
    EXPECT(WriteText(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"), "obj triángulo");
    EXPECT(oasis::AssetConvertObj(proj, "Triangle", obj, err), err.message.c_str());
    oasis::AssetList al;
    EXPECT(oasis::AssetListLoad(proj, al, err), err.message.c_str());
    EXPECT(al.items.size() == 2, "dos assets");
    EXPECT(fs::exists(root / "assets" / "source" / "TestModel.glb", ec), "source existe");
    EXPECT(fs::exists(root / "assets" / "cooked" / "TestModel.oasisasset", ec), "cooked existe");
    // set-model persiste
    EXPECT(oasis::EntitySetMeshAsset(reloaded, "Cube", "Triangle", err), err.message.c_str());
    EXPECT(oasis::SceneSaveActive(proj, reloaded, err), err.message.c_str());
    oasis::Scene again;
    EXPECT(oasis::SceneLoadActive(proj, again, err), err.message.c_str());
    const oasis::Entity* c2 = oasis::SceneGetEntity(again, "Cube");
    EXPECT(c2 != nullptr && c2->mesh.primitive == "asset" && c2->mesh.asset_id == "Triangle",
           "mesh asset persiste");
    // set-color persiste y valida rango
    oasis::Vec3 red{1.0f, 0.0f, 0.0f};
    EXPECT(oasis::EntitySetMeshColor(again, "Cube", red, err), err.message.c_str());
    EXPECT(oasis::SceneSaveActive(proj, again, err), err.message.c_str());
    oasis::Scene colored;
    EXPECT(oasis::SceneLoadActive(proj, colored, err), err.message.c_str());
    const oasis::Entity* c3 = oasis::SceneGetEntity(colored, "Cube");
    EXPECT(c3 != nullptr && c3->mesh.color.x == 1.0f && c3->mesh.color.y == 0.0f &&
               c3->mesh.color.z == 0.0f,
           "mesh color persiste");
    oasis::Vec3 bad{2.0f, 0.0f, 0.0f};
    EXPECT(!oasis::EntitySetMeshColor(colored, "Cube", bad, err), "color >1 debe fallar");
    EXPECT(!oasis::EntitySetMeshColor(colored, "Nope", red, err), "color sin entidad debe fallar");
    // set-light: el Cube temporal trae Light de la sección 2; el rechazo se
    // prueba con una entidad que solo tiene Mesh.
    EXPECT(oasis::SceneAddEntity(colored, "Plain", err), err.message.c_str());
    EXPECT(oasis::EntityAddComponent(colored, "Plain", "Mesh", err), err.message.c_str());
    EXPECT(!oasis::EntitySetLight(colored, "Plain", red, 1.0f, err), "luz sin Light debe fallar");
    EXPECT(oasis::SceneAddEntity(colored, "Sun", err), err.message.c_str());
    EXPECT(oasis::EntityAddComponent(colored, "Sun", "Light", err), err.message.c_str());
    oasis::Vec3 warm{1.0f, 0.8f, 0.6f};
    EXPECT(oasis::EntitySetLight(colored, "Sun", warm, 2.5f, err), err.message.c_str());
    EXPECT(oasis::SceneSaveActive(proj, colored, err), err.message.c_str());
    oasis::Scene lit;
    EXPECT(oasis::SceneLoadActive(proj, lit, err), err.message.c_str());
    const oasis::Entity* sun = oasis::SceneGetEntity(lit, "Sun");
    EXPECT(sun != nullptr && sun->has_light && sun->light.color.y == 0.8f &&
               sun->light.intensity == 2.5f,
           "luz persiste");
    EXPECT(!oasis::EntitySetLight(lit, "Sun", warm, -1.0f, err), "intensidad <0 debe fallar");
    // set-camera
    EXPECT(oasis::EntityAddComponent(lit, "Sun", "Camera", err), err.message.c_str());
    EXPECT(oasis::EntitySetCamera(lit, "Sun", 90.0f, err), err.message.c_str());
    EXPECT(oasis::SceneSaveActive(proj, lit, err), err.message.c_str());
    oasis::Scene cammed;
    EXPECT(oasis::SceneLoadActive(proj, cammed, err), err.message.c_str());
    const oasis::Entity* scam = oasis::SceneGetEntity(cammed, "Sun");
    EXPECT(scam != nullptr && scam->has_camera && scam->camera.fov_degrees == 90.0f,
           "fov persiste");
    EXPECT(!oasis::EntitySetCamera(cammed, "Sun", 0.0f, err), "fov 0 debe fallar");
    EXPECT(!oasis::EntitySetCamera(cammed, "Plain", 60.0f, err), "fov sin Camera debe fallar");

    // 8. Sky: .hdr mínimo + import-sky + round-trip en escena
    {
        // Radiance 2x1 plano (ancho<8 permite scanlines sin RLE).
        std::string hdr = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
        const char px[8] = {10, 10, 10, 100, 20, 10, 5, 100};
        hdr.append(px, sizeof(px));
        EXPECT(WriteText(root / "sky.hdr", hdr), "hdr mínimo");
        EXPECT(oasis::AssetImportSky(proj, "DaySky", root / "sky.hdr", err), err.message.c_str());
        oasis::AssetList al2;
        EXPECT(oasis::AssetListLoad(proj, al2, err), err.message.c_str());
        bool found_sky = false;
        for (const auto& a : al2.items) {
            if (a.id == "DaySky" && a.kind == "sky") found_sky = true;
        }
        EXPECT(found_sky, "sky en manifiesto con kind");
        EXPECT(fs::exists(root / "assets" / "source" / "DaySky.hdr", ec), "hdr importado");
        cammed.sky_asset = "DaySky";
        EXPECT(oasis::SceneSaveActive(proj, cammed, err), err.message.c_str());
        oasis::Scene with_sky;
        EXPECT(oasis::SceneLoadActive(proj, with_sky, err), err.message.c_str());
        EXPECT(with_sky.sky_asset == "DaySky", "sky persiste");
        with_sky.sky_asset.clear();
        EXPECT(oasis::SceneSaveActive(proj, with_sky, err), err.message.c_str());
        oasis::Scene no_sky;
        EXPECT(oasis::SceneLoadActive(proj, no_sky, err) && no_sky.sky_asset.empty(),
               "sky none persiste");
        // Sky inválido se rechaza sin mutar.
        fs::path spath2 = root / "scenes" / "Main.scene.json";
        EXPECT(WriteText(spath2, "{\"format\":1,\"name\":\"Main\",\"sky\":{\"asset_id\":\"mal "
                                 "id!\"},\"entities\":[]}"),
               "escribir sky malo");
        EXPECT(!oasis::SceneLoadActive(proj, no_sky, err), "sky malo debe fallar");
        EXPECT(no_sky.sky_asset.empty(), "carga fallida no muta sky");
        EXPECT(WriteText(spath2, "{\"format\":1,\"name\":\"Main\",\"sky\":5,\"entities\":[]}"),
               "escribir sky tipo malo");
        EXPECT(!oasis::SceneLoadActive(proj, no_sky, err), "sky tipo malo debe fallar");
    }

    // 9. Física: cubo cae sobre suelo estático y reposa (headless, determinista)
    {
        oasis::Scene phys;
        phys.name = "Main";
        EXPECT(oasis::SceneAddEntity(phys, "Floor", err), err.message.c_str());
        EXPECT(oasis::EntityAddComponent(phys, "Floor", "Transform", err), err.message.c_str());
        EXPECT(oasis::EntityAddComponent(phys, "Floor", "Collider", err), err.message.c_str());
        oasis::Vec3 fpos{0.0f, -0.25f, 0.0f};
        EXPECT(oasis::EntitySetTransform(phys, "Floor", "position", fpos, err),
               err.message.c_str());
        oasis::Vec3 fscl{10.0f, 0.5f, 10.0f};
        EXPECT(oasis::EntitySetTransform(phys, "Floor", "scale", fscl, err), err.message.c_str());
        EXPECT(oasis::SceneAddEntity(phys, "Box", err), err.message.c_str());
        EXPECT(oasis::EntityAddComponent(phys, "Box", "Transform", err), err.message.c_str());
        EXPECT(oasis::EntityAddComponent(phys, "Box", "RigidBody", err), err.message.c_str());
        EXPECT(oasis::EntityAddComponent(phys, "Box", "Collider", err), err.message.c_str());
        oasis::Vec3 bpos{0.0f, 5.0f, 0.0f};
        EXPECT(oasis::EntitySetTransform(phys, "Box", "position", bpos, err), err.message.c_str());
        oasis::Runtime prt;
        EXPECT(prt.init(&phys, err), err.message.c_str());
        for (int t = 0; t < 300; ++t) EXPECT(prt.update(1.0 / 60.0, err), err.message.c_str());
        const oasis::Entity* box = oasis::SceneGetEntity(phys, "Box");
        EXPECT(box != nullptr, "caja existe tras simular");
        // Suelo: cara superior y=0; caja 1x1x1 -> reposo en y=0.5, velocidad 0.
        EXPECT(box != nullptr && box->transform.position.y > 0.4f &&
                   box->transform.position.y < 0.6f,
               "caja reposa sobre el suelo");
        EXPECT(box != nullptr && box->rigidbody.velocity.x == 0.0f &&
                   box->rigidbody.velocity.y == 0.0f && box->rigidbody.velocity.z == 0.0f,
               "caja dormida en reposo");
        // Sin Transform no integra ni colisiona (se ignora, no falla).
        EXPECT(oasis::SceneAddEntity(phys, "Ghost", err), err.message.c_str());
        EXPECT(oasis::EntityAddComponent(phys, "Ghost", "RigidBody", err), err.message.c_str());
        EXPECT(oasis::EntityAddComponent(phys, "Ghost", "Collider", err), err.message.c_str());
        EXPECT(prt.update(1.0 / 60.0, err), err.message.c_str());
        // JSON inválido de física se rechaza.
        fs::path spath3 = root / "scenes" / "Main.scene.json";
        EXPECT(WriteText(spath3, "{\"format\":1,\"name\":\"Main\",\"entities\":[{\"id\":\"X\","
                                 "\"components\":{\"RigidBody\":{\"velocity\":[0,0,0],\"mass\":0,"
                                 "\"use_gravity\":true}}}]}"),
               "escribir masa 0");
        oasis::Scene badphys;
        EXPECT(!oasis::SceneLoadActive(proj, badphys, err), "masa 0 debe fallar");
        prt.shutdown();
    }

    // 6. Delete entidad persiste
    EXPECT(oasis::EntityDelete(again, "Cube", err), err.message.c_str());
    EXPECT(again.entities.empty(), "delete memoria");
    EXPECT(oasis::SceneSaveActive(proj, again, err), err.message.c_str());
    oasis::Scene empty;
    EXPECT(oasis::SceneLoadActive(proj, empty, err) && empty.entities.empty(), "delete persiste");

    // 7. ValidId
    EXPECT(oasis::ValidId("A-_09") && !oasis::ValidId("") && !oasis::ValidId("a b") &&
               !oasis::ValidId("../x"),
           "ValidId");

    if (g_failures == 0) {
        std::printf("oasis_core_v2: OK\n");
        fs::remove_all(root, ec);
        return 0;
    }
    std::fprintf(stderr, "oasis_core_v2: %d fallos\n", g_failures);
    return 1;
}
