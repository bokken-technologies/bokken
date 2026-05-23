#pragma once

#include "Component.hpp"
#include <memory>
#include <vector>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <algorithm>

namespace Bokken
{
    namespace GameObject
    {
        // The core scene entity. Dimension-agnostic — just a named bag of Components
        // with parent/child relationships. 2D vs 3D is determined entirely by which
        // components are attached.
        class Base
        {
        public:
            std::string name;

            explicit Base(std::string name = "New GameObject")
                : name(std::move(name))
            {
                // Register in the name index for O(1) find().
                // If duplicate names exist, the latest one wins (same as the
                // old linear scan which returned the first match — creation
                // order means latest push_back is at the end, but find()
                // scanned front-to-back. This preserves "first wins" by
                // only inserting if the name isn't already present).
                s_nameIndex.try_emplace(this->name, this);
            }

            template <typename T>
            T &addComponent()
            {
                static_assert(std::is_base_of_v<Component, T>,
                              "T must derive from Component");
                return addComponentInternal<T>();
            }

            // Variant that constructs the component and registers it
            // without invoking onAttach. The caller must call .onAttach()
            // on the returned component itself once it has finished
            // applying any pre-attach configuration. Used by the JS
            // bindings for colliders, where the component's shape
            // geometry is baked into Box2D at attach time and therefore
            // user-supplied size/radius/etc. must land on the component
            // before onAttach runs.
            template <typename T>
            T &addComponentDeferred()
            {
                static_assert(std::is_base_of_v<Component, T>,
                              "T must derive from Component");
                auto comp = std::make_unique<T>();
                comp->gameObject = this;
                T *raw = comp.get();
                m_components[std::type_index(typeid(T))] = std::move(comp);
                return *raw;
            }

            // Attach a JS-driven behaviour. The GameObject takes
            // ownership; the returned raw pointer is stable for the
            // lifetime of the GameObject (or until the behaviour is
            // explicitly removed). onAttach is *not* called here —
            // the caller is responsible for invoking it after wiring
            // up the JS instance, mirroring the addComponentDeferred
            // contract for colliders.
            //
            // Stored separately from m_components because multiple
            // JS behaviours commonly share the same C++ type
            // (JSBehaviour) which would collide in the type-keyed
            // map. The vector preserves attach order.
            Component *addBehaviour(std::unique_ptr<Component> behaviour)
            {
                if (!behaviour)
                    return nullptr;
                behaviour->gameObject = this;
                Component *raw = behaviour.get();
                m_jsBehaviours.push_back(std::move(behaviour));
                return raw;
            }

            template <typename T>
            T *getComponent()
            {
                auto it = m_components.find(std::type_index(typeid(T)));
                if (it == m_components.end())
                    return nullptr;
                return static_cast<T *>(it->second.get());
            }

            template <typename T>
            bool hasComponent() const
            {
                return m_components.find(std::type_index(typeid(T))) != m_components.end();
            }

            template <typename T>
            void removeComponent()
            {
                auto it = m_components.find(std::type_index(typeid(T)));
                if (it != m_components.end())
                {
                    it->second->onDestroy();
                    m_components.erase(it);
                }
            }

            // Walk every component currently attached. Used by subsystems
            // that need to broadcast to "any component that cares" rather
            // than a known concrete type — for example, Behaviour::dispatchXxx
            // forwarding physics events to user-defined script subclasses.
            // The visitor receives the raw Component pointer; downstream
            // dynamic_cast filters to the desired derived class.
            template <typename Fn>
            void forEachComponent(Fn &&fn)
            {
                for (auto &[key, comp] : m_components)
                    fn(comp.get());
                for (auto &b : m_jsBehaviours)
                    fn(b.get());
            }

            void setParent(Base *parent)
            {
                if (m_parent)
                {
                    auto &siblings = m_parent->m_children;
                    siblings.erase(
                        std::remove(siblings.begin(), siblings.end(), this),
                        siblings.end());
                }
                m_parent = parent;
                if (m_parent)
                    m_parent->m_children.push_back(this);
            }

            Base *getParent() const { return m_parent; }
            std::vector<Base *> getChildren() const { return m_children; }

            void update(float dt)
            {
                for (auto &[key, comp] : m_components)
                    if (comp->enabled)
                        comp->update(dt);
                for (auto &b : m_jsBehaviours)
                    if (b->enabled)
                        b->update(dt);
            }

            void fixedUpdate(float dt)
            {
                for (auto &[key, comp] : m_components)
                    if (comp->enabled)
                        comp->fixedUpdate(dt);
                for (auto &b : m_jsBehaviours)
                    if (b->enabled)
                        b->fixedUpdate(dt);
            }

            static void destroy(Base *obj) { obj->m_pendingDestroy = true; }

            /**
             * When true, the engine automatically marks this object for
             * destruction once every component reports isIdle(). Checked
             * once per frame before flushDestroyed(). Designed for
             * fire-and-forget objects like explosions, laser segments,
             * and one-shot particle effects.
            */
            bool destroyWhenIdle = false;

            /**
             * Check if all components report idle. Returns false if the
             * object has no components (empty objects are not considered
             * idle — they're likely being set up).
            */
            bool isIdle() const
            {
                if (m_components.empty())
                    return false;

                for (const auto &[key, comp] : m_components)
                {
                    if (!comp->isIdle())
                        return false;
                }
                return true;
            }

            /**
             * Walks all objects with destroyWhenIdle == true and marks
             * them for destruction if all their components are idle.
             * Called once per frame from the Loop, before flushDestroyed().
            */
            static void sweepIdle()
            {
                for (auto &o : s_objects)
                {
                    if (o->destroyWhenIdle && !o->m_pendingDestroy && o->isIdle())
                        o->m_pendingDestroy = true;
                }
            }

            // O(1) lookup by name via hash map.
            static Base *find(const std::string &name)
            {
                auto it = s_nameIndex.find(name);
                if (it != s_nameIndex.end() && !it->second->m_pendingDestroy)
                    return it->second;
                return nullptr;
            }

            static void flushDestroyed()
            {
                // Remove from name index before erasing from s_objects.
                for (auto &o : s_objects)
                {
                    if (!o->m_pendingDestroy)
                        continue;

                    // Only remove from index if this pointer is the indexed one
                    // (handles duplicate names correctly).
                    auto it = s_nameIndex.find(o->name);
                    if (it != s_nameIndex.end() && it->second == o.get())
                        s_nameIndex.erase(it);

                    // Detach from parent.
                    if (o->m_parent)
                    {
                        auto &siblings = o->m_parent->m_children;
                        siblings.erase(
                            std::remove(siblings.begin(), siblings.end(), o.get()),
                            siblings.end());
                    }

                    for (auto &[k, c] : o->m_components)
                        c->onDestroy();
                    for (auto &b : o->m_jsBehaviours)
                        b->onDestroy();
                }

                s_objects.erase(
                    std::remove_if(s_objects.begin(), s_objects.end(),
                                   [](const std::unique_ptr<Base> &o)
                                   { return o->m_pendingDestroy; }),
                    s_objects.end());
            }

            static inline std::vector<std::unique_ptr<Base>> s_objects;
            static inline std::unordered_map<std::string, Base *> s_nameIndex;

        private:
            std::unordered_map<std::type_index,
                               std::unique_ptr<Component>>
                m_components;

            // JS behaviours are stored separately from the type-keyed
            // m_components map because multiple instances of the same
            // C++ JSBehaviour class can co-exist on one GameObject —
            // each one wraps a different JS class. The type_index map
            // collapses by C++ type, so we'd lose all but the last.
            // The vector preserves attach order; ticks walk it after
            // the component map so native components run first.
            std::vector<std::unique_ptr<Component>> m_jsBehaviours;

            std::vector<Base *> m_children;
            Base *m_parent = nullptr;
            bool m_pendingDestroy = false;

            template <typename T>
            T &addComponentInternal()
            {
                auto comp = std::make_unique<T>();
                comp->gameObject = this;
                comp->onAttach();
                T *raw = comp.get();
                m_components[std::type_index(typeid(T))] = std::move(comp);
                return *raw;
            }
        };
    }
}