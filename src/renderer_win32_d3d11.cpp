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
#include "stb_image.h"

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
    float normal[3];
    float color[3];
    float uv[2];
};
struct SceneBuffer {
    float wvp[16];
    float world[16];   // world matrix para posición en world-space (Blinn-Phong)
    float tint[4];     // rgb = color, a = 1.0 lit / 0.0 unlit (overlays)
    float light_dir[4]; // dirección normalizada de la luz (xyz), w padding
    float light_col[4]; // color de luz × intensidad (rgb), w padding
    float eye_pos[4];   // posición de la cámara (xyz), w padding
    float tex_params[4];  // x = 1.0 usa baseColorTexture / 0.0 sin textura, yzw padding
    float base_col[4];    // baseColorFactor del material (rgb), w padding
};

struct ModelCache {
    std::string asset_id;
    ID3D11Buffer* vertices = nullptr;
    ID3D11Buffer* indices = nullptr;
    ID3D11ShaderResourceView* texture = nullptr;  // nullptr = sin baseColorTexture
    UINT index_count = 0;
    float base_factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // pbrMetallicRoughness.baseColorFactor
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
    std::vector<HWND> info_kids;   // controles de sección (se reconstruyen por entidad)
    HFONT info_f_title = nullptr;  // Segoe UI negrita: nombre de entidad
    HFONT info_f_head = nullptr;   // Segoe UI negrita: cabeceras de sección
    HFONT info_f_body = nullptr;   // Segoe UI: valores
    HFONT info_f_json = nullptr;   // Consolas: bloque JSON
    HBRUSH info_brush_a = nullptr;  // muestra de color Mesh
    HBRUSH info_brush_b = nullptr;  // muestra de color Light
    HWND info_swatch_a = nullptr;
    HWND info_swatch_b = nullptr;
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
    ID3D11SamplerState* sampler = nullptr;  // lineal clamp para baseColorTexture
    // Calidad automática: render a menor resolución + blit (ver --min-fps).
    ID3D11Texture2D* low_tex = nullptr;
    ID3D11RenderTargetView* low_rtv = nullptr;
    ID3D11ShaderResourceView* low_srv = nullptr;
    ID3D11Texture2D* low_depth = nullptr;
    ID3D11DepthStencilView* low_dsv = nullptr;
    int low_w = 0;
    int low_h = 0;
    ID3D11VertexShader* blit_vs = nullptr;
    ID3D11PixelShader* blit_ps = nullptr;
    ID3D11InputLayout* blit_layout = nullptr;
    ID3D11Buffer* blit_vb = nullptr;
    float quality_scale = 1.0f;  // 1.0 = nativa; el evaluador la ajusta
    int quality_idx = 0;
    int quality_up_streak = 0;
    bool low_warned = false;  // aviso único si falla el target reducido
    // Cielo HDRI: textura equirect + shaders + estados (carga única perezosa).
    ID3D11ShaderResourceView* sky_srv = nullptr;
    ID3D11VertexShader* sky_vs = nullptr;
    ID3D11PixelShader* sky_ps = nullptr;
    ID3D11InputLayout* sky_layout = nullptr;
    ID3D11SamplerState* sky_sampler = nullptr;  // wrap en U (sin costura)
    ID3D11RasterizerState* rs_sky = nullptr;    // CULL_FRONT: cámara dentro del cubo
    std::string sky_asset;  // id cargado ("" = ninguno)
    bool sky_attempted = false;
    int buf_w = 0;  // tamaño real del backbuffer (resize en cada frame si cambia)
    int buf_h = 0;
    std::vector<ModelCache> models;
    std::vector<std::string> model_warned;  // asset_id ya avisados por stderr (sin spam por frame)
};

void ReleaseContext(Context& c) {
    for (auto& m : c.models) {
        if (m.texture != nullptr) m.texture->Release();
        if (m.indices != nullptr) m.indices->Release();
        if (m.vertices != nullptr) m.vertices->Release();
    }
    c.models.clear();
    if (c.low_dsv != nullptr) c.low_dsv->Release();
    if (c.low_depth != nullptr) c.low_depth->Release();
    if (c.low_srv != nullptr) c.low_srv->Release();
    if (c.low_rtv != nullptr) c.low_rtv->Release();
    if (c.low_tex != nullptr) c.low_tex->Release();
    if (c.blit_vb != nullptr) c.blit_vb->Release();
    if (c.blit_layout != nullptr) c.blit_layout->Release();
    if (c.blit_ps != nullptr) c.blit_ps->Release();
    if (c.blit_vs != nullptr) c.blit_vs->Release();
    if (c.rs_sky != nullptr) c.rs_sky->Release();
    if (c.sky_sampler != nullptr) c.sky_sampler->Release();
    if (c.sky_layout != nullptr) c.sky_layout->Release();
    if (c.sky_ps != nullptr) c.sky_ps->Release();
    if (c.sky_vs != nullptr) c.sky_vs->Release();
    if (c.sky_srv != nullptr) c.sky_srv->Release();
    if (c.sampler != nullptr) c.sampler->Release();
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
    c.sampler = nullptr;
    c.low_tex = nullptr;
    c.low_rtv = nullptr;
    c.low_srv = nullptr;
    c.low_depth = nullptr;
    c.low_dsv = nullptr;
    c.low_w = 0;
    c.low_h = 0;
    c.blit_vs = nullptr;
    c.blit_ps = nullptr;
    c.blit_layout = nullptr;
    c.blit_vb = nullptr;
    c.sky_srv = nullptr;
    c.sky_vs = nullptr;
    c.sky_ps = nullptr;
    c.sky_layout = nullptr;
    c.sky_sampler = nullptr;
    c.rs_sky = nullptr;
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

// --- Panel de datos (click der.): formato legible por secciones ---
std::wstring WidenUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n) <= 0) return {};
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

std::string FmtG(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.4g", static_cast<double>(v));
    return std::string(b);
}

std::string FmtV3(const Vec3& v) {
    return "[" + FmtG(v.x) + ", " + FmtG(v.y) + ", " + FmtG(v.z) + "]";
}

float Clamp01(float v) {
    if (!(v > 0.0f) || std::isfinite(v) == 0) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

std::string HexCol(const Vec3& c) {
    char b[8];
    std::snprintf(b, sizeof(b), "#%02X%02X%02X",
                  static_cast<int>(Clamp01(c.x) * 255.0f + 0.5f),
                  static_cast<int>(Clamp01(c.y) * 255.0f + 0.5f),
                  static_cast<int>(Clamp01(c.z) * 255.0f + 0.5f));
    return std::string(b);
}

void ClearInfoChildren(Context& ctx) {
    for (HWND h : ctx.info_kids) ::DestroyWindow(h);
    ctx.info_kids.clear();
    if (ctx.info_brush_a != nullptr) {
        ::DeleteObject(ctx.info_brush_a);
        ctx.info_brush_a = nullptr;
    }
    if (ctx.info_brush_b != nullptr) {
        ::DeleteObject(ctx.info_brush_b);
        ctx.info_brush_b = nullptr;
    }
    ctx.info_swatch_a = nullptr;
    ctx.info_swatch_b = nullptr;
}

// Etiqueta de texto (Unicode para tildes). id: 1 título, 100+ cabeceras, resto cuerpo.
HWND InfoLabel(Context& ctx, int id, HFONT font, const std::string& text, int x, int y, int w,
               int h) {
    HWND hwnd = ::CreateWindowExW(0, L"STATIC", WidenUtf8(text).c_str(),
                                  WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY, x, y, w, h,
                                  ctx.info_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  ::GetModuleHandleA(nullptr), nullptr);
    if (hwnd != nullptr) {
        ::SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ctx.info_kids.push_back(hwnd);
    }
    return hwnd;
}

// Muestra de color real (64x20 con borde). El pincel vive en el Context.
HWND InfoSwatch(Context& ctx, int id, const Vec3& c, int x, int y) {
    HBRUSH br = ::CreateSolidBrush(
        RGB(static_cast<int>(Clamp01(c.x) * 255.0f + 0.5f),
            static_cast<int>(Clamp01(c.y) * 255.0f + 0.5f),
            static_cast<int>(Clamp01(c.z) * 255.0f + 0.5f)));
    if (br == nullptr) return nullptr;
    if (id == 101) {
        if (ctx.info_brush_a != nullptr) ::DeleteObject(ctx.info_brush_a);
        ctx.info_brush_a = br;
    } else {
        if (ctx.info_brush_b != nullptr) ::DeleteObject(ctx.info_brush_b);
        ctx.info_brush_b = br;
    }
    HWND h = ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_NOTIFY,
                               x, y, 64, 20, ctx.info_hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               ::GetModuleHandleA(nullptr), nullptr);
    if (h == nullptr) return nullptr;
    if (id == 101)
        ctx.info_swatch_a = h;
    else
        ctx.info_swatch_b = h;
    ctx.info_kids.push_back(h);
    return h;
}

LRESULT CALLBACK InfoProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        ::ShowWindow(hwnd, SW_HIDE);  // la X oculta, no destruye
        return 0;
    }
    if (msg == WM_CTLCOLORSTATIC) {
        HDC hdc = reinterpret_cast<HDC>(wp);
        HWND ctrl = reinterpret_cast<HWND>(lp);
        Context* ctx =
            reinterpret_cast<Context*>(::GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        ::SetBkMode(hdc, TRANSPARENT);
        int id = ::GetDlgCtrlID(ctrl);
        if (ctx != nullptr && (ctrl == ctx->info_swatch_a || ctrl == ctx->info_swatch_b)) {
            HBRUSH br = (ctrl == ctx->info_swatch_a) ? ctx->info_brush_a : ctx->info_brush_b;
            return reinterpret_cast<LRESULT>(br != nullptr ? br : GetStockObject(WHITE_BRUSH));
        }
        if (id == 1)
            ::SetTextColor(hdc, RGB(17, 24, 39));  // título casi negro
        else if (id >= 100 && id < 200)
            ::SetTextColor(hdc, RGB(194, 65, 12));  // cabeceras naranja Oasis
        else if (id == 2)
            ::SetTextColor(hdc, RGB(107, 114, 128));  // subtítulo gris
        else
            ::SetTextColor(hdc, RGB(31, 41, 55));  // cuerpo gris oscuro
        return reinterpret_cast<LRESULT>(::GetStockObject(WHITE_BRUSH));
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
    constexpr int kW = 560, kX = 16, kContentW = kW - 2 * kX;
    if (ctx.info_hwnd == nullptr) {
        WNDCLASSA wc{};
        wc.lpfnWndProc = InfoProc;
        wc.hInstance = ::GetModuleHandleA(nullptr);
        wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH));
        wc.lpszClassName = "OasisInfoV2";
        if (::RegisterClassA(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        ctx.info_hwnd = ::CreateWindowExA(0, wc.lpszClassName, "Oasis · datos",
                                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                                          CW_USEDEFAULT, CW_USEDEFAULT, kW, 480, main_hwnd, nullptr,
                                          wc.hInstance, nullptr);
        if (ctx.info_hwnd == nullptr) return;
        ::SetWindowLongPtrA(ctx.info_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
        auto make_font = [](int px, int weight, const char* face) {
            return ::CreateFontA(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                 VARIABLE_PITCH | FF_SWISS, face);
        };
        ctx.info_f_title = make_font(18, FW_BOLD, "Segoe UI");
        ctx.info_f_head = make_font(14, FW_BOLD, "Segoe UI");
        ctx.info_f_body = make_font(14, FW_NORMAL, "Segoe UI");
        ctx.info_f_json = make_font(13, FW_NORMAL, "Consolas");
        if (ctx.info_f_title == nullptr || ctx.info_f_head == nullptr ||
            ctx.info_f_body == nullptr || ctx.info_f_json == nullptr)
            return;
    }
    ClearInfoChildren(ctx);
    int y = 12;
    InfoLabel(ctx, 1, ctx.info_f_title, e.name.empty() ? e.id : e.name, kX, y, kContentW, 30);
    y += 30;
    InfoLabel(ctx, 2, ctx.info_f_body, "id: " + e.id, kX, y, kContentW, 20);
    y += 26;
    int head_id = 100;
    auto head = [&](const std::string& t) {
        InfoLabel(ctx, head_id++, ctx.info_f_head, t, kX, y, kContentW, 24);
        y += 24;
    };
    auto line = [&](const std::string& t) {
        InfoLabel(ctx, 300, ctx.info_f_body, t, kX + 8, y, kContentW - 8, 20);
        y += 20;
    };
    auto color_row = [&](int swatch_id, const Vec3& c) {
        InfoSwatch(ctx, swatch_id, c, kX + 8, y);
        InfoLabel(ctx, 300, ctx.info_f_body, FmtV3(c) + "  " + HexCol(c), kX + 80, y,
                  kContentW - 88, 20);
        y += 26;
    };
    if (e.has_transform) {
        head("UBICACIÓN");
        line("Posición:  " + FmtV3(e.transform.position));
        line("Rotación:  " + FmtV3(e.transform.rotation));
        y += 4;
        head("TAMAÑO");
        line("Escala:  " + FmtV3(e.transform.scale));
        y += 4;
    }
    if (e.has_mesh) {
        head("MALLA");
        line("Primitiva:  " + e.mesh.primitive);
        if (e.mesh.primitive == "asset") {
            line("Asset:  " + e.mesh.asset_id);
            std::string tex_state = "Pendiente de carga";
            for (const auto& m : ctx.models) {
                if (m.asset_id == e.mesh.asset_id) {
                    tex_state =
                        (m.texture != nullptr) ? "Sí (baseColorTexture)" : "No (solo color)";
                    break;
                }
            }
            line("Textura:  " + tex_state);
        }
        y += 4;
        head("COLOR");
        color_row(101, e.mesh.color);
        y += 2;
    }
    if (e.has_light) {
        head("LUZ");
        color_row(102, e.light.color);
        line("Intensidad:  " + FmtG(e.light.intensity));
        y += 4;
    }
    if (e.has_camera) {
        head("CÁMARA");
        line("FOV:  " + FmtG(e.camera.fov_degrees) + "°");
        y += 4;
    }
    head("DATOS JSON");
    HWND edit = ::CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", WidenUtf8(EntityToJson(e)).c_str(),
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        kX, y, kContentW, 110, ctx.info_hwnd, nullptr, ::GetModuleHandleA(nullptr), nullptr);
    if (edit != nullptr) {
        ::SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(ctx.info_f_json), TRUE);
        ctx.info_kids.push_back(edit);
        y += 110;
    }
    y += 12;
    std::string title = "Oasis · " + e.id;
    ::SetWindowTextW(ctx.info_hwnd, WidenUtf8(title).c_str());
    // Flotante junto al cursor sin robar el foco (el mouse-look lo necesita).
    POINT pt{};
    ::GetCursorPos(&pt);
    RECT wa{};
    ::SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int x = pt.x + 16, yy = pt.y + 16;
    if (x + kW > wa.right) x = wa.right - kW;
    if (yy + y > wa.bottom) yy = wa.bottom - y;
    if (x < wa.left) x = wa.left;
    if (yy < wa.top) yy = wa.top;
    ::SetWindowPos(ctx.info_hwnd, HWND_TOP, x, yy, kW, y, SWP_SHOWWINDOW | SWP_NOACTIVATE);
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
    // zn=0.1/zf=2000: el suelo 150x150 y la rejilla no deben cortarse al alejarse.
    // Con D24 la precisión a 200 uds es ~0.03 uds, suficiente para separaciones 0.5.
    const float zn = 0.1f, zf = 2000.0f;
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

// Modelo cargado del primer primitivo GLB: geometría + material PBR básico.
// Sin NORMAL se calculan normales suavizadas; sin TEXCOORD_0/material la malla
// usa color de vértice (cubos del editor) modulado por Mesh.color.
struct LoadedModel {
    std::vector<Vertex> verts;
    std::vector<std::uint32_t> idx;
    float base_factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<unsigned char> tex_rgba;  // vacío = sin baseColorTexture
    int tex_w = 0;
    int tex_h = 0;
};

bool LoadAssetGeometry(const Project& proj, const std::string& asset_id, LoadedModel& model,
                       Error& err) {
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
    cJSON* nrm_acc = nullptr;
    cJSON* uv_acc = nullptr;
    if (prim != nullptr) {
        cJSON* attrs = cJSON_GetObjectItemCaseSensitive(prim, "attributes");
        cJSON* pos_ref = attrs != nullptr ? cJSON_GetObjectItemCaseSensitive(attrs, "POSITION") : nullptr;
        cJSON* idx_ref = cJSON_GetObjectItemCaseSensitive(prim, "indices");
        cJSON* nrm_ref = attrs != nullptr ? cJSON_GetObjectItemCaseSensitive(attrs, "NORMAL") : nullptr;
        cJSON* uv_ref =
            attrs != nullptr ? cJSON_GetObjectItemCaseSensitive(attrs, "TEXCOORD_0") : nullptr;
        if (cJSON_IsNumber(pos_ref) != 0 && pos_ref->valueint >= 0)
            pos_acc = cJSON_GetArrayItem(accessors, pos_ref->valueint);
        if (cJSON_IsNumber(idx_ref) != 0 && idx_ref->valueint >= 0)
            idx_acc = cJSON_GetArrayItem(accessors, idx_ref->valueint);
        if (cJSON_IsNumber(nrm_ref) != 0 && nrm_ref->valueint >= 0)
            nrm_acc = cJSON_GetArrayItem(accessors, nrm_ref->valueint);
        if (cJSON_IsNumber(uv_ref) != 0 && uv_ref->valueint >= 0)
            uv_acc = cJSON_GetArrayItem(accessors, uv_ref->valueint);
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
                // NORMAL / TEXCOORD_0 opcionales: mismo count que POSITION o se ignoran.
                auto resolve_vec = [&](cJSON* acc, const char* want_type, std::uint32_t elem_bytes,
                                       std::uint32_t& out_off,
                                       std::uint32_t& out_stride) -> bool {
                    if (cJSON_IsObject(acc) == 0) return false;
                    cJSON* ty = cJSON_GetObjectItemCaseSensitive(acc, "type");
                    cJSON* ct = cJSON_GetObjectItemCaseSensitive(acc, "componentType");
                    cJSON* co = cJSON_GetObjectItemCaseSensitive(acc, "count");
                    cJSON* bv_ref = cJSON_GetObjectItemCaseSensitive(acc, "bufferView");
                    cJSON* bv = (cJSON_IsNumber(bv_ref) != 0 && bv_ref->valueint >= 0)
                                    ? cJSON_GetArrayItem(views, bv_ref->valueint)
                                    : nullptr;
                    if (cJSON_IsString(ty) == 0 || std::strcmp(ty->valuestring, want_type) != 0 ||
                        cJSON_IsNumber(ct) == 0 || ct->valueint != 5126 ||
                        cJSON_IsNumber(co) == 0 || co->valueint != static_cast<int>(pc) ||
                        cJSON_IsObject(bv) == 0)
                        return false;
                    out_off = u32(bv, "byteOffset") + u32(acc, "byteOffset");
                    out_stride = elem_bytes;
                    cJSON* st = cJSON_GetObjectItemCaseSensitive(bv, "byteStride");
                    if (cJSON_IsNumber(st) != 0) {
                        if (st->valueint < static_cast<int>(elem_bytes)) return false;
                        out_stride = static_cast<std::uint32_t>(st->valueint);
                    }
                    if (out_off > bin_bytes || pc > (bin_bytes - out_off) / out_stride) return false;
                    return true;
                };
                std::uint32_t nrm_off = 0, nrm_stride = 12, uv_off = 0, uv_stride = 8;
                bool have_normals =
                    resolve_vec(nrm_acc, "VEC3", 12U, nrm_off, nrm_stride);
                bool have_uv = resolve_vec(uv_acc, "VEC2", 8U, uv_off, uv_stride);
                model.verts.resize(pc);
                model.idx.resize(ic);
                bool uv_bad = false;
                for (std::uint32_t i = 0; i < pc; ++i) {
                    std::memcpy(model.verts[i].position, bin + pos_off + i * stride, 12);
                    model.verts[i].color[0] = model.verts[i].color[1] = model.verts[i].color[2] =
                        1.0f;
                    model.verts[i].uv[0] = model.verts[i].uv[1] = 0.0f;
                    if (have_normals)
                        std::memcpy(model.verts[i].normal, bin + nrm_off + i * nrm_stride, 12);
                    else
                        model.verts[i].normal[0] = model.verts[i].normal[1] =
                            model.verts[i].normal[2] = 0.0f;
                    if (have_uv) {
                        float tmp_uv[2] = {0.0f, 0.0f};
                        std::memcpy(tmp_uv, bin + uv_off + i * uv_stride, 8);
                        if (std::isfinite(tmp_uv[0]) == 0 || std::isfinite(tmp_uv[1]) == 0)
                            uv_bad = true;
                        else {
                            model.verts[i].uv[0] = tmp_uv[0];
                            model.verts[i].uv[1] = tmp_uv[1];
                        }
                    }
                    if (std::isfinite(model.verts[i].position[0]) == 0 ||
                        std::isfinite(model.verts[i].position[1]) == 0 ||
                        std::isfinite(model.verts[i].position[2]) == 0 ||
                        (have_normals && (std::isfinite(model.verts[i].normal[0]) == 0 ||
                                          std::isfinite(model.verts[i].normal[1]) == 0 ||
                                          std::isfinite(model.verts[i].normal[2]) == 0))) {
                        cJSON_Delete(root);
                        return false;
                    }
                }
                if (uv_bad) {
                    have_uv = false;
                    for (auto& v : model.verts) v.uv[0] = v.uv[1] = 0.0f;
                }
                for (std::uint32_t i = 0; i < ic; ++i) {
                    if (idx_ct->valueint == 5123)
                        model.idx[i] = static_cast<std::uint32_t>(
                            bin[idx_off + i * 2U] |
                            (static_cast<std::uint32_t>(bin[idx_off + i * 2U + 1U]) << 8U));
                    else
                        model.idx[i] = U32Le(bin + idx_off + i * 4U);
                    if (model.idx[i] >= pc) {
                        cJSON_Delete(root);
                        return false;
                    }
                }
                if (!have_normals) {
                    // Normales suavizadas por área para GLB sin NORMAL (p. ej. Firefox):
                    // acumular caras y normalizar; degenerado -> +Y.
                    for (std::uint32_t t = 0; t + 2U < ic; t += 3U) {
                        Vertex& a = model.verts[model.idx[t]];
                        Vertex& b = model.verts[model.idx[t + 1U]];
                        Vertex& c = model.verts[model.idx[t + 2U]];
                        float e1[3] = {b.position[0] - a.position[0], b.position[1] - a.position[1],
                                       b.position[2] - a.position[2]};
                        float e2[3] = {c.position[0] - a.position[0], c.position[1] - a.position[1],
                                       c.position[2] - a.position[2]};
                        float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                                       e1[2] * e2[0] - e1[0] * e2[2],
                                       e1[0] * e2[1] - e1[1] * e2[0]};
                        if (std::isfinite(fn[0]) == 0 || std::isfinite(fn[1]) == 0 ||
                            std::isfinite(fn[2]) == 0)
                            continue;
                        for (int k = 0; k < 3; ++k) {
                            a.normal[k] += fn[k];
                            b.normal[k] += fn[k];
                            c.normal[k] += fn[k];
                        }
                    }
                    for (auto& v : model.verts) {
                        float len = std::sqrt(v.normal[0] * v.normal[0] +
                                              v.normal[1] * v.normal[1] +
                                              v.normal[2] * v.normal[2]);
                        if (len > 1e-12f && std::isfinite(len) != 0) {
                            v.normal[0] /= len;
                            v.normal[1] /= len;
                            v.normal[2] /= len;
                        } else {
                            v.normal[0] = 0.0f;
                            v.normal[1] = 1.0f;
                            v.normal[2] = 0.0f;
                        }
                    }
                }
                // Material PBR básico del primer primitivo (opcional, no fatal).
                if (prim != nullptr) {
                    cJSON* mat_ref = cJSON_GetObjectItemCaseSensitive(prim, "material");
                    cJSON* materials = cJSON_GetObjectItemCaseSensitive(root, "materials");
                    cJSON* mat = (cJSON_IsNumber(mat_ref) != 0 && mat_ref->valueint >= 0)
                                     ? cJSON_GetArrayItem(materials, mat_ref->valueint)
                                     : nullptr;
                    if (cJSON_IsObject(mat) != 0) {
                        cJSON* pbr =
                            cJSON_GetObjectItemCaseSensitive(mat, "pbrMetallicRoughness");
                        if (cJSON_IsObject(pbr) != 0) {
                            cJSON* bcf =
                                cJSON_GetObjectItemCaseSensitive(pbr, "baseColorFactor");
                            if (cJSON_IsArray(bcf) != 0 && cJSON_GetArraySize(bcf) == 4) {
                                float cf[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                                bool cf_ok = true;
                                for (int k = 0; k < 4; ++k) {
                                    cJSON* n = cJSON_GetArrayItem(bcf, k);
                                    if (cJSON_IsNumber(n) == 0 ||
                                        std::isfinite(n->valuedouble) == 0) {
                                        cf_ok = false;
                                        break;
                                    }
                                    float fv = static_cast<float>(n->valuedouble);
                                    if (fv < 0.0f) fv = 0.0f;
                                    if (fv > 1.0f) fv = 1.0f;
                                    cf[k] = fv;
                                }
                                if (cf_ok) std::memcpy(model.base_factor, cf, sizeof(cf));
                            }
                            cJSON* bct =
                                cJSON_GetObjectItemCaseSensitive(pbr, "baseColorTexture");
                            cJSON* bct_idx = bct != nullptr
                                                 ? cJSON_GetObjectItemCaseSensitive(bct, "index")
                                                 : nullptr;
                            cJSON* textures = cJSON_GetObjectItemCaseSensitive(root, "textures");
                            cJSON* tex = (cJSON_IsNumber(bct_idx) != 0 && bct_idx->valueint >= 0)
                                             ? cJSON_GetArrayItem(textures, bct_idx->valueint)
                                             : nullptr;
                            cJSON* src_ref = tex != nullptr
                                                 ? cJSON_GetObjectItemCaseSensitive(tex, "source")
                                                 : nullptr;
                            cJSON* images = cJSON_GetObjectItemCaseSensitive(root, "images");
                            cJSON* img = (cJSON_IsNumber(src_ref) != 0 && src_ref->valueint >= 0)
                                             ? cJSON_GetArrayItem(images, src_ref->valueint)
                                             : nullptr;
                            cJSON* img_bv_ref =
                                img != nullptr
                                    ? cJSON_GetObjectItemCaseSensitive(img, "bufferView")
                                    : nullptr;
                            cJSON* img_bv =
                                (cJSON_IsNumber(img_bv_ref) != 0 && img_bv_ref->valueint >= 0)
                                    ? cJSON_GetArrayItem(views, img_bv_ref->valueint)
                                    : nullptr;
                            if (cJSON_IsObject(img_bv) != 0) {
                                std::uint32_t img_off = u32(img_bv, "byteOffset");
                                std::uint32_t img_len = u32(img_bv, "byteLength");
                                if (img_len > 0 && img_len <= 32U * 1024U * 1024U &&
                                    img_off <= bin_bytes && img_len <= bin_bytes - img_off) {
                                    int w = 0, h = 0, comp = 0;
                                    unsigned char* px = stbi_load_from_memory(
                                        bin + img_off, static_cast<int>(img_len), &w, &h, &comp,
                                        4);
                                    if (px != nullptr && w > 0 && h > 0 && w <= 4096 && h <= 4096 &&
                                        static_cast<std::uint64_t>(w) *
                                                static_cast<std::uint64_t>(h) * 4ULL <=
                                            64ULL * 1024ULL * 1024ULL) {
                                        model.tex_rgba.assign(
                                            px, px + static_cast<std::size_t>(w) *
                                                        static_cast<std::size_t>(h) * 4U);
                                        model.tex_w = w;
                                        model.tex_h = h;
                                    } else {
                                        std::fprintf(
                                            stderr,
                                            "Aviso: imagen de '%s' no decodificable, sin textura.\n",
                                            asset_id.c_str());
                                    }
                                    if (px != nullptr) stbi_image_free(px);
                                }
                            }
                        }
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
    LoadedModel loaded;
    Error tmp;
    if (!LoadAssetGeometry(proj, asset_id, loaded, tmp)) {
        err.set(tmp.code.empty() ? "BAD_FORMAT" : tmp.code,
                tmp.message.empty() ? "El GLB no contiene una malla compatible." : tmp.message);
        return nullptr;
    }
    std::vector<Vertex>& verts = loaded.verts;
    std::vector<std::uint32_t>& indices = loaded.idx;
    std::uint64_t vb = static_cast<std::uint64_t>(verts.size()) * sizeof(Vertex);
    std::uint64_t ib = static_cast<std::uint64_t>(indices.size()) * sizeof(std::uint32_t);
    if (verts.empty() || indices.empty() || vb > 128ULL * 1024ULL * 1024ULL ||
        ib > 128ULL * 1024ULL * 1024ULL) {
        err.set("LIMIT", "La malla del asset supera el límite del renderer.");
        return nullptr;
    }
    ModelCache m;
    m.asset_id = asset_id;
    std::memcpy(m.base_factor, loaded.base_factor, sizeof(m.base_factor));
    // Bounds en espacio del modelo para picking proporcional a lo visible.
    for (const auto& v : verts) {
        for (int i = 0; i < 3; ++i) {
            if (v.position[i] < m.bmin[i]) m.bmin[i] = v.position[i];
            if (v.position[i] > m.bmax[i]) m.bmax[i] = v.position[i];
        }
    }
    if (!loaded.tex_rgba.empty() && loaded.tex_w > 0 && loaded.tex_h > 0) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(loaded.tex_w);
        td.Height = static_cast<UINT>(loaded.tex_h);
        td.MipLevels = 0;  // cadena completa: se generan abajo
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        D3D11_SUBRESOURCE_DATA tsd{};
        tsd.pSysMem = loaded.tex_rgba.data();
        tsd.SysMemPitch = static_cast<UINT>(loaded.tex_w) * 4U;
        ID3D11Texture2D* tex = nullptr;
        HRESULT thr = ctx.device->CreateTexture2D(&td, &tsd, &tex);
        if (SUCCEEDED(thr)) thr = ctx.device->CreateShaderResourceView(tex, nullptr, &m.texture);
        if (SUCCEEDED(thr)) ctx.context->GenerateMips(m.texture);
        if (tex != nullptr) tex->Release();
        if (FAILED(thr)) {
            if (m.texture != nullptr) {
                m.texture->Release();
                m.texture = nullptr;
            }
            std::fprintf(stderr, "Aviso: textura de '%s' no utilizable en GPU, sin textura.\n",
                         asset_id.c_str());
        }
    }
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(vb);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = verts.data();
    if (FAILED(ctx.device->CreateBuffer(&desc, &data, &m.vertices))) {
        if (m.texture != nullptr) m.texture->Release();
        err.set("GPU", "No se pudo crear el buffer de vértices del asset '" + asset_id + "'.");
        return nullptr;
    }
    desc.ByteWidth = static_cast<UINT>(ib);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = indices.data();
    if (FAILED(ctx.device->CreateBuffer(&desc, &data, &m.indices))) {
        if (m.vertices != nullptr) m.vertices->Release();
        if (m.texture != nullptr) m.texture->Release();
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
    "  float4 tex_params;"
    "  float4 base_col;"
    "};"
    "struct I { float3 p:POSITION; float3 n:NORMAL; float3 c:COLOR; float2 uv:TEXCOORD0; };"
    "struct O { float4 p:SV_POSITION; float3 c:COLOR; float3 wp:TEXCOORD0; float3 wn:TEXCOORD1;"
    " float2 uv:TEXCOORD2; };"
    "O VSMain(I i) {"
    "  O o;"
    "  o.p  = mul(float4(i.p,1), wvp);"
    "  o.c  = i.c * tint.rgb * base_col.rgb;"
    "  o.wp = mul(float4(i.p,1), world).xyz;"
    "  o.wn = normalize(mul(i.n, (float3x3)world));"
    "  o.uv = i.uv;"
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
    "  float4 tex_params;"
    "  float4 base_col;"
    "};"
    "Texture2D albedo_tex : register(t0);"
    "SamplerState albedo_smp : register(s0);"
    "struct I { float4 p:SV_POSITION; float3 c:COLOR; float3 wp:TEXCOORD0; float3 wn:TEXCOORD1;"
    " float2 uv:TEXCOORD2; };"
    "float4 PSMain(I i) : SV_TARGET {"
    "  if (tint.a < 0.5) return float4(i.c, 1);"
    "  float3 albedo = i.c;"
    "  if (tex_params.x > 0.5) albedo *= albedo_tex.Sample(albedo_smp, i.uv).rgb;"
    "  float3 N = normalize(i.wn);"
    "  float3 L = normalize(-light_dir.xyz);"
    "  float3 V = normalize(eye_pos.xyz - i.wp);"
    "  float3 ambient = 0.15 * albedo;"
    "  float NdotL = saturate(dot(N, L));"
    "  float3 diffuse = albedo * light_col.rgb * NdotL;"
    "  float3 H = normalize(L + V);"
    "  float spec = pow(saturate(dot(N, H)), 32.0);"
    "  float3 specular = light_col.rgb * spec * 0.4;"
    "  return float4(ambient + diffuse + specular, 1);"
    "}";

// Blit de reescalado (calidad automática): triángulo pantalla completa.
const char* kBlitVsSrc =
    "struct I { float2 p:POSITION; float2 uv:TEXCOORD0; };"
    "struct O { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };"
    "O BVSMain(I i) { O o; o.p = float4(i.p, 0, 1); o.uv = i.uv; return o; }";
const char* kBlitPsSrc =
    "Texture2D src_tex : register(t0);"
    "SamplerState src_smp : register(s0);"
    "struct I { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };"
    "float4 BPSMain(I i) : SV_TARGET { return src_tex.Sample(src_smp, i.uv); }";

struct BlitVertex {
    float p[2];
    float uv[2];
};

// Cielo HDRI: cubo centrado en la cámara con textura equirect + tonemap ACES.
// Solo fondo: la luz de escena sigue siendo direccional (sin IBL).
const char* kSkyVsSrc =
    "cbuffer SceneBuffer : register(b0) {"
    "  row_major float4x4 wvp;"
    "  row_major float4x4 world;"
    "  float4 tint;"
    "  float4 light_dir;"
    "  float4 light_col;"
    "  float4 eye_pos;"
    "  float4 tex_params;"
    "  float4 base_col;"
    "};"
    "struct I { float3 p:POSITION; };"
    "struct O { float4 p:SV_POSITION; float3 wp:TEXCOORD0; };"
    "O SVSMain(I i) {"
    "  O o;"
    "  o.p  = mul(float4(i.p,1), wvp);"
    "  o.wp = mul(float4(i.p,1), world).xyz;"
    "  return o;"
    "}";
const char* kSkyPsSrc =
    "cbuffer SceneBuffer : register(b0) {"
    "  row_major float4x4 wvp;"
    "  row_major float4x4 world;"
    "  float4 tint;"
    "  float4 light_dir;"
    "  float4 light_col;"
    "  float4 eye_pos;"
    "  float4 tex_params;"
    "  float4 base_col;"
    "};"
    "Texture2D sky_tex : register(t0);"
    "SamplerState sky_smp : register(s0);"
    "struct I { float4 p:SV_POSITION; float3 wp:TEXCOORD0; };"
    "float4 SPSMain(I i) : SV_TARGET {"
    "  float3 d = i.wp - eye_pos.xyz;"
    "  float len = length(d);"
    "  if (!(len > 1e-6)) return float4(0.02, 0.05, 0.09, 1);"
    "  d /= len;"
    "  float u = atan2(d.x, d.z) * 0.15915494 + 0.5;"
    "  float v = 0.5 - asin(clamp(d.y, -1.0, 1.0)) * 0.31830988;"  // v=0 fila sup. D3D = +Y stb
    "  float3 hdr = sky_tex.Sample(sky_smp, float2(u, v)).rgb;"
    "  float3 t = (hdr * (2.51 * hdr + 0.03)) / (hdr * (2.43 * hdr + 0.59) + 0.14);"
    "  t = pow(saturate(t), 1.0 / 2.2);"
    "  return float4(t, 1);"
    "}";

// Niveles de resolución (1.0 = nativa). Histéresis en el evaluador del bucle.
static const float kQualityScales[5] = {1.0f, 0.85f, 0.7f, 0.55f, 0.4f};

bool Initialize(Context& ctx, HWND hwnd, Error& err) {
    // Cubo con normales por cara (24 vértices: 4 por cara). Mismo winding que el
    // cubo de 8 vértices anterior. Vértices blancos: el color visible lo decide
    // Mesh.color (tinte) × baseColor, sin arcoíris heredado por esquina.
    static const Vertex kCube[24] = {
        // atrás (z=-1)
        {{-1, -1, -1}, {0, 0, -1}, {1, 1, 1}, {0, 0}},
        {{-1, 1, -1}, {0, 0, -1}, {1, 1, 1}, {0, 0}},
        {{1, 1, -1}, {0, 0, -1}, {1, 1, 1}, {0, 0}},
        {{1, -1, -1}, {0, 0, -1}, {1, 1, 1}, {0, 0}},
        // delante (z=+1)
        {{-1, -1, 1}, {0, 0, 1}, {1, 1, 1}, {0, 0}},
        {{1, -1, 1}, {0, 0, 1}, {1, 1, 1}, {0, 0}},
        {{1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {0, 0}},
        {{-1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {0, 0}},
        // izquierda (x=-1)
        {{-1, -1, -1}, {-1, 0, 0}, {1, 1, 1}, {0, 0}},
        {{-1, -1, 1}, {-1, 0, 0}, {1, 1, 1}, {0, 0}},
        {{-1, 1, 1}, {-1, 0, 0}, {1, 1, 1}, {0, 0}},
        {{-1, 1, -1}, {-1, 0, 0}, {1, 1, 1}, {0, 0}},
        // derecha (x=+1)
        {{1, -1, -1}, {1, 0, 0}, {1, 1, 1}, {0, 0}},
        {{1, 1, -1}, {1, 0, 0}, {1, 1, 1}, {0, 0}},
        {{1, 1, 1}, {1, 0, 0}, {1, 1, 1}, {0, 0}},
        {{1, -1, 1}, {1, 0, 0}, {1, 1, 1}, {0, 0}},
        // arriba (y=+1)
        {{-1, 1, -1}, {0, 1, 0}, {1, 1, 1}, {0, 0}},
        {{-1, 1, 1}, {0, 1, 0}, {1, 1, 1}, {0, 0}},
        {{1, 1, 1}, {0, 1, 0}, {1, 1, 1}, {0, 0}},
        {{1, 1, -1}, {0, 1, 0}, {1, 1, 1}, {0, 0}},
        // abajo (y=-1)
        {{-1, -1, -1}, {0, -1, 0}, {1, 1, 1}, {0, 0}},
        {{1, -1, -1}, {0, -1, 0}, {1, 1, 1}, {0, 0}},
        {{1, -1, 1}, {0, -1, 0}, {1, 1, 1}, {0, 0}},
        {{-1, -1, 1}, {0, -1, 0}, {1, 1, 1}, {0, 0}},
    };
    static const unsigned short kIdx[36] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,
                                            8,  9,  10, 8,  10, 11, 12, 13, 14, 12, 14, 15,
                                            16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};
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
    D3D11_INPUT_ELEMENT_DESC elems[4] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    if (SUCCEEDED(hr))
        hr = ctx.device->CreateInputLayout(elems, 4, vsb->GetBufferPointer(),
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
    if (SUCCEEDED(hr)) {
        // Muestreador lineal con clamp para baseColorTexture.
        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.MaxAnisotropy = 1;
        smp.ComparisonFunc = D3D11_COMPARISON_NEVER;
        smp.MinLOD = 0.0f;
        smp.MaxLOD = D3D11_FLOAT32_MAX;  // mipmaps generados: menos resolución a distancia
        hr = ctx.device->CreateSamplerState(&smp, &ctx.sampler);
    }
    if (SUCCEEDED(hr)) {
        // Pipeline de blit: shaders + layout + triángulo pantalla completa.
        ID3DBlob* bvs = nullptr;
        ID3DBlob* bps = nullptr;
        bool bok = CompileShader(kBlitVsSrc, "BVSMain", "vs_4_0", &bvs, err) &&
                   CompileShader(kBlitPsSrc, "BPSMain", "ps_4_0", &bps, err);
        if (bok) {
            hr = ctx.device->CreateVertexShader(bvs->GetBufferPointer(), bvs->GetBufferSize(),
                                                nullptr, &ctx.blit_vs);
            if (SUCCEEDED(hr))
                hr = ctx.device->CreatePixelShader(bps->GetBufferPointer(), bps->GetBufferSize(),
                                                   nullptr, &ctx.blit_ps);
            D3D11_INPUT_ELEMENT_DESC belems[2] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            if (SUCCEEDED(hr))
                hr = ctx.device->CreateInputLayout(belems, 2, bvs->GetBufferPointer(),
                                                   bvs->GetBufferSize(), &ctx.blit_layout);
        } else {
            hr = E_FAIL;  // err ya describe el shader que falló
        }
        if (bvs != nullptr) bvs->Release();
        if (bps != nullptr) bps->Release();
    }
    static const BlitVertex kBlitTri[3] = {{{-1.0f, -1.0f}, {0.0f, 1.0f}},
                                           {{3.0f, -1.0f}, {2.0f, 1.0f}},
                                           {{-1.0f, 3.0f}, {0.0f, -1.0f}}};
    if (SUCCEEDED(hr)) {
        D3D11_BUFFER_DESC bd2{};
        bd2.ByteWidth = sizeof(kBlitTri);
        bd2.Usage = D3D11_USAGE_DEFAULT;
        bd2.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd2{};
        sd2.pSysMem = kBlitTri;
        hr = ctx.device->CreateBuffer(&bd2, &sd2, &ctx.blit_vb);
    }
    if (SUCCEEDED(hr)) {
        // Cielo HDRI: shaders equirect + layout POSITION + sampler con wrap en U.
        ID3DBlob* svs = nullptr;
        ID3DBlob* sps = nullptr;
        bool sok = CompileShader(kSkyVsSrc, "SVSMain", "vs_4_0", &svs, err) &&
                   CompileShader(kSkyPsSrc, "SPSMain", "ps_4_0", &sps, err);
        if (sok) {
            hr = ctx.device->CreateVertexShader(svs->GetBufferPointer(), svs->GetBufferSize(),
                                                nullptr, &ctx.sky_vs);
            if (SUCCEEDED(hr))
                hr = ctx.device->CreatePixelShader(sps->GetBufferPointer(), sps->GetBufferSize(),
                                                   nullptr, &ctx.sky_ps);
            D3D11_INPUT_ELEMENT_DESC selems[1] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA,
                 0}};
            if (SUCCEEDED(hr))
                hr = ctx.device->CreateInputLayout(selems, 1, svs->GetBufferPointer(),
                                                   svs->GetBufferSize(), &ctx.sky_layout);
        } else {
            hr = E_FAIL;  // err ya describe el shader que falló
        }
        if (svs != nullptr) svs->Release();
        if (sps != nullptr) sps->Release();
    }
    if (SUCCEEDED(hr)) {
        D3D11_SAMPLER_DESC skysd{};
        skysd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        skysd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;  // equirect sin costura
        skysd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        skysd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        skysd.MaxAnisotropy = 1;
        skysd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        skysd.MinLOD = 0.0f;
        skysd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = ctx.device->CreateSamplerState(&skysd, &ctx.sky_sampler);
    }
    if (SUCCEEDED(hr)) {
        // Dentro del cubo se ven sus caras traseras.
        D3D11_RASTERIZER_DESC rsd{};
        rsd.FillMode = D3D11_FILL_SOLID;
        rsd.CullMode = D3D11_CULL_FRONT;
        rsd.FrontCounterClockwise = FALSE;
        rsd.DepthClipEnable = TRUE;
        hr = ctx.device->CreateRasterizerState(&rsd, &ctx.rs_sky);
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
    Vertex v[8] = {{{-ax, 0, 0}, {0, 0, 1}, {cr, cg, cb}, {0, 0}},
                   {{-gap, 0, 0}, {0, 0, 1}, {cr, cg, cb}, {0, 0}},
                   {{gap, 0, 0}, {0, 0, 1}, {cr, cg, cb}, {0, 0}},
                   {{ax, 0, 0}, {0, 0, 1}, {cr, cg, cb}, {0, 0}},
                   {{0, -ay, 0}, {0, 0, 1}, {cr, cg, cb}, {0, 0}},
                   {{0, -gy, 0}, {0, 0, 1}, {cr, cg, cb}, {0, 0}},
                   {{0, gy, 0}, {0, 0, 1}, {cr, cg, cb}, {0, 0}},
                   {{0, ay, 0}, {0, 0, 1}, {cr, cg, cb}, {0, 0}}};
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
        v[n].normal[0] = 0.0f;
        v[n].normal[1] = 0.0f;
        v[n].normal[2] = 1.0f;
        v[n].color[0] = r;
        v[n].color[1] = g;
        v[n].color[2] = b;
        v[n].uv[0] = 0.0f;
        v[n].uv[1] = 0.0f;
        ++n;
        v[n].position[0] = bx;
        v[n].position[1] = by;
        v[n].position[2] = bz;
        v[n].normal[0] = 0.0f;
        v[n].normal[1] = 0.0f;
        v[n].normal[2] = 1.0f;
        v[n].color[0] = r;
        v[n].color[1] = g;
        v[n].color[2] = b;
        v[n].uv[0] = 0.0f;
        v[n].uv[1] = 0.0f;
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
        v[n].normal[0] = 0.0f;
        v[n].normal[1] = 0.0f;
        v[n].normal[2] = 1.0f;
        v[n].color[0] = 0.10f;
        v[n].color[1] = 0.12f;
        v[n].color[2] = 0.17f;
        v[n].uv[0] = 0.0f;
        v[n].uv[1] = 0.0f;
        ++n;
        ndc(bx, by, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].normal[0] = 0.0f;
        v[n].normal[1] = 0.0f;
        v[n].normal[2] = 1.0f;
        v[n].color[0] = 0.10f;
        v[n].color[1] = 0.12f;
        v[n].color[2] = 0.17f;
        v[n].uv[0] = 0.0f;
        v[n].uv[1] = 0.0f;
        ++n;
        ndc(cx, cy, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].normal[0] = 0.0f;
        v[n].normal[1] = 0.0f;
        v[n].normal[2] = 1.0f;
        v[n].color[0] = 0.10f;
        v[n].color[1] = 0.12f;
        v[n].color[2] = 0.17f;
        v[n].uv[0] = 0.0f;
        v[n].uv[1] = 0.0f;
        ++n;
    };
    auto edge = [&](float ax, float ay, float bx, float by) {
        float nx, ny;
        ndc(ax, ay, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].normal[0] = 0.0f;
        v[n].normal[1] = 0.0f;
        v[n].normal[2] = 1.0f;
        v[n].color[0] = 0.55f;
        v[n].color[1] = 0.60f;
        v[n].color[2] = 0.68f;
        v[n].uv[0] = 0.0f;
        v[n].uv[1] = 0.0f;
        ++n;
        ndc(bx, by, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].normal[0] = 0.0f;
        v[n].normal[1] = 0.0f;
        v[n].normal[2] = 1.0f;
        v[n].color[0] = 0.55f;
        v[n].color[1] = 0.60f;
        v[n].color[2] = 0.68f;
        v[n].uv[0] = 0.0f;
        v[n].uv[1] = 0.0f;
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
        v[n].normal[0] = 0.0f;
        v[n].normal[1] = 0.0f;
        v[n].normal[2] = 1.0f;
        v[n].color[0] = cr * 0.35f;
        v[n].color[1] = cg * 0.35f;
        v[n].color[2] = cb * 0.35f;
        v[n].uv[0] = 0.0f;
        v[n].uv[1] = 0.0f;
        ++n;
        ndc(ex, ey, nx, ny);
        v[n].position[0] = nx;
        v[n].position[1] = ny;
        v[n].position[2] = 0.0f;
        v[n].normal[0] = 0.0f;
        v[n].normal[1] = 0.0f;
        v[n].normal[2] = 1.0f;
        v[n].color[0] = cr;
        v[n].color[1] = cg;
        v[n].color[2] = cb;
        v[n].uv[0] = 0.0f;
        v[n].uv[1] = 0.0f;
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

// Objetivo reducido para calidad automática. Se recrea al cambiar tamaño o escala.
// El SRV se desata tras cada blit: seguro recrear aquí.
bool EnsureLowResSize(Context& ctx, int full_w, int full_h, float scale, Error& err) {
    int lw = static_cast<int>(static_cast<float>(full_w) * scale);
    int lh = static_cast<int>(static_cast<float>(full_h) * scale);
    if (lw < 8) lw = 8;
    if (lh < 8) lh = 8;
    if (lw == ctx.low_w && lh == ctx.low_h && ctx.low_rtv != nullptr) return true;
    if (ctx.low_dsv != nullptr) {
        ctx.low_dsv->Release();
        ctx.low_dsv = nullptr;
    }
    if (ctx.low_depth != nullptr) {
        ctx.low_depth->Release();
        ctx.low_depth = nullptr;
    }
    if (ctx.low_srv != nullptr) {
        ctx.low_srv->Release();
        ctx.low_srv = nullptr;
    }
    if (ctx.low_rtv != nullptr) {
        ctx.low_rtv->Release();
        ctx.low_rtv = nullptr;
    }
    if (ctx.low_tex != nullptr) {
        ctx.low_tex->Release();
        ctx.low_tex = nullptr;
    }
    ctx.low_w = 0;
    ctx.low_h = 0;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(lw);
    td.Height = static_cast<UINT>(lh);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = ctx.device->CreateTexture2D(&td, nullptr, &ctx.low_tex);
    if (SUCCEEDED(hr)) hr = ctx.device->CreateRenderTargetView(ctx.low_tex, nullptr, &ctx.low_rtv);
    if (SUCCEEDED(hr))
        hr = ctx.device->CreateShaderResourceView(ctx.low_tex, nullptr, &ctx.low_srv);
    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = static_cast<UINT>(lw);
    dd.Height = static_cast<UINT>(lh);
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(hr)) hr = ctx.device->CreateTexture2D(&dd, nullptr, &ctx.low_depth);
    if (SUCCEEDED(hr))
        hr = ctx.device->CreateDepthStencilView(ctx.low_depth, nullptr, &ctx.low_dsv);
    if (FAILED(hr)) {
        err.set("GPU", "No se pudo crear el objetivo reducido.");
        return false;
    }
    ctx.low_w = lw;
    ctx.low_h = lh;
    return true;
}

// Cielo HDRI de la escena (carga única perezosa). Devuelve true si hay sky listo.
// Fuente: assets/source/<sky>.hdr (Radiance RGBE) decodificado a float.
bool EnsureSky(Context& ctx, const Project& proj, const Scene& scene) {
    if (ctx.sky_attempted) return ctx.sky_srv != nullptr;
    ctx.sky_attempted = true;
    if (scene.sky_asset.empty()) return false;
    ctx.sky_asset = scene.sky_asset;
    std::filesystem::path path =
        proj.root / "assets" / "source" / (scene.sky_asset + ".hdr");
    std::error_code ec;
    std::uintmax_t fsize = std::filesystem::file_size(path, ec);
    constexpr std::uint64_t kMaxHdrBytes = 32ULL * 1024ULL * 1024ULL;
    if (ec || fsize < 32 || fsize > kMaxHdrBytes) {
        std::fprintf(stderr, "Aviso: HDRI '%s' ilegible, sin cielo.\n", scene.sky_asset.c_str());
        return false;
    }
    std::vector<unsigned char> file(static_cast<std::size_t>(fsize));
    std::FILE* f = nullptr;
    if (::fopen_s(&f, path.string().c_str(), "rb") != 0 || f == nullptr) {
        std::fprintf(stderr, "Aviso: HDRI '%s' ilegible, sin cielo.\n", scene.sky_asset.c_str());
        return false;
    }
    std::size_t got = std::fread(file.data(), 1, file.size(), f);
    std::fclose(f);
    if (got != file.size()) {
        std::fprintf(stderr, "Aviso: HDRI '%s' ilegible, sin cielo.\n", scene.sky_asset.c_str());
        return false;
    }
    int w = 0, h = 0, comp = 0;
    float* px =
        stbi_loadf_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &comp, 3);
    if (px == nullptr || w <= 0 || h <= 0 || w > 2048 || h > 2048 ||
        static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 16ULL >
            64ULL * 1024ULL * 1024ULL) {
        if (px != nullptr) stbi_image_free(px);
        std::fprintf(stderr, "Aviso: HDRI '%s' no decodificable o enorme, sin cielo.\n",
                     scene.sky_asset.c_str());
        return false;
    }
    std::vector<float> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U);
    for (std::size_t i = 0, n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h); i < n;
         ++i) {
        rgba[i * 4U] = px[i * 3U];
        rgba[i * 4U + 1U] = px[i * 3U + 1U];
        rgba[i * 4U + 2U] = px[i * 3U + 2U];
        rgba[i * 4U + 3U] = 1.0f;
    }
    stbi_image_free(px);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA tsd{};
    tsd.pSysMem = rgba.data();
    tsd.SysMemPitch = static_cast<UINT>(w) * 16U;
    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = ctx.device->CreateTexture2D(&td, &tsd, &tex);
    if (SUCCEEDED(hr)) hr = ctx.device->CreateShaderResourceView(tex, nullptr, &ctx.sky_srv);
    if (tex != nullptr) tex->Release();
    if (FAILED(hr)) {
        if (ctx.sky_srv != nullptr) {
            ctx.sky_srv->Release();
            ctx.sky_srv = nullptr;
        }
        std::fprintf(stderr, "Aviso: HDRI '%s' no utilizable en GPU, sin cielo.\n",
                     scene.sky_asset.c_str());
        return false;
    }
    return true;
}

HRESULT DrawFrame(Context& ctx, const Project& proj, const Scene& scene, int width, int height,
                  bool vsync) {
    D3D11_VIEWPORT full{};
    full.Width = static_cast<FLOAT>(width);
    full.Height = static_cast<FLOAT>(height);
    full.MaxDepth = 1.0f;
    // Sin SRV enlazado al empezar (el blit del frame anterior lo desata, pero
    // EnsureLowResSize puede recrear low_tex y no debe estar enlazado).
    ID3D11ShaderResourceView* nullsrv0 = nullptr;
    ctx.context->PSSetShaderResources(0, 1, &nullsrv0);
    // Destino de escena: nativo o reducido según calidad automática. El aspect
    // de proyección siempre es el de pantalla completa (el blit estira).
    float scale = (ctx.quality_scale < 1.0f && ctx.quality_scale >= 0.4f) ? ctx.quality_scale
                                                                         : 1.0f;
    ID3D11RenderTargetView* scene_rtv = ctx.target;
    ID3D11DepthStencilView* scene_dsv = ctx.depth;
    int rw = width, rh = height;
    if (scale < 1.0f) {
        Error lerr;
        if (EnsureLowResSize(ctx, width, height, scale, lerr)) {
            scene_rtv = ctx.low_rtv;
            scene_dsv = ctx.low_dsv;
            rw = ctx.low_w;
            rh = ctx.low_h;
        } else if (!ctx.low_warned) {
            ctx.low_warned = true;
            std::fprintf(stderr, "Aviso: calidad automática sin efecto (%s).\n",
                         lerr.message.c_str());
            scale = 1.0f;
        } else {
            scale = 1.0f;
        }
    }
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<FLOAT>(rw);
    vp.Height = static_cast<FLOAT>(rh);
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
    ctx.context->OMSetRenderTargets(1, &scene_rtv, scene_dsv);
    ctx.context->RSSetViewports(1, &vp);
    ctx.context->ClearRenderTargetView(scene_rtv, clear);
    ctx.context->ClearDepthStencilView(scene_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx.context->IASetInputLayout(ctx.layout);
    UINT stride = sizeof(Vertex), off = 0;
    ctx.context->IASetVertexBuffers(0, 1, &ctx.cube_vb, &stride, &off);
    ctx.context->IASetIndexBuffer(ctx.cube_ib, DXGI_FORMAT_R16_UINT, 0);
    ctx.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx.context->VSSetShader(ctx.vs, nullptr, 0);
    ctx.context->PSSetShader(ctx.ps, nullptr, 0);
    ctx.context->VSSetConstantBuffers(0, 1, &ctx.matrices);
    ctx.context->PSSetConstantBuffers(0, 1, &ctx.matrices);
    ctx.context->PSSetSamplers(0, 1, &ctx.sampler);
    float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 4.0f / 3.0f;
    float view[16], proj_m[16];
    bool has_view = ViewMatrix(view, *cam_t);
    bool has_proj = ProjectionMatrix(proj_m, aspect, fov);
    if (!has_view || !has_proj) {
        return ctx.swap->Present(vsync ? 1 : 0, 0);
    }
    if (EnsureSky(ctx, proj, scene)) {
        // Cubo centrado en la cámara y dentro del far (esquinas a ~1732 < 2000):
        // con 1500 las esquinas (2598) las recortaba el far y se veían
        // triángulos del color de fondo al mirar en diagonal.
        float sworld[16], swv[16];
        SceneBuffer ssb{};
        Transform skyt{};
        skyt.position = cam_t->position;
        skyt.scale = {1000.0f, 1000.0f, 1000.0f};
        WorldMatrix(sworld, skyt);
        MatMul(swv, sworld, view);
        MatMul(ssb.wvp, swv, proj_m);
        std::memcpy(ssb.world, sworld, sizeof(sworld));
        ssb.tint[0] = ssb.tint[1] = ssb.tint[2] = ssb.tint[3] = 1.0f;
        ssb.eye_pos[0] = cam_t->position.x;
        ssb.eye_pos[1] = cam_t->position.y;
        ssb.eye_pos[2] = cam_t->position.z;
        ctx.context->IASetInputLayout(ctx.sky_layout);
        ctx.context->IASetVertexBuffers(0, 1, &ctx.cube_vb, &stride, &off);
        ctx.context->IASetIndexBuffer(ctx.cube_ib, DXGI_FORMAT_R16_UINT, 0);
        ctx.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx.context->VSSetShader(ctx.sky_vs, nullptr, 0);
        ctx.context->PSSetShader(ctx.sky_ps, nullptr, 0);
        ctx.context->VSSetConstantBuffers(0, 1, &ctx.matrices);
        ctx.context->PSSetConstantBuffers(0, 1, &ctx.matrices);
        ctx.context->PSSetShaderResources(0, 1, &ctx.sky_srv);
        ctx.context->PSSetSamplers(0, 1, &ctx.sky_sampler);
        ctx.context->RSSetState(ctx.rs_sky);
        ctx.context->UpdateSubresource(ctx.matrices, 0, nullptr, &ssb, 0, 0);
        ctx.context->DrawIndexed(36, 0, 0);
        ctx.context->RSSetState(nullptr);
        // Restaurar pipeline 3D (el bucle de entidades lo espera).
        ctx.context->IASetInputLayout(ctx.layout);
        ctx.context->VSSetShader(ctx.vs, nullptr, 0);
        ctx.context->PSSetShader(ctx.ps, nullptr, 0);
        ctx.context->PSSetSamplers(0, 1, &ctx.sampler);
    }
    for (const auto& e : scene.entities) {
        if (!e.has_mesh) continue;
        float world[16], wv[16];
        SceneBuffer sb{};        ModelCache* model = nullptr;
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
        sb.base_col[0] = sb.base_col[1] = sb.base_col[2] = 1.0f;
        sb.base_col[3] = 1.0f;
        sb.tex_params[0] = sb.tex_params[1] = sb.tex_params[2] = sb.tex_params[3] = 0.0f;
        ID3D11ShaderResourceView* srv = nullptr;
        if (model != nullptr) {
            std::memcpy(sb.base_col, model->base_factor, sizeof(sb.base_col));
            if (model->texture != nullptr) {
                srv = model->texture;
                sb.tex_params[0] = 1.0f;
            }
        }
        ctx.context->PSSetShaderResources(0, 1, &srv);
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
    if (scale < 1.0f) {
        // Blit del objetivo reducido al backbuffer nativo; los overlays van
        // después a resolución completa (nítidos).
        ctx.context->OMSetRenderTargets(1, &ctx.target, nullptr);
        ctx.context->RSSetViewports(1, &full);
        ctx.context->IASetInputLayout(ctx.blit_layout);
        UINT bs = sizeof(BlitVertex), bo = 0;
        ctx.context->IASetVertexBuffers(0, 1, &ctx.blit_vb, &bs, &bo);
        ctx.context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        ctx.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx.context->VSSetShader(ctx.blit_vs, nullptr, 0);
        ctx.context->PSSetShader(ctx.blit_ps, nullptr, 0);
        ctx.context->PSSetShaderResources(0, 1, &ctx.low_srv);
        ctx.context->PSSetSamplers(0, 1, &ctx.sampler);
        ctx.context->Draw(3, 0);
        ID3D11ShaderResourceView* nullsrv = nullptr;
        ctx.context->PSSetShaderResources(0, 1, &nullsrv);
        // Restaurar pipeline 3D para los overlays.
        ctx.context->IASetInputLayout(ctx.layout);
        ctx.context->VSSetShader(ctx.vs, nullptr, 0);
        ctx.context->PSSetShader(ctx.ps, nullptr, 0);
        ctx.context->VSSetConstantBuffers(0, 1, &ctx.matrices);
        ctx.context->PSSetConstantBuffers(0, 1, &ctx.matrices);
        ctx.context->PSSetSamplers(0, 1, &ctx.sampler);
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
            // Calidad automática con histéresis: baja al primer segundo bajo el
            // mínimo; sube tras 3 segundos seguidos con >20 FPS de margen.
            if (cfg.min_fps > 0) {
                if (frames < static_cast<unsigned>(cfg.min_fps)) {
                    if (ctx.quality_idx < 4) {
                        ++ctx.quality_idx;
                        ctx.quality_scale = kQualityScales[ctx.quality_idx];
                    }
                    ctx.quality_up_streak = 0;
                } else if (frames > static_cast<unsigned>(cfg.min_fps) + 20U) {
                    ++ctx.quality_up_streak;
                    if (ctx.quality_up_streak >= 3) {
                        if (ctx.quality_idx > 0) {
                            --ctx.quality_idx;
                            ctx.quality_scale = kQualityScales[ctx.quality_idx];
                        }
                        ctx.quality_up_streak = 0;
                    }
                } else {
                    ctx.quality_up_streak = 0;
                }
            }
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
                          "cam:[%.1f,%.1f,%.1f] rot:[%.2f,%.2f] sel:%s eje:%c res:%d%%",
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
                          ctx.locked_axis != 0 ? ctx.locked_axis : '-',
                          static_cast<int>(ctx.quality_scale * 100.0f + 0.5f));
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
    ClearInfoChildren(ctx);
    if (ctx.info_hwnd != nullptr) {
        ::DestroyWindow(ctx.info_hwnd);
        ctx.info_hwnd = nullptr;
    }
    if (ctx.info_f_title != nullptr) {
        ::DeleteObject(ctx.info_f_title);
        ctx.info_f_title = nullptr;
    }
    if (ctx.info_f_head != nullptr) {
        ::DeleteObject(ctx.info_f_head);
        ctx.info_f_head = nullptr;
    }
    if (ctx.info_f_body != nullptr) {
        ::DeleteObject(ctx.info_f_body);
        ctx.info_f_body = nullptr;
    }
    if (ctx.info_f_json != nullptr) {
        ::DeleteObject(ctx.info_f_json);
        ctx.info_f_json = nullptr;
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
