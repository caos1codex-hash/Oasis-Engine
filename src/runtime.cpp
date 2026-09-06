#include "runtime.hpp"

#include <cmath>

namespace oasis {
namespace {

constexpr float kGravity = 9.8f;
constexpr float kUnitLmn[3] = {-0.5f, -0.5f, -0.5f};
constexpr float kUnitLmx[3] = {0.5f, 0.5f, 0.5f};

// Un paso físico h: gravedad + integración + colisiones AABB inelásticas.
// Estático = Collider sin RigidBody. Todo secuencial en orden de entidades.
void StepPhysics(Scene& scene, float h) {
    // 1. Gravedad + integración (solo con Transform; sin él no hay dónde mover).
    for (auto& e : scene.entities) {
        if (!e.has_rigidbody || !e.has_transform) continue;
        if (e.rigidbody.use_gravity) e.rigidbody.velocity.y -= kGravity * h;
        e.transform.position.x += e.rigidbody.velocity.x * h;
        e.transform.position.y += e.rigidbody.velocity.y * h;
        e.transform.position.z += e.rigidbody.velocity.z * h;
    }
    // 2. Contactos: cada dinámico contra colisionadores anteriores (estáticos y
    // ya resueltos). Eje de mínima penetración; restitución 0.
    for (std::size_t di = 0; di < scene.entities.size(); ++di) {
        Entity& d = scene.entities[di];
        if (!d.has_rigidbody || !d.has_collider || !d.has_transform) continue;
        float dmn[3], dmx[3];
        BoxWorldAABB(kUnitLmn, kUnitLmx, d.transform, dmn, dmx);
        for (std::size_t oi = 0; oi < scene.entities.size(); ++oi) {
            if (oi == di) continue;
            const Entity& o = scene.entities[oi];
            if (!o.has_collider || !o.has_transform) continue;
            // Dinámico-dinámico: solo el de menor índice resuelve (evita doble
            // conteo); el otro lo verá en su turno con posiciones actualizadas.
            if (o.has_rigidbody && oi > di) continue;
            float omn[3], omx[3];
            BoxWorldAABB(kUnitLmn, kUnitLmx, o.transform, omn, omx);
            float pen[3];
            bool overlap = true;
            for (int a = 0; a < 3; ++a) {
                float lo = dmn[a] > omn[a] ? dmn[a] : omn[a];
                float hi = dmx[a] < omx[a] ? dmx[a] : omx[a];
                pen[a] = hi - lo;
                if (pen[a] <= 0.0f) overlap = false;
            }
            if (!overlap) continue;
            int axis = 0;
            if (pen[1] < pen[axis]) axis = 1;
            if (pen[2] < pen[axis]) axis = 2;
            float dc = (dmn[axis] + dmx[axis]) * 0.5f;
            float oc = (omn[axis] + omx[axis]) * 0.5f;
            float sign = (dc >= oc) ? 1.0f : -1.0f;
            float push = pen[axis] * sign;
            float* dp = (axis == 0) ? &d.transform.position.x
                        : (axis == 1) ? &d.transform.position.y
                                      : &d.transform.position.z;
            float* dv = (axis == 0) ? &d.rigidbody.velocity.x
                        : (axis == 1) ? &d.rigidbody.velocity.y
                                      : &d.rigidbody.velocity.z;
            if (o.has_rigidbody) {
                // Ambas dinámicas: reparto por masa inversa + frenado mutuo.
                Entity& m = scene.entities[oi];
                float im_d = 1.0f / d.rigidbody.mass;
                float im_o = (m.rigidbody.mass > 0.0f) ? 1.0f / m.rigidbody.mass : 0.0f;
                float im_sum = im_d + im_o;
                if (im_sum > 0.0f) {
                    *dp += push * (im_d / im_sum);
                    float* op = (axis == 0) ? &m.transform.position.x
                                : (axis == 1) ? &m.transform.position.y
                                              : &m.transform.position.z;
                    *op -= push * (im_o / im_sum);
                }
                float* ov = (axis == 0) ? &m.rigidbody.velocity.x
                            : (axis == 1) ? &m.rigidbody.velocity.y
                                          : &m.rigidbody.velocity.z;
                if (*dv * sign < 0.0f) *dv = 0.0f;
                if (*ov * sign > 0.0f) *ov = 0.0f;
            } else {
                *dp += push;
                if (*dv * sign < 0.0f) *dv = 0.0f;
            }
            BoxWorldAABB(kUnitLmn, kUnitLmx, d.transform, dmn, dmx);
        }
        // Reposo: bajo el umbral se duerme para no temblar sobre el apoyo.
        float sp2 = d.rigidbody.velocity.x * d.rigidbody.velocity.x +
                    d.rigidbody.velocity.y * d.rigidbody.velocity.y +
                    d.rigidbody.velocity.z * d.rigidbody.velocity.z;
        if (sp2 < 0.0001f) {
            d.rigidbody.velocity.x = d.rigidbody.velocity.y = d.rigidbody.velocity.z = 0.0f;
        }
    }
}

}  // namespace
bool Runtime::init(Scene* s, Error& err) {
    err.clear();
    if (s == nullptr) {
        err.set("INVALID_ARG", "Runtime requiere una escena.");
        return false;
    }
    scene = s;
    tick_count = 0;
    elapsed_seconds = 0.0;
    initialized = true;
    return true;
}

bool Runtime::update(double delta_seconds, Error& err) {
    err.clear();
    if (!initialized || scene == nullptr) {
        err.set("INVALID_ARG", "Runtime no inicializado.");
        return false;
    }
    if (std::isfinite(delta_seconds) == 0 || delta_seconds < 0.0 || delta_seconds > 10.0) {
        err.set("INVALID_ARG", "Delta de runtime inválido.");
        return false;
    }
    // Física en substeps de <=1/60 para estabilidad con dt grande (ventana).
    // Headless usa 1/60: un solo substep, determinista. Con simulate=false
    // (edición en ventana) el tick avanza pero la escena no se mueve.
    double remaining = delta_seconds;
    while (remaining > 1e-9) {
        double h = remaining > 1.0 / 60.0 ? 1.0 / 60.0 : remaining;
        if (simulate) StepPhysics(*scene, static_cast<float>(h));
        remaining -= h;
    }
    ++tick_count;
    elapsed_seconds += delta_seconds;
    if (std::isfinite(elapsed_seconds) == 0) {
        err.set("INTERNAL", "Tiempo de runtime fuera de rango.");
        return false;
    }
    return true;
}

void Runtime::shutdown() {
    initialized = false;
    scene = nullptr;
    // tick_count/elapsed se conservan para diagnóstico post-run
}

}  // namespace oasis
