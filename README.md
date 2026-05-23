# Bokken

A cross-platform 2D game engine with a custom asset pipeline, a
component-based scene model, and a TypeScript scripting runtime built on
QuickJS-NG.

Bokken is for developers who like owning their tools: the asset packer, the
scripting bridge, the render pipeline, and the command-line workflow are all
first-party rather than glued together from black boxes.

## Features

- **Component-based scenes** — GameObjects compose transforms, sprites,
  animations, particles, lights, distortion effects, and user-authored
  behaviours.
- **TypeScript scripting** — full type definitions and JSX-based UI, compiled
  to QuickJS-NG bytecode. Hot reload in development.
- **Box2D v3 physics** — six collider types and eight joint types, with a full
  contact and sensor event system surfaced to scripts.
- **Audio** — a lock-free multi-channel mixer with built-in DSP (gain,
  high-pass, low-pass, compressor, delay, distortion, reverb) and a C++ plugin
  path for custom effects; spatial sources do distance attenuation, panning,
  and Doppler via an `AudioListener2D`.
- **2D lighting** — point, spot, and directional lights with soft shadows,
  cookies, and animation envelopes.
- **JSX UI layer** — flexbox layout, `useState`/`useEffect` hooks, and inline
  event handling.
- **Particle system** — CPU emitter with gravity, damping, color/size easing,
  and blend modes.
- **Asset packs** — assets bundled into archives and mounted through a virtual
  filesystem (PhysFS).
- **bokken-cli** — a single command-line tool for building, running, watching,
  packing, and diagnosing projects.

## Getting started

You don't clone this engine directly to make a game. Start from the project
template, which pulls the engine in automatically via CMake's `FetchContent`:

```bash
cookiecutter gh:bokken-technologies/template
cd my-project
make build      # First build fetches the engine and builds bokken-cli
make install    # Put bokken-cli on your PATH
bokken-cli run  # Build and launch
```

See the template's README for the full project workflow and `bokken-cli`
command reference.

## Building the engine directly

Cloning this repository is only needed to work on the engine itself or to run
its tests:

```bash
git clone git@github.com:bokken-technologies/bokken.git
cd bokken
make
```

## Contributing

Contributions are welcome. Before opening a pull request, read
[`CONTRIBUTING.md`](CONTRIBUTING.md); all contributors must sign the Contributor
License Agreement, which is handled automatically by a bot on your first PR.

1. Fork it (<https://github.com/bokken-technologies/bokken/fork>)
2. Create a feature branch (`git checkout -b my-feature`)
3. Commit your changes (`git commit -am "Add my feature"`)
4. Push the branch (`git push origin my-feature`)
5. Open a pull request

## Contributors

- [Giorgi Kavrelishvili](https://github.com/grkek) — creator and maintainer
