#include "ShadowCaster2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        void ShadowCaster2D::onAttach()
        {
            m_registryIndex = static_cast<int>(s_all.size());
            s_all.push_back(this);
        }

        void ShadowCaster2D::onDestroy()
        {
            if (m_registryIndex < 0)
                return;
            // O(1) swap-and-pop. The component swapped into our slot
            // updates its registry index so subsequent destroys
            // continue to work in constant time. Same pattern as
            // Light2D::onDestroy.
            const size_t idx = static_cast<size_t>(m_registryIndex);
            const size_t last = s_all.size() - 1;
            if (idx != last)
            {
                s_all[idx] = s_all[last];
                s_all[idx]->m_registryIndex = static_cast<int>(idx);
            }
            s_all.pop_back();
            m_registryIndex = -1;
        }

        void ShadowCaster2D::emit(std::vector<Renderer::Lighting::ShadowSegment> &out,
                                  const Renderer::Lighting::WorldToScreen &w2s) const
        {
            if (!castsShadow)
                return;
            if (outline.size() < 2)
                return;
            if (!gameObject)
                return;

            const Transform2D *t = gameObject->getComponent<Transform2D>();
            if (!t)
                return;

            // Cache trig once per caster — the rotation applies
            // identically to every vertex of the outline. Rotation
            // is in degrees per Transform2D convention.
            const float radians = t->rotation * 3.14159265358979323846f / 180.0f;
            const float cosR = std::cos(radians);
            const float sinR = std::sin(radians);

            // Local outline → world space → render-pixel space. Done
            // in one shot so the per-vertex cost stays a handful of
            // muladds. The lighting fragment shader and the shadow
            // rasterizer both expect endpoints in the same pixel
            // coordinate system the light positions live in (post-
            // Light2D::snapshot conversion); emitting world units
            // here would mismatch them and shadows would either fall
            // in the wrong direction or fail to clip rays at all.
            auto toScreen = [&](glm::vec2 local) {
                // Local-space scale, then rotate, then translate to
                // world. Matches Transform2D::getMatrix's order so a
                // caster outline transforms in lockstep with any
                // sibling Sprite2D drawn through the same Transform2D.
                glm::vec2 scaled(local.x * t->scale.x, local.y * t->scale.y);
                glm::vec2 rotated(scaled.x * cosR - scaled.y * sinR,
                                  scaled.x * sinR + scaled.y * cosR);
                glm::vec2 world = t->position + rotated;
                return w2s.apply(world);
            };

            // Walk every edge in the implicitly-closed outline. The
            // (i, i+1) iteration handles the n-1 interior edges; the
            // final wrap-around edge (n-1 → 0) is emitted after the
            // loop so the loop body itself stays the simple "edge
            // between consecutive vertices" form.
            const size_t n = outline.size();
            out.reserve(out.size() + n);

            const auto isDegenerate = [](const glm::vec2 &a, const glm::vec2 &b) {
                // Zero-length segments produce nan when normalised in
                // the shadow rasterizer. We drop them at emit time so
                // downstream code never has to think about it.
                // 1e-6 in pixel space is sub-pixel; anything tighter
                // than that is the same point as far as the lighting
                // math cares.
                const glm::vec2 d = b - a;
                return (std::abs(d.x) < 1e-6f && std::abs(d.y) < 1e-6f);
            };

            glm::vec2 prev = toScreen(outline[0]);
            for (size_t i = 1; i < n; ++i)
            {
                glm::vec2 next = toScreen(outline[i]);
                if (!isDegenerate(prev, next))
                    out.push_back({prev, next});
                prev = next;
            }

            // Closing edge: last vertex back to first. Re-fetch the
            // first vertex's transformed coords rather than caching
            // — the cache would buy nothing because n-1 transforms
            // already happened in the loop and the JIT can keep the
            // result in a register for one call. Readability wins.
            glm::vec2 first = toScreen(outline[0]);
            if (!isDegenerate(prev, first))
                out.push_back({prev, first});
        }

    }
}