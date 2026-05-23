#pragma once

#include "../Signal.hpp"

namespace Bokken
{
    namespace Audio
    {
        namespace Effects
        {
            /**
             * Abstract base for all audio effects.
             *
             * Derive from this and implement process() to define a new effect.
             * The engine calls process() only when enabled is true.
             *
             * Signal flow:
             *   Signal in → process() → Signal out (same buffer, mutated in-place)
            */
            class Base
            {
            public:
                bool enabled = true;

                virtual ~Base() = default;

                /**
                 * Transform the signal in-place.
                 * Called once per audio frame by the Channel that owns this effect.
                 * Do all DSP work here — do not allocate memory inside this call.
                */
                virtual void process(Signal &signal) = 0;

                Base &bypass()
                {
                    enabled = false;
                    return *this;
                }
                Base &enable()
                {
                    enabled = true;
                    return *this;
                }

                // Non-copyable — effects own native DSP state (ring buffers, filter state, etc.)
                Base(const Base &) = delete;
                Base &operator=(const Base &) = delete;

            protected:
                Base() = default;
            };
        }
    }
}
