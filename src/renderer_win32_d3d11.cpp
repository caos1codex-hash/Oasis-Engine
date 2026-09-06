#include "renderer.hpp"

#include "cJSON.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef _WIN32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#endif

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace oasis {
namespace {

#ifdef _WIN32

// Solicitud explícita para gráficos híbridos.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

struct Vertex {
    float position[3];
    float color[3];
};
struct SceneBuffer {
    float wvp[16];
    float world[16];   // world matrix para posición en world-space (Blinn-Phong)
    float tint[4];     // rgb = color, a = 1.0 lit / 0.0 unlit (overlays)
    float light_dir[4]; // dirección normalizada de la luz (xyz), w padding
    float light_col[4]; // color de luz × intensidad (rgb), w padding
    float eye_pos[4];   // posición de la cámara (xyz), w padding
};

struct ModelCache {
    std::string asset_id;
    ID3D11Buffer* vertices = nullptr;
    ID3D11Buffer* indices = nullptr;
    UINT index_count = 0;
    float bmin[3] = {-0.5f, -0.5f, -0.5f};  // bounds en espacio del modelo
    float bmax[3] = {0.5f, 0.5f, 0.5f};
};

struct Context {
    bool running = true;
    bool fwd = false, back = false, left = false, right = false, down = false, up = false;
    bool fast = false;
    bool has_focus = false;
    std::uint64_t event_count = 0;
    std::uint64_t mouse_event_count = 0;
    bool mouse_look = false;  // Ctrl lo activa/desactiva (pointer-lock al centro)
    bool cursor_hidden = false;
    int last_x = 0;
    int last_y = 0;
    float pending_yaw_px = 0.0f;
    float pending_pitch_px = 0.0f;
    char locked_axis = 0;  // 'X','Y','Z' o 0: bloqueo de eje para arrastre (teclas X/Y/Z)
    int view_w = 800;      // último tamaño cliente (para matemática de arrastre)
    int view_h = 600;
    Scene* scene = nullptr;        // escena viva del runtime (para picking/arrastre)
    const Project* proj = nullptr;  // proyecto vivo (para redibujar durante resize modal)
    std::string selected;          // id de entidad seleccionada ("" = ninguna)
    bool dragging = false;         // botón izq. mantenido sobre una selección
    HWND info_hwnd = nullptr;      // ventana flotante de datos (click der.)
    HWND info_edit = nullptr;
    HFONT info_font = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    ID3D11Texture2D* depth_tex = nullptr;
    ID3D11DepthStencilView* depth = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cube_vb = nullptr;
    ID3D11Buffer* cube_ib = nullptr;
    ID3D11Buffer* matrices = nullptr;
    ID3D11Buffer* cross_vb = nullptr;      // crosshair "+" (dinámico, 8 vértices)
    ID3D11Buffer* gizmo_vb = nullptr;      // gizmo ejes + línea roja (dinámico, 20 vértices)
    ID3D11Buffer* corner_vb = nullptr;     // widget orientación esq. sup. der. (dinámico, 26)
    ID3D11DepthStencilState* no_depth = nullptr;  // para dibujar el overlay 2D
    int buf_w = 0;  // tamaño real del backbuffer (resize en cada frame si cambia)
    int buf_h = 0;
    std::vector<ModelCache> models;
    std::vector<std::string> model_warned;  // asset_id ya avisados por stderr (sin spam por frame)
};

void ReleaseContext(Context& c) {
    for (auto& m : c.models) {
        if (m.indices != nullptr) m.indices->Release();
        if (m.vertices != nullptr) m.vertices->Release();
    }
    c.models.clear();
    if (c.matrices != nullptr) c.matrices->Release();
    if (c.cross_vb != nullptr) c.cross_vb->Release();
    if (c.gizmo_vb != nullptr) c.gizmo_vb->Release();
    if (c.corner_vb != nullptr) c.corner_vb->Release();
    if (c.no_depth != nullptr) c.no_depth->Release();
    if (c.cube_ib != nullptr) c.cube_ib->Release();
    if (c.cube_vb != nullptr) c.cube_vb->Release();
    if (c.layout != nullptr) c.layout->Release();
    if (c.ps != nullptr) c.ps->Release();
    if (c.vs != nullptr) c.vs->Release();
    if (c.depth != nullptr) c.depth->Release();
    if (c.depth_tex != nullptr) c.depth_tex->Release();
    if (c.target != nullptr) c.target->Release();
    if (c.swap != nullptr) c.swap->Release();
    if (c.context != nullptr) c.context->Release();
    if (c.device != nullptr) c.device->Release();
    c.matrices = nullptr;
    c.cross_vb = nullptr;
    c.gizmo_vb = nullptr;
    c.corner_vb = nullptr;
    c.no_depth = nullptr;
    c.cube_ib = nullptr;
    c.cube_vb = nullptr;
    c.layout = nullptr;
    c.ps = nullptr;
    c.vs = nullptr;
    c.depth = nullptr;
    c.depth_tex = nullptr;
    c.target = nullptr;
    c.swap = nullptr;
    c.context = nullptr;
    c.device = nullptr;
}

// Pointer-lock estilo juegos: cursor oculto, confinado a la ventana y recentrado
// al centro cada frame. Los deltas son relativos (píxeles desde el centro), así el
// mouse nunca llega al borde de la pantalla.
void LockMouse(HWND hwnd, Context& ctx) {
    RECT rc{};
    ::GetClientRect(hwnd, &rc);
    POINT center{(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
    ::ClientToScreen(hwnd, &center);
    RECT wr{};
    ::GetWindowRect(hwnd, &wr);
    (void)::ClipCursor(&wr);
    (void)::SetCursorPos(center.x, center.y);
    if (!ctx.cursor_hidden) {
        ::ShowCursor(FALSE);
        ctx.cursor_hidden = true;
    }
    ctx.mouse_look = true;
    ctx.pending_yaw_px = 0.0f;
    ctx.pending_pitch_px = 0.0f;
}

void UnlockMouse(Context& ctx) {
    (void)::ClipCursor(nullptr);
    if (ctx.cursor_hidden) {
        ::ShowCursor(TRUE);
        ctx.cursor_hidden = false;
    }
    ctx.mouse_look = false;
    ctx.pending_yaw_px = 0.0f;
    ctx.pending_pitch_px = 0.0f;
}

// Llamar una vez por frame antes de UpdateCamera. Lee la posición real del cursor,
// la convierte en delta relativo al centro y devuelve el cursor al centro.
void PollPointerLock(HWND hwnd, Context& ctx) {
    if (!ctx.mouse_look || !ctx.has_focus) return;
    RECT rc{};
    ::GetClientRect(hwnd, &rc);
    if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) return;
    POINT center{(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
    ::ClientToScreen(hwnd, &center);
    POINT pos{};
    if (::GetCursorPos(&pos) == 0) return;
    float dx = static_cast<float>(pos.x - center.x);
    float dy = static_cast<float>(pos.y - center.y);
    if (dx != 0.0f || dy != 0.0f) {
        if (std::isfinite(dx) != 0 && std::isfinite(dy) != 0) {
            ctx.pending_yaw_px += dx;
            ctx.pending_pitch_px += dy;
            ++ctx.mouse_event_count;
        }
        (void)::SetCursorPos(center.x, center.y);
    }
}

// --- Picking: rayo contra cajas de entidades (cubo unidad escalado, con yaw) ---
struct Ray {
    float o[3];
    float d[3];
};

bool RayAABB(const float o[3], const float d[3], const float mn[3], const float mx[3],
             float& t_out) {
    float tmin = 0.0f, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-8f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
        } else {
            float inv = 1.0f / d[i];
            float a = (mn[i] - o[i]) * inv, b = (mx[i] - o[i]) * inv;
            if (a > b) {
                float t = a;
                a = b;
                b = t;
            }
            if (a > tmin) tmin = a;
            if (b < tmax) tmax = b;
            if (tmin > tmax) return false;
        }
    }
    t_out = tmin;
    return true;
}

// Rotación 3x3 row-major: R = Ry(yaw) * Rx(pitch) * Rz(roll), misma convención
// row-vector que el shader (mul(float4(p,1),M)). Con pitch=roll=0 es idéntica
// al yaw-only histórico (filas [c,0,-s] / [0,1,0] / [s,0,c]).
void RotationMatrix33(float r[9], const Transform& t) {
    float cy = std::cos(t.rotation.y), sy = std::sin(t.rotation.y);
    float cp = std::cos(t.rotation.x), sp = std::sin(t.rotation.x);
    float cr = std::cos(t.rotation.z), sr = std::sin(t.rotation.z);
    // C = Ry * Rx:
    // fila0 = (cy, sy*sp, -sy*cp), fila1 = (0, cp, sp), fila2 = (sy, -cy*sp, cy*cp)
    // R = C * Rz:
    r[0] = cy * cr - sy * sp * sr;
    r[1] = cy * sr + sy * sp * cr;
    r[2] = -sy * cp;
    r[3] = -cp * sr;
    r[4] = cp * cr;
    r[5] = sp;
    r[6] = sy * cr + cy * sp * sr;
    r[7] = sy * sr - cy * sp * cr;
    r[8] = cy * cp;
}

// Caja mundo: caja local [lmn,lmx] escalada, rotada (yaw+pitch+roll) y trasladada,
// con la MISMA matriz que WorldMatrix: picking y dibujo siempre coinciden.
// Nota: GetModel se define más abajo; declaración adelantada para el picking.
ModelCache* GetModel(Context& ctx, const Project& proj, const std::string& asset_id, Error& err);
void BoxWorldAABB(const float lmn[3], const float lmx[3], const Transform& t, float mn[3],
                  float mx[3]) {
    float ex = (t.scale.x != 0.0f ? t.scale.x : 1.0f);
    float ey = (t.scale.y != 0.0f ? t.scale.y : 1.0f);
    float ez = (t.scale.z != 0.0f ? t.scale.z : 1.0f);
    float r[9];
    RotationMatrix33(r, t);
    mn[0] = mn[1] = mn[2] = 1e30f;
    mx[0] = mx[1] = mx[2] = -1e30f;
    for (int ix = 0; ix < 2; ++ix)
        for (int iy = 0; iy < 2; ++iy)
            for (int iz = 0; iz < 2; ++iz) {
                float lx = (ix == 0 ? lmn[0] : lmx[0]) * ex;
                float ly = (iy == 0 ? lmn[1] : lmx[1]) * ey;
                float lz = (iz == 0 ? lmn[2] : lmx[2]) * ez;
                float wx = r[0] * lx + r[3] * ly + r[6] * lz + t.position.x;
                float wy = r[1] * lx + r[4] * ly + r[7] * lz + t.position.y;
                float wz = r[2] * lx + r[5] * ly + r[8] * lz + t.position.z;
                if (wx < mn[0]) mn[0] = wx;
                if (wy < mn[1]) mn[1] = wy;
                if (wz < mn[2]) mn[2] = wz;
                if (wx > mx[0]) mx[0] = wx;
                if (wy > mx[1]) mx[1] = wy;
                if (wz > mx[2]) mx[2] = wz;
            }
}

// Caja mundo aproximada: cubo unidad centrado en position, escalado y rotado.
void EntityWorldAABB(const Entity& e, float mn[3], float mx[3]) {
    const float lmn[3] = {-0.5f, -0.5f, -0.5f};
    const float lmx[3] = {0.5f, 0.5f, 0.5f};
    BoxWorldAABB(lmn, lmx, e.transform, mn, mx);
}

// Proyecta un punto mundo a píxeles de pantalla (misma convención que el shader:
// row-major, mul(float4(p,1),wvp)). Devuelve false si está detrás o degenera.
bool ProjectToScreen(const float P[3], const float view[16], const float proj[16], int w, int h,
                     float& sx, float& sy) {
    float vx = P[0] * view[0] + P[1] * view[4] + P[2] * view[8] + view[12];
    float vy = P[0] * view[1] + P[1] * view[5] + P[2] * view[9] + view[13];
    float vz = P[0] * view[2] + P[1] * view[6] + P[2] * view[10] + view[14];
    float cx = vx * proj[0] + vz * proj[8];
    float cy = vy * proj[5] + vz * proj[9];
    float cw = vz * proj[11] + proj[15];
    if (!(cw > 1e-6f) || std::isfinite(cx) == 0 || std::isfinite(cy) == 0 ||
        std::isfinite(cw) == 0)
        return false;  // detrás de cámara o degenerado: no mover este frame
    float nx = cx / cw, ny = cy / cw;
    sx = (nx * 0.5f + 0.5f) * static_cast<float>(w);
    sy = (0.5f - ny * 0.5f) * static_cast<float>(h);
    return std::isfinite(sx) != 0 && std::isfinite(sy) != 0;
}

void CameraBasis(const Transform& cam, float f[3], float r[3], float u[3]) {
    f[0] = std::sin(cam.rotation.y) * std::cos(cam.rotation.x);
    f[1] = std::sin(cam.rotation.x);
    f[2] = std::cos(cam.rotation.y) * std::cos(cam.rotation.x);
    r[0] = std::cos(cam.rotation.y);
    r[1] = 0.0f;
    r[2] = -std::sin(cam.rotation.y);
    u[0] = f[1] * r[2] - f[2] * r[1];
    u[1] = f[2] * r[0] - f[0] * r[2];
    u[2] = f[0] * r[1] - f[1] * r[0];
}

Ray CenterRay(const Transform& cam) {
    Ray ray{};
    float r[3], u[3], f[3];
    CameraBasis(cam, f, r, u);
    ray.o[0] = cam.position.x;
    ray.o[1] = cam.position.y;
    ray.o[2] = cam.position.z;
    ray.d[0] = f[0];
    ray.d[1] = f[1];
    ray.d[2] = f[2];
    return ray;
}

Ray CursorRay(int px, int py, int w, int h, const Transform& cam, float fov_deg) {
    Ray ray{};
    float f[3], r[3], u[3];
    CameraBasis(cam, f, r, u);
    float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 4.0f / 3.0f;
    float fov = (fov_deg >= 1.0f && fov_deg <= 179.0f) ? fov_deg : 60.0f;
    float th = std::tan(fov * 0.01745329252f * 0.5f);
    float nx = (w > 0) ? (2.0f * static_cast<float>(px) / static_cast<float>(w) - 1.0f) : 0.0f;
    float ny = (h > 0) ? (1.0f - 2.0f * static_cast<float>(py) / static_cast<float>(h)) : 0.0f;
    float vx = nx * th * aspect, vy = ny * th;
    float dx = vx * r[0] + vy * u[0] + f[0];
    float dy = vx * r[1] + vy * u[1] + f[1];
    float dz = vx * r[2] + vy * u[2] + f[2];
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(len > 1e-6f) || std::isfinite(len) == 0) {
        dx = f[0];
        dy = f[1];
        dz = f[2];
        len = 1.0f;
    }
    ray.o[0] = cam.position.x;
    ray.o[1] = cam.position.y;
    ray.o[2] = cam.position.z;
    ray.d[0] = dx / len;
    ray.d[1] = dy / len;
    ray.d[2] = dz / len;
    return ray;
}

const Entity* PickEntity(Context& ctx, const Project& proj, const Scene& scene, const Ray& ray,
                         float& best_t) {
    const Entity* hit = nullptr;
    best_t = 1e30f;
    for (const auto& e : scene.entities) {
        if (!e.has_mesh) continue;  // solo mallas visibles son seleccionables
        float mn[3], mx[3];
        if (e.mesh.primitive == "asset" && !e.mesh.asset_id.empty()) {
            // Caja real del GLB (no la unidad): sin esto, un asset escalado a 0.19
            // tendría una caja 8 veces menor que lo que se ve.
            Error tmp;
            ModelCache* m = GetModel(ctx, proj, e.mesh.asset_id, tmp);
            if (m != nullptr) {
                BoxWorldAABB(m->bmin, m->bmax, e.transform, mn, mx);
            } else {
                EntityWorldAABB(e, mn, mx);  // fallback si el asset no carga
            }
        } else {
            EntityWorldAABB(e, mn, mx);
        }
        float t = 0.0f;
        if (RayAABB(ray.o, ray.d, mn, mx, t) && t < best_t) {
            best_t = t;
            hit = &e;
        }
    }
    return hit;
}

std::string EntityInfoText(const Entity& e) {
    std::ostringstream oss;
    oss << "id: " << e.id << "\r\nname: " << e.name << "\r\n";
    if (e.has_transform)
        oss << "[Transform] position: [" << e.transform.position.x << ", " << e.transform.position.y
            << ", " << e.transform.position.z << "] rotation: [" << e.transform.rotation.x << ", "
            << e.transform.rotation.y << ", " << e.transform.rotation.z << "] scale: ["
            << e.transform.scale.x << ", " << e.transform.scale.y << ", " << e.transform.scale.z
            << "]\r\n";
    if (e.has_mesh) {
        oss << "[Mesh] primitive: " << e.mesh.primitive;
        if (e.mesh.primitive == "asset") oss << " asset_id: " << e.mesh.asset_id;
        oss << " color: [" << e.mesh.color.x << ", " << e.mesh.color.y << ", " << e.mesh.color.z
            << "]\r\n";
    }
    if (e.has_camera) oss << "[Camera] fov_degrees: " << e.camera.fov_degrees << "\r\n";
    if (e.has_light)
        oss << "[Light] color: [" << e.light.color.x << ", " << e.light.color.y << ", "
            << e.light.color.z << "] intensity: " << e.light.intensity << "\r\n";
    oss << "\r\n--- JSON ---\r\n" << EntityToJson(e) << "\r\n";
    return oss.str();
}

LRESULT CALLBACK InfoProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        ::ShowWindow(hwnd, SW_HIDE);  // la X oculta, no destruye
        return 0;
    }
    return ::DefWindowProcA(hwnd, msg, wp, lp);
}

// Cierra paneles flotantes (info/datos/edición). Devuelve true si cerró alguno:
// en ese caso ESC no debe salir todavía.
bool CloseTopPanels(Context& ctx) {
    bool closed = false;
    if (ctx.info_hwnd != nullptr && ::IsWindowVisible(ctx.info_hwnd) != FALSE) {
        ::ShowWindow(ctx.info_hwnd, SW_HIDE);
        closed = true;
    }
    return closed;
}

// Segundo paso de ESC: confirma la salida y guarda los cambios de la escena.
// Devuelve true si hay que salir.
bool ConfirmExitAndSave(HWND hwnd, Context& ctx) {
    if (CloseTopPanels(ctx)) return false;
    int r = ::MessageBoxA(hwnd, "Guardar los cambios de la escena y salir?", "Oasis Engine",
                          MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDYES) {
        if (ctx.scene != nullptr && ctx.proj != nullptr) {
            Error serr;
            if (!SceneSaveActive(*ctx.proj, *ctx.scene, serr)) {
                std::string msg = "No se pudo guardar: " + serr.message;
                ::MessageBoxA(hwnd, msg.c_str(), "Oasis Engine", MB_OK | MB_ICONERROR);
                return false;  // quedarse para no perder el trabajo
            }
        }
        return true;
    }
    if (r == IDNO) return true;  // salir sin guardar
    return false;                // Cancelar (o error): quedarse
}

void ShowEntityInfo(HWND main_hwnd, Context& ctx, const Entity& e) {
    if (ctx.info_hwnd == nullptr) {
        WNDCLASSA wc{};
        wc.lpfnWndProc = InfoProc;
        wc.hInstance = ::GetModuleHandleA(nullptr);
        wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "OasisInfoV2";
        if (::RegisterClassA(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        ctx.info_hwnd = ::CreateWindowExA(0, wc.lpszClassName, "Oasis · datos",
                                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                                          CW_USEDEFAULT, CW_USEDEFAULT, 540, 380, main_hwnd, nullptr,
                                          wc.hInstance, nullptr);
        if (ctx.info_hwnd == nullptr) return;
        ctx.info_edit = ::CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            8, 8, 506, 326, ctx.info_hwnd, nullptr, wc.hInstance, nullptr);
        if (ctx.info_edit == nullptr) {
            ::DestroyWindow(ctx.info_hwnd);
            ctx.info_hwnd = nullptr;
            return;
        }
        ctx.info_font = ::CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      FIXED_PITCH | FF_MODERN, "Consolas");
        if (ctx.info_font != nullptr)
            ::SendMessageA(ctx.info_edit, WM_SETFONT, reinterpret_cast<WPARAM>(ctx.info_font),
                           TRUE);
    }
    std::string title = "Oasis · " + e.id;
    ::SetWindowTextA(ctx.info_hwnd, title.c_str());
    std::string body = EntityInfoText(e);
    ::SetWindowTextA(ctx.info_edit, body.c_str());
    // Flotante junto al cursor sin robar el foco (el mouse-look lo necesita).
    POINT pt{};
    ::GetCursorPos(&pt);
    RECT wa{};
    ::SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int x = pt.x + 16, y = pt.y + 16;
    if (x + 540 > wa.right) x = wa.right - 540;
    if (y + 380 > wa.bottom) y = wa.bottom - 380;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    ::SetWindowPos(ctx.info_hwnd, HWND_TOP, x, y, 540, 380, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

// Rayo bajo el punto de mira: centro si el lock está activo, cursor si está libre.
const Entity* PickAt(Context& ctx, HWND hwnd, int mx, int my, float& t_out) {
    if (ctx.scene == nullptr || ctx.proj == nullptr) return nullptr;
    RECT rc{};
    ::GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    const Transform* cam_t = nullptr;
    float fov = 60.0f;
    for (const auto& e : ctx.scene->entities) {
        if (e.has_camera) {
            cam_t = &e.transform;
            fov = e.camera.fov_degrees;
            break;
        }
    }
    Transform fallback{};
    fallback.position = {0.0f, 3.0f, -10.0f};
    if (cam_t == nullptr) cam_t = &fallback;
    Ray ray = ctx.mouse_look ? CenterRay(*cam_t) : CursorRay(mx, my, w, h, *cam_t, fov);
    return PickEntity(ctx, *ctx.proj, *ctx.scene, ray, t_out);
}

bool EnsureBackbufferSize(Context& ctx, int w, int h, Error& err);
HRESULT DrawFrame(Context& ctx, const Project& proj, const Scene& scene, int width, int height,
                  bool vsync);

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Context* ctx =
        reinterpret_cast<Context*>(::GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
        ::SetWindowLongPtrA(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }
    if (ctx == nullptr) return ::DefWindowProcA(hwnd, msg, wp, lp);
    bool is_repeat = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && ((lp & (1L << 30)) != 0);
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (!is_repeat) ++ctx->event_count;
        if (wp == 'W') ctx->fwd = true;
        else if (wp == 'S') ctx->back = true;
        else if (wp == 'A') ctx->left = true;
        else if (wp == 'D') ctx->right = true;
        else if (wp == 'Q' || wp == 'C') ctx->down = true;
        else if (wp == 'E' || wp == VK_SPACE) ctx->up = true;
        else if (wp == 'X' || wp == 'Y' || wp == 'Z') {
            // Bloqueo de eje para arrastre: pulsar de nuevo libera. Vale también
            // armado antes del click (se aplica al arrastrar).
            if (!is_repeat) {
                char a = (wp == 'X') ? 'X' : ((wp == 'Y') ? 'Y' : 'Z');
                ctx->locked_axis = (ctx->locked_axis == a) ? 0 : a;
            }
        }
        else if (wp == VK_SHIFT) ctx->fast = true;
        else if (wp == VK_CONTROL) {
            // Ctrl = interruptor del mouse (solo en el flanco de pulsación, no en repetición).
            if (!is_repeat) {
                if (ctx->mouse_look) UnlockMouse(*ctx);
                else LockMouse(hwnd, *ctx);
            }
        }
        else if (wp == VK_ESCAPE) {
            // ESC por pasos: primero cierra paneles, luego confirma y guarda.
            if (!is_repeat && ConfirmExitAndSave(hwnd, *ctx)) ctx->running = false;
        }
        return 0;
    }
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        ++ctx->event_count;
        if (wp == 'W') ctx->fwd = false;
        else if (wp == 'S') ctx->back = false;
        else if (wp == 'A') ctx->left = false;
        else if (wp == 'D') ctx->right = false;
        else if (wp == 'Q' || wp == 'C') ctx->down = false;
        else if (wp == 'E' || wp == VK_SPACE) ctx->up = false;
        else if (wp == VK_SHIFT) ctx->fast = false;
        return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        // Con pointer-lock los deltas los genera PollPointerLock (relativos al centro).
        // Sin lock, un click solo selecciona. El arrastre empieza al mover el
        // ratón con el botón pulsado; así seleccionar no altera ni bloquea la vista.
        int mx = static_cast<int>(static_cast<short>(lp & 0xFFFF));
        int my =
            static_cast<int>(static_cast<short>((lp >> 16) & 0xFFFF));
        if (!ctx->mouse_look) {
            ++ctx->mouse_event_count;
            const int dx = mx - ctx->last_x;
            const int dy = my - ctx->last_y;
            if (!ctx->dragging && (wp & MK_LBUTTON) != 0 && !ctx->selected.empty() &&
                (dx != 0 || dy != 0)) {
                ctx->dragging = true;
            }
            if (ctx->dragging) {
                ctx->pending_yaw_px += static_cast<float>(dx);
                ctx->pending_pitch_px += static_cast<float>(dy);
            }
        }
        ctx->last_x = mx;
        ctx->last_y = my;
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        ++ctx->mouse_event_count;
        int mx = static_cast<int>(static_cast<short>(lp & 0xFFFF));
        int my =
            static_cast<int>(static_cast<short>((lp >> 16) & 0xFFFF));
        ctx->last_x = mx;
        ctx->last_y = my;
        float t = 0.0f;
        const Entity* hit = PickAt(*ctx, hwnd, mx, my, t);
        if (hit != nullptr) {
            ctx->selected = hit->id;
            // Con mouse:on (mira, pointer-lock) no hay WM_MOUSEMOVE de arrastre:
            // los deltas los genera PollPointerLock. Para poder mover el objeto
            // enfocado, el arrastre empieza al pulsar (mantener para mover,
            // soltar para terminar). Con mouse:off se mantiene el gesto clásico
            // (empieza al mover con el botón pulsado en WM_MOUSEMOVE).
            ctx->dragging = ctx->mouse_look;
            ::SetCapture(hwnd);  // recibir WM_LBUTTONUP aunque el cursor salga de la ventana
        } else {
            ctx->selected.clear();
            ctx->dragging = false;
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        ++ctx->mouse_event_count;
        ctx->dragging = false;  // soltar deja la selección marcada
        if (::GetCapture() == hwnd) ::ReleaseCapture();
        return 0;
    }
    if (msg == WM_RBUTTONDOWN) {
        ++ctx->mouse_event_count;
        int mx = static_cast<int>(static_cast<short>(lp & 0xFFFF));
        int my =
            static_cast<int>(static_cast<short>((lp >> 16) & 0xFFFF));
        float t = 0.0f;
        const Entity* hit = PickAt(*ctx, hwnd, mx, my, t);
        if (hit != nullptr) {
            ctx->selected = hit->id;  // click der.: ventana flotante con los datos
            ShowEntityInfo(hwnd, *ctx, *hit);
        }
        return 0;
    }
    if (msg == WM_SIZE) {
        // Redibujado en vivo: al arrastrar el borde, Windows bloquea nuestro bucle
        // dentro de un modal sizing loop; si no dibujamos aquí, el DWM estira el
        // último frame (imagen estirada) hasta soltar el botón. Sin vsync para ir al día.
        if (wp != SIZE_MINIMIZED && ctx->scene != nullptr && ctx->proj != nullptr &&
            ctx->device != nullptr && ctx->swap != nullptr) {
            int w = static_cast<int>(LOWORD(lp));
            int h = static_cast<int>(HIWORD(lp));
            if (w > 0 && h > 0) {
                Error rerr;
                if (EnsureBackbufferSize(*ctx, w, h, rerr)) {
                    (void)DrawFrame(*ctx, *ctx->proj, *ctx->scene, w, h, false);
                }
            }
        }
        return 0;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;  // el fondo lo pinta D3D; evita parpadeo GDI al redimensionar
    }
    if (msg == WM_SETFOCUS) {
        ctx->has_focus = true;
        return 0;
    }
    if (msg == WM_KILLFOCUS) {
        ctx->has_focus = false;
        ctx->fwd = ctx->back = ctx->left = ctx->right = ctx->down = ctx->up = ctx->fast = false;
        ctx->dragging = false;
        if (::GetCapture() == hwnd) ::ReleaseCapture();
        ctx->locked_axis = 0;
        // Perder el foco desactiva el mouse y restaura cursor/clip (hay que pulsar Ctrl de nuevo).
        UnlockMouse(*ctx);
        return 0;
    }
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        ctx->running = false;
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcA(hwnd, msg, wp, lp);
}

void MatIdentity(float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
void MatMul(float out[16], const float a[16], const float b[16]) {
    float r[16];
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            r[row * 4 + col] = 0.0f;
            for (int k = 0; k < 4; ++k) r[row * 4 + col] += a[row * 4 + k] * b[k * 4 + col];
        }
    std::memcpy(out, r, sizeof(r));
}
void WorldMatrix(float out[16], const Transform& t) {
    float r[9];
    RotationMatrix33(r, t);
    MatIdentity(out);
    out[0] = r[0] * t.scale.x;
    out[1] = r[1] * t.scale.x;
    out[2] = r[2] * t.scale.x;
    out[4] = r[3] * t.scale.y;
    out[5] = r[4] * t.scale.y;
    out[6] = r[5] * t.scale.y;
    out[8] = r[6] * t.scale.z;
    out[9] = r[7] * t.scale.z;
    out[10] = r[8] * t.scale.z;
    out[12] = t.position.x;
    out[13] = t.position.y;
    out[14] = t.position.z;
}
bool ViewMatrix(float out[16], const Transform& cam) {
    float eye[3] = {cam.position.x, cam.position.y, cam.position.z};
    float yaw = cam.rotation.y, pitch = cam.rotation.x;
    float tx = eye[0] + std::sin(yaw) * std::cos(pitch);
    float ty = eye[1] + std::sin(pitch);
    float tz = eye[2] + std::cos(yaw) * std::cos(pitch);
    float z[3] = {tx - eye[0], ty - eye[1], tz - eye[2]};
    float len = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    if (!(len > 1e-6f) || std::isfinite(len) == 0) return false;
    z[0] /= len;
    z[1] /= len;
    z[2] /= len;
    float x[3] = {z[2], 0.0f, -z[0]};  // up(0,1,0) x z
    len = std::sqrt(x[0] * x[0] + x[2] * x[2]);
    if (!(len > 1e-6f) || std::isfinite(len) == 0) return false;
    x[0] /= len;
    x[2] /= len;
    float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    MatIdentity(out);
    out[0] = x[0];
    out[1] = y[0];
    out[2] = z[0];
    out[4] = x[1];
    out[5] = y[1];
    out[6] = z[1];
    out[8] = x[2];
    out[9] = y[2];
    out[10] = z[2];
    out[12] = -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]);
    out[13] = -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]);
    out[14] = -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]);
    return true;
}
bool ProjectionMatrix(float out[16], float aspect, float fov_deg) {
    if (!(aspect > 0.0f) || std::isfinite(aspect) == 0) return false;
    if (!(fov_deg >= 1.0f && fov_deg <= 179.0f) || std::isfinite(fov_deg) == 0) return false;
    const float zn = 0.1f, zf = 100.0f;
    float fov = fov_deg * 0.01745329252f;
    float s = 1.0f / std::tan(fov * 0.5f);
    if (std::isfinite(s) == 0 || !(s > 0.0f)) return false;
    for (int i = 0; i < 16; ++i) out[i] = 0.0f;
    out[0] = s / aspect;
    out[5] = s;
    out[10] = zf / (zf - zn);
    out[11] = 1.0f;
    out[14] = -zn * zf / (zf - zn);
    return true;
}

bool CompileShader(const char* src, const char* entry, const char* profile, ID3DBlob** out,
                   Error& err) {
    ID3DBlob* errors = nullptr;
    HRESULT hr = ::D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, entry, profile,
                              D3DCOMPILE_ENABLE_STRICTNESS, 0, out, &errors);
    if (FAILED(hr)) {
        std::string msg = "No se pudo compilar el shader.";
        if (errors != nullptr && errors->GetBufferPointer() != nullptr)
            msg += std::string(" ") + static_cast<const char*>(errors->GetBufferPointer());
        if (errors != nullptr) errors->Release();
        err.set("GPU", msg);
        return false;
    }
    if (errors != nullptr) errors->Release();
    return true;
}

std::uint32_t U32Le(const unsigned char* b) {
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8U) |
           (static_cast<std::uint32_t>(b[2]) << 16U) | (static_cast<std::uint32_t>(b[3]) << 24U);
}

bool LoadAssetGeometry(const Project& proj, const std::string& asset_id,
                       std::vector<Vertex>& verts, std::vector<std::uint32_t>& idx, Error& err) {
    if (!ValidId(asset_id)) {
        err.set("INVALID_ARG", "Identificador de asset inválido.");
        return false;
    }
    std::filesystem::path path = proj.root / "assets" / "source" / (asset_id + ".glb");
    std::error_code ec;
    std::uintmax_t fsize = std::filesystem::file_size(path, ec);
    if (ec || fsize < 28 || fsize > 64ULL * 1024ULL * 1024ULL) return false;
    std::vector<unsigned char> glb(static_cast<std::size_t>(fsize));
    std::FILE* f = nullptr;
    if (::fopen_s(&f, path.string().c_str(), "rb") != 0 || f == nullptr) return false;
    std::size_t got = std::fread(glb.data(), 1, glb.size(), f);
    std::fclose(f);
    if (got != glb.size()) return false;
    if (std::memcmp(glb.data(), "glTF", 4) != 0 || U32Le(glb.data() + 4) != 2U ||
        U32Le(glb.data() + 8) != static_cast<std::uint32_t>(fsize) ||
        std::memcmp(glb.data() + 16, "JSON", 4) != 0)
        return false;
    std::uint32_t json_bytes = U32Le(glb.data() + 12);
    if (json_bytes > fsize - 28U) return false;
    if (std::memcmp(glb.data() + 20U + json_bytes + 4U, "BIN\0", 4) != 0) return false;
    std::uint32_t bin_bytes = U32Le(glb.data() + 20U + json_bytes);
    if (bin_bytes > fsize - 28U - json_bytes) return false;
    const unsigned char* bin = glb.data() + 28U + json_bytes;
    cJSON* root = cJSON_ParseWithLength(reinterpret_cast<const char*>(glb.data() + 20), json_bytes);
    if (root == nullptr) return false;
    bool ok = false;
    cJSON* meshes = cJSON_GetObjectItemCaseSensitive(root, "meshes");
    cJSON* mesh0 = cJSON_IsArray(meshes) != 0 ? cJSON_GetArrayItem(meshes, 0) : nullptr;
    cJSON* prims = mesh0 != nullptr ? cJSON_GetObjectItemCaseSensitive(mesh0, "primitives") : nullptr;
    cJSON* prim = cJSON_IsArray(prims) != 0 ? cJSON_GetArrayItem(prims, 0) : nullptr;
    cJSON* accessors = cJSON_GetObjectItemCaseSensitive(root, "accessors");
    cJSON* views = cJSON_GetObjectItemCaseSensitive(root, "bufferViews");
    cJSON* pos_acc = nullptr;
    cJSON* idx_acc = nullptr;
    if (prim != nullptr) {
        cJSON* attrs = cJSON_GetObjectItemCaseSensitive(prim, "attributes");
        cJSON* pos_ref = attrs != nullptr ? cJSON_GetObjectItemCaseSensitive(attrs, "POSITION") : nullptr;
        cJSON* idx_ref = cJSON_GetObjectItemCaseSensitive(prim, "indices");
        if (cJSON_IsNumber(pos_ref) != 0 && pos_ref->valueint >= 0)
            pos_acc = cJSON_GetArrayItem(accessors, pos_ref->valueint);
        if (cJSON_IsNumber(idx_ref) != 0 && idx_ref->valueint >= 0)
            idx_acc = cJSON_GetArrayItem(accessors, idx_ref->valueint);
    }
    if (cJSON_IsObject(pos_acc) != 0 && cJSON_IsObject(idx_acc) != 0) {
        cJSON* pos_type = cJSON_GetObjectItemCaseSensitive(pos_acc, "type");
        cJSON* pos_ct = cJSON_GetObjectItemCaseSensitive(pos_acc, "componentType");
        cJSON* pos_count = cJSON_GetObjectItemCaseSensitive(pos_acc, "count");
        cJSON* idx_count = cJSON_GetObjectItemCaseSensitive(idx_acc, "count");
        cJSON* idx_ct = cJSON_GetObjectItemCaseSensitive(idx_acc, "componentType");
        cJSON* pos_bv_ref = cJSON_GetObjectItemCaseSensitive(pos_acc, "bufferView");
        cJSON* idx_bv_ref = cJSON_GetObjectItemCaseSensitive(idx_acc, "bufferView");
        cJSON* pos_bv = (cJSON_IsNumber(pos_bv_ref) != 0 && pos_bv_ref->valueint >= 0)
                            ? cJSON_GetArrayItem(views, pos_bv_ref->valueint)
                            : nullptr;
        cJSON* idx_bv = (cJSON_IsNumber(idx_bv_ref) != 0 && idx_bv_ref->valueint >= 0)
                            ? cJSON_GetArrayItem(views, idx_bv_ref->valueint)
                            : nullptr;
        if (cJSON_IsString(pos_type) != 0 && std::strcmp(pos_type->valuestring, "VEC3") == 0 &&
            cJSON_IsNumber(pos_ct) != 0 && pos_ct->valueint == 5126 &&
            cJSON_IsNumber(pos_count) != 0 && pos_count->valueint > 0 &&
            cJSON_IsNumber(idx_count) != 0 && idx_count->valueint > 0 &&
            cJSON_IsNumber(idx_ct) != 0 &&
            (idx_ct->valueint == 5123 || idx_ct->valueint == 5125) &&
            cJSON_IsObject(pos_bv) != 0 && cJSON_IsObject(idx_bv) != 0) {
            auto u32 = [](cJSON* o, const char* n) -> std::uint32_t {
                cJSON* v = cJSON_GetObjectItemCaseSensitive(o, n);
                if (cJSON_IsNumber(v) == 0 || v->valuedouble < 0.0 ||
                    v->valuedouble > 4294967295.0 ||
                    v->valuedouble != static_cast<double>(v->valueint))
                    return 0;
                return static_cast<std::uint32_t>(v->valueint);
            };
            std::uint32_t pos_off = u32(pos_bv, "byteOffset") + u32(pos_acc, "byteOffset");
            std::uint32_t idx_off = u32(idx_bv, "byteOffset") + u32(idx_acc, "byteOffset");
            cJSON* stride_n = cJSON_GetObjectItemCaseSensitive(pos_bv, "byteStride");
            std::uint32_t stride = 12;
            if (cJSON_IsNumber(stride_n) != 0) {
                if (stride_n->valueint < 12) {
                    cJSON_Delete(root);
                    return false;
                }
                stride = static_cast<std::uint32_t>(stride_n->valueint);
            }
            std::uint32_t pc = static_cast<std::uint32_t>(pos_count->valueint);
            std::uint32_t ic = static_cast<std::uint32_t>(idx_count->valueint);
            std::uint32_t idx_sz = (idx_ct->valueint == 5123) ? 2U : 4U;
            if (pc <= 1000000U && ic <= 4000000U && pos_off <= bin_bytes && idx_off <= bin_bytes &&
                pc <= (bin_bytes - pos_off) / stride &&
                ic <= (bin_bytes - idx_off) / idx_sz) {
                verts.resize(pc);
                idx.resize(ic);
                for (std::uint32_t i = 0; i < pc; ++i) {
                    std::memcpy(verts[i].position, bin + pos_off + i * stride, 12);
                    verts[i].color[0] = verts[i].color[1] = verts[i].color[2] = 1.0f;
                    if (std::isfinite(verts[i].position[0]) == 0 ||
                        std::isfinite(verts[i].position[1]) == 0 ||
                        std::isfinite(verts[i].position[2]) == 0) {
                        cJSON_Delete(root);
                        return false;
                    }
                }
                for (std::uint32_t i = 0; i < ic; ++i) {
                    if (idx_ct->valueint == 5123)
                        idx[i] = static_cast<std::uint32_t>(bin[idx_off + i * 2U] |
                                                            (static_cast<std::uint32_t>(
                                                                 bin[idx_off + i * 2U + 1U])
                                                             << 8U));
                    else
                        idx[i] = U32Le(bin + idx_off + i * 4U);
                    if (idx[i] >= pc) {
                        cJSON_Delete(root);
                        return false;
                    }
                }
                ok = true;
            }
        }
    }
    cJSON_Delete(root);
    if (!ok) {
        err.set("BAD_FORMAT", "El GLB no contiene una malla compatible.");
    }
    return ok;
}

ModelCache* GetModel(Context& ctx, const Project& proj, const std::string& asset_id, Error& err) {
    for (auto& m : ctx.models)
        if (m.asset_id == asset_id) return &m;
    if (ctx.models.size() >= 256) {
        err.set("LIMIT", "Se superó la caché de modelos del renderer.");
        return nullptr;
    }
    std::vector<Vertex> verts;
    std::vector<std::uint32_t> indices;
    Error tmp;
    if (!LoadAssetGeometry(proj, asset_id, verts, indices, tmp)) {
        err.set(tmp.code.empty() ? "BAD_FORMAT" : tmp.code,
                tmp.message.empty() ? "El GLB no contiene una malla compatible." : tmp.message);
        return nullptr;
    }
    std::uint64_t vb = static_cast<std::uint64_t>(verts.size()) * sizeof(Vertex);
    std::uint64_t ib = static_cast<std::uint64_t>(indices.size()) * sizeof(std::uint32_t);
    if (verts.empty() || indices.empty() || vb > 128ULL * 1024ULL * 1024ULL ||
        ib > 128ULL * 1024ULL * 1024ULL) {
        err.set("LIMIT", "La malla del asset supera el límite del renderer.");
        return nullptr;
    }
    ModelCache m;
    m.asset_id = asset_id;
    // Bounds en espacio del modelo para picking proporcional a lo visible.
    for (const auto& v : verts) {
        for (int i = 0; i < 3; ++i) {
            if (v.position[i] < m.bmin[i]) m.bmin[i] = v.position[i];
            if (v.position[i] > m.bmax[i]) m.bmax[i] = v.position[i];
        }
    }
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(vb);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = verts.data();
    if (FAILED(ctx.device->CreateBuffer(&desc, &data, &m.vertices))) {
        err.set("GPU", "No se pudo crear el buffer de vértices del asset '" + asset_id + "'.");
        return nullptr;
    }
    desc.ByteWidth = static_cast<UINT>(ib);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = indices.data();
    if (FAILED(ctx.device->CreateBuffer(&desc, &data, &m.indices))) {
        if (m.vertices != nullptr) m.vertices->Release();
        err.set("GPU", "No se pudo crear el buffer de índices del asset '" + asset_id + "'.");
        return nullptr;
    }
    m.index_count = static_cast<UINT>(indices.size());
    ctx.models.push_back(m);
    return &ctx.models.back();
}

bool CreateDeviceWithFallback(Context& ctx, const DXGI_SWAP_CHAIN_DESC& chain,
                              std::string& backend, Error& err) {
    // 1) dedicada con mayor VRAM
    {
        IDXGIFactory1* factory = nullptr;
        if (SUCCEEDED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                           reinterpret_cast<void**>(&factory)))) {
            IDXGIAdapter1* best = nullptr;
            SIZE_T best_mem = 0;
            for (UINT i = 0;; ++i) {
                IDXGIAdapter1* cand = nullptr;
                if (factory->EnumAdapters1(i, &cand) == DXGI_ERROR_NOT_FOUND) break;
                if (cand == nullptr) continue;
                DXGI_ADAPTER_DESC1 d{};
                if (FAILED(cand->GetDesc1(&d))) {
                    cand->Release();
                    continue;
                }
                if ((d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                    d.DedicatedVideoMemory > best_mem) {
                    if (best != nullptr) best->Release();
                    best = cand;
                    best_mem = d.DedicatedVideoMemory;
                } else {
                    cand->Release();
                }
            }
            if (best != nullptr) {
                DXGI_ADAPTER_DESC1 d{};
                best->GetDesc1(&d);
                char tmp[128] = {};
                ::WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, tmp, 128, nullptr, nullptr);
                HRESULT hr = ::D3D11CreateDevice(reinterpret_cast<IDXGIAdapter*>(best),
                                                 D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                                 D3D11_SDK_VERSION, &ctx.device, nullptr,
                                                 &ctx.context);
                if (SUCCEEDED(hr)) {
                    hr = factory->CreateSwapChain(reinterpret_cast<IUnknown*>(ctx.device),
                                                  const_cast<DXGI_SWAP_CHAIN_DESC*>(&chain),
                                                  &ctx.swap);
                }
                if (SUCCEEDED(hr)) {
                    backend = std::string("hardware-dedicado: ") + tmp;
                    best->Release();
                    factory->Release();
                    return true;
                }
                if (ctx.device != nullptr) {
                    ctx.device->Release();
                    ctx.device = nullptr;
                }
                if (ctx.context != nullptr) {
                    ctx.context->Release();
                    ctx.context = nullptr;
                }
                best->Release();
            }
            factory->Release();
        }
    }
    // 2) hardware por defecto
    {
        DXGI_SWAP_CHAIN_DESC c = chain;
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* cx = nullptr;
        IDXGISwapChain* sw = nullptr;
        HRESULT hr = ::D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &c, &sw,
            &dev, nullptr, &cx);
        if (SUCCEEDED(hr)) {
            ctx.device = dev;
            ctx.context = cx;
            ctx.swap = sw;
            backend = "hardware";
            return true;
        }
    }
    // 3) WARP para VMs/CI/iGPU sin driver
    {
        DXGI_SWAP_CHAIN_DESC c = chain;
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* cx = nullptr;
        IDXGISwapChain* sw = nullptr;
        HRESULT hr = ::D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                                     nullptr, 0, D3D11_SDK_VERSION, &c, &sw, &dev,
                                                     nullptr, &cx);
        if (SUCCEEDED(hr)) {
            ctx.device = dev;
            ctx.context = cx;
            ctx.swap = sw;
            backend = "warp";
            return true;
        }
        err.set("GPU", "No se pudo crear Direct3D 11 (hardware ni WARP).");
        return false;
    }
}

const char* kVsSrc =
    "cbuffer SceneBuffer : register(b0) {"
    "  row_major float4x4 wvp;"
    "  row_major float4x4 world;"
    "  float4 tint;"
    "  float4 light_dir;"
    "  float4 light_col;"
    "  float4 eye_pos;"
    "};"
    "struct I { float3 p:POSITION; float3 c:COLOR; };"
    "struct O { float4 p:SV_POSITION; float3 c:COLOR; float3 wp:TEXCOORD0; };"
    "O VSMain(I i) {"
    "  O o;"
    "  o.p  = mul(float4(i.p,1), wvp);"
    "  o.c  = i.c * tint.rgb;"
    "  o.wp = mul(float4(i.p,1), world).xyz;"
    "  return o;"
    "}";
const char* kPsSrc =
    "cbuffer SceneBuffer : register(b0) {"
    "  row_major float4x4 wvp;"
    "  row_major float4x4 world;"
    "  float4 tint;"
    "  float4 light_dir;"
    "  float4 light_col;"
    "  float4 eye_pos;"
    "};"
    "struct I { float4 p:SV_POSITION; float3 c:COLOR; float3 wp:TEXCOORD0; };"
    "float4 PSMain(I i) : SV_TARGET {"
    "  if (tint.a < 0.5) return float4(i.c, 1);"
    "  float3 N = normalize(cross(ddx(i.wp), ddy(i.wp)));"
    "  float3 L = normalize(-light_dir.xyz);"
    "  float3 V = normalize(eye_pos.xyz - i.wp);"
    "  float3 ambient = 0.15 * i.c;"
    "  float NdotL = saturate(dot(N, L));"
    "  float3 diffuse = i.c * light_col.rgb * NdotL;"
    "  float3 H = normalize(L + V);"
    "  float spec = pow(saturate(dot(N, H)), 32.0);"
    "  float3 specular = light_col.rgb * spec * 0.4;"
    "  return float4(ambient + diffuse + specular, 1);"
    "}";

bool Initialize(Context& ctx, HWND hwnd, Error& err) {
    static const Vertex kCube[8] = {{{-1, -1, -1}, {1, .1f, .1f}}, {{-1, 1, -1}, {.1f, 1, .1f}},
                                    {{1, 1, -1}, {.1f, .3f, 1}},   {{1, -1, -1}, {1, 1, .1f}},
                                    {{-1, -1, 1}, {1, .1f, 1}},    {{-1, 1, 1}, {.1f, 1, 1}},
                                    {{1, 1, 1}, {1, 1, 1}},        {{1, -1, 1}, {.5f, .5f, .5f}}};
    static const unsigned short kIdx[36] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                                            3, 2, 6, 3, 6, 7, 1, 5, 6, 1, 6, 2, 0, 3, 7, 0, 7, 4};
    RECT rc{};
    ::GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;
    DXGI_SWAP_CHAIN_DESC chain{};
    chain.BufferCount = 2;
    chain.BufferDesc.Width = static_cast<UINT>(w);
    chain.BufferDesc.Height = static_cast<UINT>(h);
    chain.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    chain.OutputWindow = hwnd;
    chain.SampleDesc.Count = 1;
    chain.Windowed = TRUE;
    chain.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    std::string backend_local;
    if (!CreateDeviceWithFallback(ctx, chain, backend_local, err)) return false;
    (void)backend_local;
    ID3D11Texture2D* back = nullptr;
    HRESULT hr = ctx.swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    if (SUCCEEDED(hr)) hr = ctx.device->CreateRenderTargetView(back, nullptr, &ctx.target);
    if (back != nullptr) back->Release();
    if (FAILED(hr)) {
        err.set("GPU", "No se pudo crear el render target.");
        return false;
    }
    D3D11_TEXTURE2D_DESC depth{};
    depth.Width = static_cast<UINT>(w);
    depth.Height = static_cast<UINT>(h);
    depth.MipLevels = 1;
    depth.ArraySize = 1;
    depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth.SampleDesc.Count = 1;
    depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = ctx.device->CreateTexture2D(&depth, nullptr, &ctx.depth_tex);
    if (SUCCEEDED(hr))
        hr = ctx.device->CreateDepthStencilView(ctx.depth_tex, nullptr, &ctx.depth);
    if (FAILED(hr)) {
        err.set("GPU", "No se pudo crear el depth buffer.");
        return false;
    }
    ID3DBlob* vsb = nullptr;
    ID3DBlob* psb = nullptr;
    if (!CompileShader(kVsSrc, "VSMain", "vs_4_0", &vsb, err)) return false;
    if (!CompileShader(kPsSrc, "PSMain", "ps_4_0", &psb, err)) {
        if (vsb != nullptr) vsb->Release();
        return false;
    }
    hr = ctx.device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr,
                                        &ctx.vs);
    if (SUCCEEDED(hr))
        hr = ctx.device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr,
                                           &ctx.ps);
    D3D11_INPUT_ELEMENT_DESC elems[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    if (SUCCEEDED(hr))
        hr = ctx.device->CreateInputLayout(elems, 2, vsb->GetBufferPointer(),
                                           vsb->GetBufferSize(), &ctx.layout);
    if (vsb != nullptr) vsb->Release();
    if (psb != nullptr) psb->Release();
    if (FAILED(hr)) {
        err.set("GPU", "No se pudo crear el pipeline.");
        return false;
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(kCube);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = kCube;
    hr = ctx.device->CreateBuffer(&bd, &sd, &ctx.cube_vb);
    if (SUCCEEDED(hr)) {
        bd.ByteWidth = sizeof(kIdx);
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        sd.pSysMem = kIdx;
        hr = ctx.device->CreateBuffer(&bd, &sd, &ctx.cube_ib);
    }
    if (SUCCEEDED(hr)) {
        bd.ByteWidth = sizeof(SceneBuffer);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = ctx.device->CreateBuffer(&bd, nullptr, &ctx.matrices);
    }
    if (SUCCEEDED(hr)) {
        // Crosshair "+": 4 segmentos (8 vértices), dinámico, se rellena cada frame.
        bd.ByteWidth = static_cast<UINT>(sizeof(Vertex) * 8U);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = ctx.device->CreateBuffer(&bd, nullptr, &ctx.cross_vb);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.CPUAccessFlags = 0;
    }
    if (SUCCEEDED(hr)) {
        // Gizmo: 3 flechas x 3 segmentos + línea de eje = 20 vértices máx.
        bd.ByteWidth = static_cast<UINT>(sizeof(Vertex) * 20U);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = ctx.device->CreateBuffer(&bd, nullptr, &ctx.gizmo_vb);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.CPUAccessFlags = 0;
    }
    if (SUCCEEDED(hr)) {
        // Widget esquina: fondo (6) + borde (8) + 3 ejes x 2 mitades (12) = 26 máx.
        bd.ByteWidth = static_cast<UINT>(sizeof(Vertex) * 26U);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = ctx.device->CreateBuffer(&bd, nullptr, &ctx.corner_vb);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.CPUAccessFlags = 0;
    }
    if (SUCCEEDED(hr)) {
        // Sin depth para el overlay 2D (se restaura a nullptr tras dibujarlo).
        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = FALSE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_LESS;
        ds.StencilEnable = FALSE;
        hr = ctx.device->CreateDepthStencilState(&ds, &ctx.no_depth);
    }
    if (FAILED(hr)) {
        err.set("GPU", "No se pudo crear los buffers.");
        return false;
    }
    ctx.buf_w = w;
    ctx.buf_h = h;
    return true;
}

void UpdateCamera(Scene& scene, Context& ctx, double dt) {
    if (!ctx.has_focus) {
        ctx.pending_yaw_px = 0.0f;
        ctx.pending_pitch_px = 0.0f;
        return;
    }
    if (!(dt > 0.0) || std::isfinite(dt) == 0) return;
    double clamped = dt > 0.1 ? 0.1 : dt;
    Transform* cam = nullptr;
    for (auto& e : scene.entities) {
        if (e.has_camera) {
            cam = &e.transform;
            break;
        }
    }
    if (cam == nullptr) {
        ctx.pending_yaw_px = 0.0f;
        ctx.pending_pitch_px = 0.0f;
        return;
    }
    // Validar selección (puede haber quedado huérfana) y resolver puntero.
    Entity* sel = nullptr;
    if (!ctx.selected.empty()) {
        for (auto& e : scene.entities) {
            if (e.id == ctx.selected) {
                sel = &e;
                break;
            }
        }
        if (sel == nullptr) {
            ctx.selected.clear();
            ctx.dragging = false;
        }
    }
    // Mouse-look: píxeles acumulados desde PollPointerLock o desde el arrastre.
    // Arrastrando una selección, los deltas mueven la entidad en vez de la cámara.
    if (std::isfinite(ctx.pending_yaw_px) == 0) ctx.pending_yaw_px = 0.0f;
    if (std::isfinite(ctx.pending_pitch_px) == 0) ctx.pending_pitch_px = 0.0f;
    float npx = 0, npy = 0, npz = 0;
    bool have_drag = false;  // nueva posición válida para la selección
    if (ctx.dragging && sel != nullptr && sel->has_mesh &&
        (ctx.pending_yaw_px != 0.0f || ctx.pending_pitch_px != 0.0f)) {
        float dx = ctx.pending_yaw_px, dy = ctx.pending_pitch_px;
        float ex = sel->transform.position.x - cam->position.x;
        float ey = sel->transform.position.y - cam->position.y;
        float ez = sel->transform.position.z - cam->position.z;
        float dist = std::sqrt(ex * ex + ey * ey + ez * ez);
        if (!(dist > 0.5f) || std::isfinite(dist) == 0) dist = 0.5f;
        if (ctx.locked_axis == 'X' || ctx.locked_axis == 'Y' || ctx.locked_axis == 'Z') {
            // Eje bloqueado: proyectar el eje mundo a pantalla y mover sobre él.
            float fov = 60.0f;
            for (const auto& e : scene.entities) {
                if (e.has_camera) {
                    fov = e.camera.fov_degrees;
                    break;
                }
            }
            float view[16], proj_m[16];
            float P[3] = {sel->transform.position.x, sel->transform.position.y,
                          sel->transform.position.z};
            float Q[3] = {P[0], P[1], P[2]};
            if (ctx.locked_axis == 'X') Q[0] += 1.0f;
            if (ctx.locked_axis == 'Y') Q[1] += 1.0f;
            if (ctx.locked_axis == 'Z') Q[2] += 1.0f;
            float s0x = 0, s0y = 0, s1x = 0, s1y = 0;
            if (ctx.view_w > 0 && ctx.view_h > 0 && ViewMatrix(view, *cam) &&
                ProjectionMatrix(proj_m, static_cast<float>(ctx.view_w) /
                                             static_cast<float>(ctx.view_h),
                                 fov) &&
                ProjectToScreen(P, view, proj_m, ctx.view_w, ctx.view_h, s0x, s0y) &&
                ProjectToScreen(Q, view, proj_m, ctx.view_w, ctx.view_h, s1x, s1y)) {
                float dirx = s1x - s0x, diry = s1y - s0y;
                float len = std::sqrt(dirx * dirx + diry * diry);
                if (len > 1e-3f && std::isfinite(len) != 0) {
                    dirx /= len;
                    diry /= len;
                } else {
                    dirx = 1.0f;
                    diry = 0.0f;
                }
                float wpp = 2.0f * dist *
                            std::tan(fov * 0.01745329252f * 0.5f) /
                            static_cast<float>(ctx.view_h);  // mundo por píxel
                float amount = (dx * dirx + dy * diry) * wpp;
                float nx = P[0], ny = P[1], nz = P[2];
                if (ctx.locked_axis == 'X') nx += amount;
                if (ctx.locked_axis == 'Y') ny += amount;
                if (ctx.locked_axis == 'Z') nz += amount;
                if (std::isfinite(nx) != 0 && std::isfinite(ny) != 0 &&
                    std::isfinite(nz) != 0 && std::fabs(nx) <= 1000000.0f &&
                    std::fabs(ny) <= 1000000.0f && std::fabs(nz) <= 1000000.0f) {
                    npx = nx;
                    npy = ny;
                    npz = nz;
                    have_drag = true;
                }
            }
        } else {
            float s = dist * 0.0025f;  // el agarre conserva proporción con la distancia
            float rx = std::cos(cam->rotation.y), rz = -std::sin(cam->rotation.y);
            // Solo asignar valores finitos en rango guardable (si no, la escena no recargaría).
            float nx = sel->transform.position.x + rx * dx * s;
            float ny = sel->transform.position.y + -dy * s;  // arrastrar arriba (dy<0) = subir
            float nz = sel->transform.position.z + rz * dx * s;
            if (std::isfinite(nx) != 0 && std::isfinite(ny) != 0 && std::isfinite(nz) != 0 &&
                std::fabs(nx) <= 1000000.0f && std::fabs(ny) <= 1000000.0f &&
                std::fabs(nz) <= 1000000.0f) {
                npx = nx;
                npy = ny;
                npz = nz;
                have_drag = true;
            }
        }
    } else if (ctx.mouse_look) {
        constexpr float kMouseSens = 0.004f;  // rad/píxel
        constexpr float kMaxPitch = 1.55f;  // ~88.8°, evita la singularidad de ViewMatrix en ±90°
        cam->rotation.y += ctx.pending_yaw_px * kMouseSens;
        cam->rotation.x += -ctx.pending_pitch_px * kMouseSens;  // subir el mouse (dy<0) = mirar arriba
        if (cam->rotation.x > kMaxPitch) cam->rotation.x = kMaxPitch;
        if (cam->rotation.x < -kMaxPitch) cam->rotation.x = -kMaxPitch;
    }
    if (have_drag && sel != nullptr) {
        sel->transform.position = {npx, npy, npz};
        // La cámara y todas las matrices usan radianes. atan2 ya devuelve
        // radianes; convertirlo a grados aquí hacía que, tras arrastrar una
        // selección, la siguiente matriz de vista apuntase a otra dirección.
        float dx = sel->transform.position.x - cam->position.x;
        float dz = sel->transform.position.z - cam->position.z;
        cam->rotation.y = std::atan2(dx, dz);
        float horiz = std::sqrt(dx * dx + dz * dz);
        float dy = sel->transform.position.y - cam->position.y;
        cam->rotation.x = std::atan2(dy, horiz);
        constexpr float kMaxPitch = 1.55f;  // ~88.8°, igual que mouse-look.
        if (cam->rotation.x > kMaxPitch) cam->rotation.x = kMaxPitch;
        if (cam->rotation.x < -kMaxPitch) cam->rotation.x = -kMaxPitch;
        // Ya no movemos cam->position; solo el ángulo.
    }
    ctx.pending_yaw_px = 0.0f;
    ctx.pending_pitch_px = 0.0f;
    float speed = (ctx.fast ? 12.0f : 4.0f) * static_cast<float>(clamped);
    float fx = std::sin(cam->rotation.y), fz = std::cos(cam->rotation.y);
    float rx = std::cos(cam->rotation.y), rz = -std::sin(cam->rotation.y);
    if (ctx.fwd) {
        cam->position.x += fx * speed;
        cam->position.z += fz * speed;
    }
    if (ctx.back) {
        cam->position.x -= fx * speed;
        cam->position.z -= fz * speed;
    }
    if (ctx.left) {
        cam->position.x -= rx * speed;
        cam->position.z -= rz * speed;
    }
    if (ctx.right) {
        cam->position.x += rx * speed;
        cam->position.z += rz * speed;
    }
    if (ctx.down) cam->position.y -= speed;
    if (ctx.up) cam->position.y += speed;
}

// Si el área cliente cambió (maximizar, restaurar, redimensionar, DPI), recrea
// RTV + depth al tamaño real. Sin esto el render queda en una sub-región o se
// corrompe al cambiar el tamaño de la ventana.
bool EnsureBackbufferSize(Context& ctx, int w, int h, Error& err) {
    if (w == ctx.buf_w && h == ctx.buf_h) return true;
    if (ctx.target != nullptr) {
        ctx.target->Release();
        ctx.target = nullptr;
    }
    if (ctx.depth != nullptr) {
        ctx.depth->Release();
        ctx.depth = nullptr;
    }
    if (ctx.depth_tex != nullptr) {
        ctx.depth_tex->Release();
        ctx.depth_tex = nullptr;
    }
    ctx.context->OMSetRenderTargets(0, nullptr, nullptr);
    HRESULT hr = ctx.swap->ResizeBuffers(2, static_cast<UINT>(w), static_cast<UINT>(h),
                                         DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(hr)) {
        err.set("GPU", "No se pudo redimensionar el swapchain.");
        return false;
    }
    ID3D11Texture2D* back = nullptr;
    hr = ctx.swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    if (SUCCEEDED(hr)) hr = ctx.device->CreateRenderTargetView(back, nullptr, &ctx.target);
    if (back != nullptr) back->Release();
    if (FAILED(hr)) {
        err.set("GPU", "No se pudo recrear el render target.");
        return false;
    }
    D3D11_TEXTURE2D_DESC depth{};
    depth.Width = static_cast<UINT>(w);
    depth.Height = static_cast<UINT>(h);
    depth.MipLevels = 1;
    depth.ArraySize = 1;
    depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth.SampleDesc.Count = 1;
    depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = ctx.device->CreateTexture2D(&depth, nullptr, &ctx.depth_tex);
    if (SUCCEEDED(hr))
        hr = ctx.device->CreateDepthStencilView(ctx.depth_tex, nullptr, &ctx.depth);
    if (FAILED(hr)) {
        err.set("GPU", "No se pudo recrear el depth buffer.");
        return false;
    }
    ctx.buf_w = w;
    ctx.buf_h = h;
    return true;
}

// Dibuja líneas con contorno negro (pasada desplazada 1.5px + pasada en color) para
// que se vean sobre modelos brillantes. wvp_or_null: matriz mundo→clip, o nullptr
// para geometría ya en NDC (crosshair, widget). Deja depth restaurado a nullptr.
void DrawLinesOutlined(Context& ctx, ID3D11Buffer* vb, UINT count, int w, int h,
                       const float* wvp_or_null) {
    SceneBuffer sb{};
    UINT stride = sizeof(Vertex), off = 0;
    ctx.context->OMSetDepthStencilState(ctx.no_depth, 0);
    ctx.context->IASetVertexBuffers(0, 1, &vb, &stride, &off);
    ctx.context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    float fw = static_cast<float>(w > 0 ? w : 1);
    float fh = static_cast<float>(h > 0 ? h : 1);
    if (wvp_or_null != nullptr) {
        std::memcpy(sb.wvp, wvp_or_null, sizeof(sb.wvp));
    } else {
        MatIdentity(sb.wvp);
    }
    sb.wvp[12] += 3.0f / fw;
    sb.wvp[13] += -3.0f / fh;
    sb.tint[0] = sb.tint[1] = sb.tint[2] = 0.0f;
    sb.tint[3] = 0.0f;  // unlit: pass-through para overlays
    ctx.context->UpdateSubresource(ctx.matrices, 0, nullptr, &sb, 0, 0);
    ctx.context->Draw(count, 0);
    if (wvp_or_null != nullptr) {
        std::memcpy(sb.wvp, wvp_or_null, sizeof(sb.wvp));
    } else {
        MatIdentity(sb.wvp);
    }
    sb.tint[0] = sb.tint[1] = sb.tint[2] = 1.0f;
    sb.tint[3] = 0.0f;  // unlit: pass-through para overlays
    ctx.context->UpdateSubresource(ctx.matrices, 0, nullptr, &sb, 0, 0);
    ctx.context->Draw(count, 0);
    ctx.context->OMSetDepthStencilState(nullptr, 0);
}

void DrawCrosshair(Context& ctx, const Project& proj, const Scene& scene, const Transform& cam_t,
                   float aspect, int w, int h) {
    if (ctx.cross_vb == nullptr || ctx.no_depth == nullptr) return;
    // Estado bajo el punto: blanco = nada, verde = objetivo, amarillo = selección.
    float ht = 0.0f;
    const Entity* hover = PickEntity(ctx, proj, scene, CenterRay(cam_t), ht);
    float cr = 1.0f, cg = 1.0f, cb = 1.0f;
    if (hover != nullptr) {
        if (!ctx.selected.empty() && hover->id == ctx.selected) {
            cr = 1.0f;
            cg = 1.0f;
            cb = 0.2f;
        } else {
            cr = 0.2f;
            cg = 1.0f;
            cb = 0.2f;
        }
    }
    float ax = 0.035f / aspect, gap = 0.012f / aspect;  // brazos corregidos por aspecto
    const float ay = 0.035f, gy = 0.012f;
    Vertex v[8] = {{{-ax, 0, 0}, {cr, cg, cb}}, {{-gap, 0, 0}, {cr, cg, cb}},
                   {{gap, 0, 0}, {cr, cg, cb}},  {{ax, 0, 0}, {cr, cg, cb}},
                   {{0, -ay, 0}, {cr, cg, cb}},  {{0, -gy, 0}, {cr, cg, cb}},
                   {{0, gy, 0}, {cr, cg, cb}},   {{0, ay, 0}, {cr, cg, cb}}};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx.context->Map(ctx.cross_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    std::memcpy(mapped.pData, v, sizeof(v));
    ctx.context->Unmap(ctx.cross_vb, 0);
    DrawLinesOutlined(ctx, ctx.cross_vb, 8, w, h, nullptr);
}

// Gizmo de mover sobre la selección (ejes mundo RGB) + línea roja fina del eje
// bloqueado. Geometría en espacio mundo: se dibuja con vista·proyección real
// (con identidad salía despegada del objeto y al contrario que el arrastre).
void DrawGizmo(Context& ctx, const Scene& scene, const Transform& cam_t, const float view_proj[16],
               int w, int h) {
    if (ctx.gizmo_vb == nullptr || ctx.no_depth == nullptr) return;
    const Entity* sel = nullptr;
    if (!ctx.selected.empty()) {
        for (const auto& e : scene.entities) {
            if (e.id == ctx.selected) {
                sel = &e;
                break;
            }
        }
    }
    if (sel == nullptr) return;
    float ex = sel->transform.position.x - cam_t.position.x;
    float ey = sel->transform.position.y - cam_t.position.y;
    float ez = sel->transform.position.z - cam_t.position.z;
    float dist = std::sqrt(ex * ex + ey * ey + ez * ez);
    if (!(dist > 0.5f) || std::isfinite(dist) == 0) dist = 0.5f;
    float len = dist * 0.22f;  // tamaño constante en pantalla
    if (len < 0.35f) len = 0.35f;
    float fx = std::sin(cam_t.rotation.y) * std::cos(cam_t.rotation.x);
    float fy = std::sin(cam_t.rotation.x);
    float fz = std::cos(cam_t.rotation.y) * std::cos(cam_t.rotation.x);
    const float px = sel->transform.position.x;
    const float py = sel->transform.position.y;
    const float pz = sel->transform.position.z;
    Vertex v[20];
    int n = 0;
    auto seg = [&](float ax, float ay, float az, float bx, float by, float bz, float r, float g,
                   float b) {
        v[n].position[0] = ax;
        v[n].position[1] = ay;
        v[n].position[2] = az;
        v[n].color[0] = r;
        v[n].color[1] = g;
        v[n].color[2] = b;
        ++n;
        v[n].position[0] = bx;
        v[n].position[1] = by;
        v[n].position[2] = bz;
        v[n].color[0] = r;
        v[n].color[1] = g;
        v[n].color[2] = b;
        ++n;
    };
    const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const float cols[3][3] = {{1, 0.15f, 0.15f}, {0.25f, 1, 0.25f}, {0.3f, 0.55f, 1}};
    for (int i = 0; i < 3; ++i) {
        const float* d = dirs[i];
        float tx = px + d[0] * len, ty = py + d[1] * len, tz = pz + d[2] * len;
        seg(px, py, pz, tx, ty, tz, cols[i][0], cols[i][1], cols[i][2]);
        // Cabeza de flecha: dos segmentos hacia atrás con apertura lateral.
        float cx = d[1] * fz - d[2] * fy, cy = d[2] * fx - d[0] * fz, cz = d[0] * fy - d[1] * fx;
        float cl = std::sqrt(cx * cx + cy * cy + cz * cz);
        float dot = d[0] * fx + d[1] * fy + d[2] * fz;
        if (!(cl > 1e-3f) || std::fabs(dot) > 0.98f || std::isfinite(cl) == 0) {
            // Eje casi paralelo a la vista: perpendicular arbitraria estable.
            if (std::fabs(d[1]) < 0.9f) {
                cx = -d[2];
                cy = 0.0f;
                cz = d[0];
            } else {
                cx = 1.0f;
                cy = 0.0f;
                cz = 0.0f;
            }
            cl = std::sqrt(cx * cx + cy * cy + cz * cz);
        }
        cx /= cl;
        cy /= cl;
        cz /= cl;
        float hl = len * 0.28f, hw = len * 0.12f;
        float bx = tx - d[0] * hl, by = ty - d[1] * hl, bz = tz - d[2] * hl;
        seg(tx, ty, tz, bx + cx * hw, by + cy * hw, bz + cz * hw, cols[i][0], cols[i][1],
            cols[i][2]);
        seg(tx, ty, tz, bx - cx * hw, by - cy * hw, bz - cz * hw, cols[i][0], cols[i][1],
            cols[i][2]);
    }
    if (ctx.locked_axis == 'X' || ctx.locked_axis == 'Y' || ctx.locked_axis == 'Z') {
        // Línea roja muy fina a lo largo del eje bloqueado (ambas direcciones).
        float ax = 0, ay = 0, az = 0;
        if (ctx.locked_axis == 'X') ax = 1.0f;
        if (ctx.locked_axis == 'Y') ay = 1.0f;
        if (ctx.locked_axis == 'Z') az = 1.0f;
        float L = len * 8.0f;
        seg(px - ax * L, py - ay * L, pz - az * L, px + ax * L, py + ay * L, pz + az * L, 1.0f,
            0.1f, 0.1f);
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx.context->Map(ctx.gizmo_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    std::memcpy(mapped.pData, v, static_cast<std::size_t>(n) * sizeof(Vertex));
    ctx.context->Unmap(ctx.gizmo_vb, 0);
    DrawLinesOutlined(ctx, ctx.gizmo_vb, static_cast<UINT>(n), w, h, view_proj);
}

// Widget de orientación en la esquina superior derecha: cajita oscura con los 3
// ejes mundo (RGB) rotados con la cámara (solo orientación, sin traslación).
// La mitad positiva va brillante y la negativa atenuada.
void DrawCornerGizmo(Context& ctx, const Transform& cam_t, int w, int h) {
    if (ctx.corner_vb == nullptr || ctx.no_depth == nullptr || w <= 0 || h <= 0) return;
    const int S = 110, M = 12;
    if (w < S + 2 * M + 40 || h < S + 2 * M + 40) return;  // ventana demasiado pequeña
    float f[3], r[3], u[3];
    CameraBasis(cam_t, f, r, u);
    auto ndc = [&](float px, float py, float& nx, float& ny) {
        nx = (px / static_cast<float>(w)) * 2.0f - 1.0f;
        ny = 1.0f - (py / static_cast<float>(h)) * 2.0f;
    };
    Vertex v[26];
    int n = 0;
    // Fondo visible: gris azulado claro (el azul fondo lo camuflaba) + borde claro.
    auto tri = [&](float ax, float ay, float bx, float by, float cx, float cy) {
        float nx, ny;
        ndc(ax, ay, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].color[0] = 0.10f;
        v[n].color[1] = 0.12f;
        v[n].color[2] = 0.17f;
        ++n;
        ndc(bx, by, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].color[0] = 0.10f;
        v[n].color[1] = 0.12f;
        v[n].color[2] = 0.17f;
        ++n;
        ndc(cx, cy, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].color[0] = 0.10f;
        v[n].color[1] = 0.12f;
        v[n].color[2] = 0.17f;
        ++n;
    };
    auto edge = [&](float ax, float ay, float bx, float by) {
        float nx, ny;
        ndc(ax, ay, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].color[0] = 0.55f;
        v[n].color[1] = 0.60f;
        v[n].color[2] = 0.68f;
        ++n;
        ndc(bx, by, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].color[0] = 0.55f;
        v[n].color[1] = 0.60f;
        v[n].color[2] = 0.68f;
        ++n;
    };
    float x0 = static_cast<float>(w - M - S), y0 = static_cast<float>(M);
    float x1 = static_cast<float>(w - M), y1 = static_cast<float>(M + S);
    tri(x0, y0, x1, y0, x0, y1);
    tri(x1, y0, x1, y1, x0, y1);
    // Fondo: 2 triángulos opacos (sin blending en el pipeline).
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx.context->Map(ctx.corner_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;
        std::memcpy(mapped.pData, v, static_cast<std::size_t>(n) * sizeof(Vertex));
        ctx.context->Unmap(ctx.corner_vb, 0);
        SceneBuffer sb{};
        MatIdentity(sb.wvp);
        sb.tint[0] = sb.tint[1] = sb.tint[2] = 1.0f;
        sb.tint[3] = 0.0f;  // unlit: pass-through para overlays
        UINT stride = sizeof(Vertex), off = 0;
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = x0;
        vp.TopLeftY = y0;
        vp.Width = static_cast<FLOAT>(S);
        vp.Height = static_cast<FLOAT>(S);
        vp.MaxDepth = 1.0f;
        ctx.context->OMSetDepthStencilState(ctx.no_depth, 0);
        ctx.context->RSSetViewports(1, &vp);
        ctx.context->IASetVertexBuffers(0, 1, &ctx.corner_vb, &stride, &off);
        ctx.context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        ctx.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx.context->UpdateSubresource(ctx.matrices, 0, nullptr, &sb, 0, 0);
        ctx.context->Draw(static_cast<UINT>(n), 0);
    }
    // Ejes: del centro, mitad positiva brillante y negativa atenuada.
    // (Antes va el borde del marco para que la cajita se distinga del fondo.)
    n = 0;
    edge(x0, y0, x1, y0);
    edge(x1, y0, x1, y1);
    edge(x1, y1, x0, y1);
    edge(x0, y1, x0, y0);
    auto axis = [&](float ax, float ay, float az, float cr, float cg, float cb) {
        float cx = ax * r[0] + ay * r[1] + az * r[2];
        float cy = ax * u[0] + ay * u[1] + az * u[2];
        float cl = std::sqrt(cx * cx + cy * cy);
        if (!(cl > 1e-4f) || std::isfinite(cl) == 0) return;  // eje de canto: se oculta
        float R = static_cast<float>(S) * 0.5f - 16.0f;
        float ex = (x0 + x1) * 0.5f + (cx / cl) * R;
        float ey = (y0 + y1) * 0.5f - (cy / cl) * R;
        float nx, ny;
        ndc((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].color[0] = cr * 0.35f;
        v[n].color[1] = cg * 0.35f;
        v[n].color[2] = cb * 0.35f;
        ++n;
        ndc(ex, ey, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].color[0] = cr;
        v[n].color[1] = cg;
        v[n].color[2] = cb;
        ++n;
    };
    axis(1, 0, 0, 1, 0.15f, 0.15f);
    axis(0, 1, 0, 0.25f, 1, 0.25f);
    axis(0, 0, 1, 0.3f, 0.55f, 1);
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx.context->Map(ctx.corner_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;
        std::memcpy(mapped.pData, v, static_cast<std::size_t>(n) * sizeof(Vertex));
        ctx.context->Unmap(ctx.corner_vb, 0);
    }
    // Los ejes van en NDC de pantalla completa: restaurar viewport antes de dibujar.
    {
        D3D11_VIEWPORT full{};
        full.Width = static_cast<FLOAT>(w);
        full.Height = static_cast<FLOAT>(h);
        full.MaxDepth = 1.0f;
        ctx.context->RSSetViewports(1, &full);
    }
    DrawLinesOutlined(ctx, ctx.corner_vb, static_cast<UINT>(n), w, h, nullptr);
}

HRESULT DrawFrame(Context& ctx, const Project& proj, const Scene& scene, int width, int height,
                  bool vsync) {
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<FLOAT>(width);
    vp.Height = static_cast<FLOAT>(height);
    vp.MaxDepth = 1.0f;
    const float clear[4] = {.02f, .05f, .09f, 1.0f};
    Transform fallback{};
    fallback.position = {0.0f, 3.0f, -10.0f};
    const Transform* cam_t = &fallback;
    float fov = 60.0f;
    float light_c[3] = {1, 1, 1};
    float light_i = 1.0f;
    for (const auto& e : scene.entities) {
        if (e.has_camera) {
            cam_t = &e.transform;
            fov = e.camera.fov_degrees;
            break;
        }
    }
    for (const auto& e : scene.entities) {
        if (e.has_light) {
            light_c[0] = e.light.color.x;
            light_c[1] = e.light.color.y;
            light_c[2] = e.light.color.z;
            light_i = e.light.intensity;
            break;
        }
    }
    if (!(fov >= 1.0f && fov <= 179.0f)) fov = 60.0f;
    if (std::isfinite(light_i) == 0 || light_i < 0.0f) light_i = 1.0f;
    ctx.context->OMSetRenderTargets(1, &ctx.target, ctx.depth);
    ctx.context->RSSetViewports(1, &vp);
    ctx.context->ClearRenderTargetView(ctx.target, clear);
    ctx.context->ClearDepthStencilView(ctx.depth, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx.context->IASetInputLayout(ctx.layout);
    UINT stride = sizeof(Vertex), off = 0;
    ctx.context->IASetVertexBuffers(0, 1, &ctx.cube_vb, &stride, &off);
    ctx.context->IASetIndexBuffer(ctx.cube_ib, DXGI_FORMAT_R16_UINT, 0);
    ctx.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx.context->VSSetShader(ctx.vs, nullptr, 0);
    ctx.context->PSSetShader(ctx.ps, nullptr, 0);
    ctx.context->VSSetConstantBuffers(0, 1, &ctx.matrices);
    ctx.context->PSSetConstantBuffers(0, 1, &ctx.matrices);
    float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 4.0f / 3.0f;
    float view[16], proj_m[16];
    bool has_view = ViewMatrix(view, *cam_t);
    bool has_proj = ProjectionMatrix(proj_m, aspect, fov);
    if (!has_view || !has_proj) {
        return ctx.swap->Present(vsync ? 1 : 0, 0);
    }
    for (const auto& e : scene.entities) {
        if (!e.has_mesh) continue;
        float world[16], wv[16];
        SceneBuffer sb{};
        ModelCache* model = nullptr;
        UINT count = 36;
        if (e.mesh.primitive == "asset") {
            Error tmp;
            model = GetModel(ctx, proj, e.mesh.asset_id, tmp);
            if (model == nullptr) {
                // Diagnóstico por stderr (stdout sigue siendo JSON al final en main.cpp).
                // Solo una vez por asset_id para no inundar a 60 FPS.
                bool warned = false;
                for (const auto& w : ctx.model_warned) {
                    if (w == e.mesh.asset_id) {
                        warned = true;
                        break;
                    }
                }
                if (!warned) {
                    ctx.model_warned.push_back(e.mesh.asset_id);
                    std::fprintf(stderr, "Aviso: no se pudo cargar la geometría del asset '%s' (%s), se omite su dibujo.\n",
                                 e.mesh.asset_id.c_str(),
                                 tmp.message.empty() ? "malla incompatible" : tmp.message.c_str());
                }
                continue;
            }
            ctx.context->IASetVertexBuffers(0, 1, &model->vertices, &stride, &off);
            ctx.context->IASetIndexBuffer(model->indices, DXGI_FORMAT_R32_UINT, 0);
            count = model->index_count;
        }
        WorldMatrix(world, e.transform);
        MatMul(wv, world, view);
        MatMul(sb.wvp, wv, proj_m);
        std::memcpy(sb.world, world, sizeof(world));
        float r = e.mesh.color.x, g = e.mesh.color.y, b = e.mesh.color.z;
        if (std::isfinite(r) == 0) r = 1.0f;
        if (std::isfinite(g) == 0) g = 1.0f;
        if (std::isfinite(b) == 0) b = 1.0f;
        sb.tint[0] = r;
        sb.tint[1] = g;
        sb.tint[2] = b;
        sb.tint[3] = 1.0f;  // 1.0 = iluminación Blinn-Phong activa
        if (!ctx.selected.empty() && e.id == ctx.selected) {
            sb.tint[0] = sb.tint[0] * 0.45f + 0.55f;
            sb.tint[1] = sb.tint[1] * 0.45f + 0.55f;
            sb.tint[2] = sb.tint[2] * 0.45f + 0.55f;
        }
        // Dirección de luz: sol desde arriba-derecha-adelante (normalizada)
        sb.light_dir[0] = 0.4f;
        sb.light_dir[1] = -0.8f;
        sb.light_dir[2] = 0.4f;
        sb.light_dir[3] = 0.0f;
        // Color de luz × intensidad (del componente Light de la escena)
        sb.light_col[0] = light_c[0] * light_i;
        sb.light_col[1] = light_c[1] * light_i;
        sb.light_col[2] = light_c[2] * light_i;
        sb.light_col[3] = 0.0f;
        // Posición de la cámara (para specular)
        sb.eye_pos[0] = cam_t->position.x;
        sb.eye_pos[1] = cam_t->position.y;
        sb.eye_pos[2] = cam_t->position.z;
        sb.eye_pos[3] = 0.0f;
        ctx.context->UpdateSubresource(ctx.matrices, 0, nullptr, &sb, 0, 0);
        ctx.context->DrawIndexed(count, 0, 0);
        if (model != nullptr) {
            ctx.context->IASetVertexBuffers(0, 1, &ctx.cube_vb, &stride, &off);
            ctx.context->IASetIndexBuffer(ctx.cube_ib, DXGI_FORMAT_R16_UINT, 0);
        }
    }
    DrawCrosshair(ctx, proj, scene, *cam_t, aspect, width, height);
    float view_proj[16];
    MatMul(view_proj, view, proj_m);
    DrawGizmo(ctx, scene, *cam_t, view_proj, width, height);
    DrawCornerGizmo(ctx, *cam_t, width, height);
    return ctx.swap->Present(vsync ? 1 : 0, 0);
}

// --- Canal stop IA -> ventana (solo Windows) ---
// Evento nombrado manual-reset por proyecto. "Global\\" lo hace visible entre
// sesiones (automatización vs escritorio del usuario); requiere privilegio de
// creación global (admin). Sin él se usa "Local\\" (solo misma sesión).
// La raíz se canoniza (absoluta+normalizada+minúsculas) para que ".\DemoGame" y
// "C:\...\DemoGame" calculen el mismo nombre en ambos extremos.
std::string StopCanonicalRootKey(const std::filesystem::path& root) {
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(root, ec);
    if (ec) abs = root;
    abs = abs.lexically_normal();
    std::string s = abs.generic_string();
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::uint64_t StopFnv1a64(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string StopEventNameInternal(const std::filesystem::path& root, bool save) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(StopFnv1a64(StopCanonicalRootKey(root))));
    std::string base = std::string("Global\\OasisEngineStop") + (save ? "Save_" : "_") + hex;
    return base;
}

HANDLE StopEventCreate(const std::string& name) {
    HANDLE h = ::CreateEventA(nullptr, TRUE, FALSE, name.c_str());
    if (h == nullptr && name.rfind("Global\\", 0) == 0) {
        std::string local = "Local\\" + name.substr(7);
        h = ::CreateEventA(nullptr, TRUE, FALSE, local.c_str());
    }
    if (h != nullptr) ::ResetEvent(h);  // limpiar señal rancia de un stop anterior
    return h;
}

bool StopEventPoll(HANDLE h) {
    if (h == nullptr) return false;
    return ::WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
}

#endif  // _WIN32

}  // namespace

int RendererRun(const Project& proj, Runtime& rt, const RenderConfig& cfg,
                std::string& out_backend, Error& err) {
    err.clear();
    out_backend.clear();
    if (!rt.initialized || rt.scene == nullptr) {
        err.set("INVALID_ARG", "Runtime no inicializado.");
        return 1;
    }
    const bool borderless = (cfg.mode != WindowMode::Windowed);
    if (!borderless &&
        (cfg.width < 320 || cfg.width > 4096 || cfg.height < 240 || cfg.height > 2160)) {
        err.set("INVALID_ARG", "Tamaño de ventana inválido.");
        return 1;
    }
#ifdef _WIN32
    Context ctx;
    WNDCLASSA wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = ::GetModuleHandleA(nullptr);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "OasisD3D11v2";
    if (::RegisterClassA(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        err.set("GPU", "No se pudo registrar la ventana.");
        return 1;
    }
    HWND hwnd = nullptr;
    bool maximized_show = false;
    if (cfg.mode == WindowMode::Windowed) {
        RECT rc{0, 0, cfg.width, cfg.height};
        ::AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
        hwnd = ::CreateWindowExA(0, wc.lpszClassName, "Oasis Engine — Direct3D 11",
                                 WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE, CW_USEDEFAULT,
                                 CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr,
                                 nullptr, wc.hInstance, &ctx);
    } else if (cfg.mode == WindowMode::Fullscreen) {
        // Completa CON botones -/□/X: ventana con caption y TOPMOST al tamaño del
        // monitor (tapa la barra de tareas pero conserva la barra de título).
        HMONITOR mon =
            ::MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (::GetMonitorInfoA(mon, &mi) == 0) {
            err.set("GPU", "No se pudo obtener la resolución de pantalla.");
            return 1;
        }
        int sw = mi.rcMonitor.right - mi.rcMonitor.left;
        int sh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        if (sw < 320 || sh < 240) {
            err.set("GPU", "No se pudo obtener la resolución de pantalla.");
            return 1;
        }
        hwnd = ::CreateWindowExA(WS_EX_TOPMOST, wc.lpszClassName, "Oasis Engine — Direct3D 11",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE, mi.rcMonitor.left,
                                 mi.rcMonitor.top, sw, sh, nullptr, nullptr, wc.hInstance, &ctx);
    } else {
        // Barra: ventana con botones maximizada (respeta la barra de tareas).
        HMONITOR mon =
            ::MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (::GetMonitorInfoA(mon, &mi) == 0) {
            err.set("GPU", "No se pudo obtener la resolución de pantalla.");
            return 1;
        }
        int sw = mi.rcWork.right - mi.rcWork.left;
        int sh = mi.rcWork.bottom - mi.rcWork.top;
        if (sw < 320 || sh < 240) {
            err.set("GPU", "No se pudo obtener la resolución de pantalla.");
            return 1;
        }
        hwnd = ::CreateWindowExA(0, wc.lpszClassName, "Oasis Engine — Direct3D 11",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE, mi.rcWork.left, mi.rcWork.top, sw,
                                 sh, nullptr, nullptr, wc.hInstance, &ctx);
        maximized_show = true;
    }
    if (hwnd == nullptr) {
        err.set("GPU", "No se pudo crear la ventana.");
        return 1;
    }
    ::SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    if (!Initialize(ctx, hwnd, err)) {
        ReleaseContext(ctx);
        ::DestroyWindow(hwnd);
        return 1;
    }
    // Backend para diagnóstico: re-derivar nombre del adaptador real
    {
        IDXGIDevice* dg = nullptr;
        IDXGIAdapter* ad = nullptr;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(ctx.device->QueryInterface(__uuidof(IDXGIDevice),
                                                 reinterpret_cast<void**>(&dg))) &&
            SUCCEEDED(dg->GetAdapter(&ad)) && SUCCEEDED(ad->GetDesc(&desc))) {
            char tmp[128] = {};
            ::WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, tmp, 128, nullptr, nullptr);
            out_backend = tmp[0] != '\0' ? tmp : "adaptador de hardware";
        } else {
            out_backend = "adaptador de hardware";
        }
        if (ad != nullptr) ad->Release();
        if (dg != nullptr) dg->Release();
    }
    std::fprintf(stderr, "Renderer: Direct3D 11 | GPU: %s\n", out_backend.c_str());
    ::ShowWindow(hwnd, maximized_show ? SW_SHOWMAXIMIZED : SW_SHOW);
    ::SetForegroundWindow(hwnd);
    ::SetFocus(hwnd);
    LARGE_INTEGER freq{}, last{}, cur{};
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&last);
    double acc = 0.0;
    double ema_ms = 16.6;  // media móvil del frame para diagnóstico
    unsigned frames = 0;
    ctx.running = true;
    HANDLE stop_event = StopEventCreate(StopEventNameInternal(proj.root, false));
    HANDLE stop_save_event = StopEventCreate(StopEventNameInternal(proj.root, true));
    if (stop_event == nullptr || stop_save_event == nullptr) {
        std::fprintf(stderr, "Aviso: canal 'oasis stop' no disponible (sin evento).\n");
    }
    while (ctx.running) {
        ctx.scene = rt.scene;  // visible para picking en clicks (rayo del frame anterior)
        ctx.proj = &proj;      // idem para redibujar durante el resize modal
        MSG m{};
        while (::PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE) != 0) {
            ::TranslateMessage(&m);
            ::DispatchMessageA(&m);
        }
        if (!ctx.running) break;
        // 'oasis stop' desde otra terminal/IA. Con --save persiste la escena
        // viva antes de salir (equivale a ESC -> Sí); sin --save sale sin guardar.
        bool save_and_stop = StopEventPoll(stop_save_event);
        if (save_and_stop || StopEventPoll(stop_event)) {
            if (save_and_stop && ctx.scene != nullptr && ctx.proj != nullptr) {
                Error serr;
                if (SceneSaveActive(*ctx.proj, *ctx.scene, serr))
                    std::fprintf(stderr, "Parada con guardado: escena '%s' guardada.\n",
                                 ctx.scene->name.c_str());
                else
                    std::fprintf(stderr, "Parada con guardado: no se pudo guardar: %s\n",
                                 serr.message.c_str());
            }
            break;
        }
        ::QueryPerformanceCounter(&cur);
        double dt = static_cast<double>(cur.QuadPart - last.QuadPart) /
                    static_cast<double>(freq.QuadPart);
        last = cur;
        if (!(dt >= 0.0) || std::isfinite(dt) == 0) dt = 1.0 / 60.0;
        if (dt > 0.1) dt = 0.1;
        PollPointerLock(hwnd, ctx);
        UpdateCamera(*rt.scene, ctx, dt);
        Error uerr;
        if (!rt.update(dt, uerr)) {
            err = uerr;
            break;
        }
        RECT crc{};
        ::GetClientRect(hwnd, &crc);
        int vw = crc.right - crc.left;
        int vh = crc.bottom - crc.top;
        ctx.view_w = vw;
        ctx.view_h = vh;
        if (vw > 0 && vh > 0) {
            Error rerr;
            if (!EnsureBackbufferSize(ctx, vw, vh, rerr)) {
                err = rerr;
                break;
            }
            HRESULT pr = DrawFrame(ctx, proj, *rt.scene, vw, vh, cfg.vsync);
            if (pr == DXGI_ERROR_DEVICE_REMOVED || pr == DXGI_ERROR_DEVICE_RESET) {
                err.set("GPU", "Dispositivo D3D11 perdido (device removed).");
                break;
            }
        } else {
            ::Sleep(10);  // minimizado: no quemar CPU
        }
        ++frames;
        ema_ms += (dt * 1000.0 - ema_ms) * 0.05;
        acc += dt;
        if (acc >= 1.0) {
            const Transform* cam = nullptr;
            for (const auto& e : rt.scene->entities) {
                if (e.has_camera) {
                    cam = &e.transform;
                    break;
                }
            }
            char title[384];
            std::snprintf(title, sizeof(title),
                          "Oasis | %u FPS %.1fms | foco:%s eventos:%llu raton:%llu mouse:%s | "
                          "cam:[%.1f,%.1f,%.1f] rot:[%.2f,%.2f] sel:%s eje:%c",
                          frames, ema_ms, ctx.has_focus ? "si" : "no",
                          static_cast<unsigned long long>(ctx.event_count),
                          static_cast<unsigned long long>(ctx.mouse_event_count),
                          ctx.mouse_look ? "on" : "off",
                          cam == nullptr ? 0.0f : cam->position.x,
                          cam == nullptr ? 0.0f : cam->position.y,
                          cam == nullptr ? 0.0f : cam->position.z,
                          cam == nullptr ? 0.0f : cam->rotation.y,
                          cam == nullptr ? 0.0f : cam->rotation.x,
                          ctx.selected.empty() ? "-" : ctx.selected.c_str(),
                          ctx.locked_axis != 0 ? ctx.locked_axis : '-');
            ::SetWindowTextA(hwnd, title);
            acc = 0.0;
            frames = 0;
        }
        if (cfg.max_ticks > 0 && rt.tick_count >= cfg.max_ticks) break;
    }
    // Restaurar cursor/clip aunque se salga con el lock activo (ESC, --ticks, error, stop).
    UnlockMouse(ctx);
    if (stop_event != nullptr) {
        ::ResetEvent(stop_event);
        ::CloseHandle(stop_event);
        stop_event = nullptr;
    }
    if (stop_save_event != nullptr) {
        ::ResetEvent(stop_save_event);
        ::CloseHandle(stop_save_event);
        stop_save_event = nullptr;
    }
    if (ctx.info_hwnd != nullptr) {
        ::DestroyWindow(ctx.info_hwnd);
        ctx.info_hwnd = nullptr;
        ctx.info_edit = nullptr;
    }
    if (ctx.info_font != nullptr) {
        ::DeleteObject(ctx.info_font);
        ctx.info_font = nullptr;
    }
    ReleaseContext(ctx);
    ::DestroyWindow(hwnd);
    return err.empty() ? 0 : 1;
#else
    (void)proj;
    (void)rt;
    (void)cfg;
    (void)out_backend;
    err.set("GPU", "Direct3D 11 requiere Windows.");
    return 1;
#endif
}

std::string StopEventNameForRoot(const std::filesystem::path& project_root) {
#ifdef _WIN32
    return StopEventNameInternal(project_root, false);
#else
    (void)project_root;
    return {};
#endif
}

bool RequestStopForRoot(const std::filesystem::path& project_root, bool& out_signaled, Error& err,
                        bool save) {
    err.clear();
    out_signaled = false;
#ifdef _WIN32
    std::string name = StopEventNameInternal(project_root, save);
    HANDLE h = ::OpenEventA(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (h == nullptr && name.rfind("Global\\", 0) == 0) {
        std::string local = "Local\\" + name.substr(7);
        h = ::OpenEventA(EVENT_MODIFY_STATE, FALSE, local.c_str());
    }
    if (h == nullptr) return true;  // sin ventana escuchando: idempotente, stopped:false
    BOOL ok = ::SetEvent(h);
    ::CloseHandle(h);
    if (ok == FALSE) {
        err.set("INTERNAL", "No se pudo señalar parada a la ventana.");
        return false;
    }
    out_signaled = true;
    return true;
#else
    (void)project_root;
    err.set("GPU", "Direct3D 11 requiere Windows.");
    return false;
#endif
}

}  // namespace oasis
