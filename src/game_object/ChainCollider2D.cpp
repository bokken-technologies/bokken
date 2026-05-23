#include "ChainCollider2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        b2ShapeId ChainCollider2D::createShape(b2BodyId body, const b2ShapeDef &def)
        {
            auto &world = Bokken::Physics::World::get();

            if (points.size() < 2)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[ChainCollider2D] need at least 2 points, have %zu",
                             points.size());
                return b2_nullShapeId;
            }

            std::vector<b2Vec2> verts;
            verts.reserve(points.size());
            for (const auto &p : points)
                verts.push_back(world.pxToB2(p));

            b2ChainDef cdef = b2DefaultChainDef();
            cdef.points = verts.data();
            cdef.count = static_cast<int>(verts.size());
            cdef.isLoop = loop;
            cdef.userData = this;
            cdef.filter.categoryBits = categoryBits;
            cdef.filter.maskBits = maskBits;
            cdef.filter.groupIndex = groupIndex;

            // v3 takes a single material per chain rather than per
            // segment. The Collider2D base values are honoured.
            b2SurfaceMaterial mat = b2DefaultSurfaceMaterial();
            mat.friction = friction;
            mat.restitution = restitution;
            mat.tangentSpeed = tangentSpeed;
            cdef.materials = &mat;
            cdef.materialCount = 1;

            m_chain = b2CreateChain(body, &cdef);
            if (B2_IS_NULL(m_chain))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[ChainCollider2D] b2CreateChain failed");
                return b2_nullShapeId;
            }

            // Chain colliders don't have a single owning b2ShapeId — the
            // chain spawns one per segment. m_shape stays null, and
            // Collider2D::onDestroy will skip its b2DestroyShape branch
            // because of the null check.
            return b2_nullShapeId;
        }

        void ChainCollider2D::onDestroy()
        {
            if (B2_IS_NON_NULL(m_chain))
            {
                if (b2Chain_IsValid(m_chain))
                    b2DestroyChain(m_chain);
                m_chain = b2_nullChainId;
            }

            // Defer the rest of cleanup (the body if we own it) to the base.
            Collider2D::onDestroy();
        }

    }
}
