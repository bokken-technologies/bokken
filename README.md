# Bokken Engine

Bokken is a high-performance, cross-platform 2D game engine built with a focus on systems architecture, custom tooling, and a lightweight scripting runtime.

Designed for developers who prefer "building the tools" over using black-box solutions, Bokken features a custom asset pipeline, a component-based architecture, and a specialized scripting integration using QuickJS-NG.

## Features

- **Component-based architecture** — GameObjects with transforms, sprites, animations, particles, distortion effects, and user-authored behaviours
- **Box2D v3 physics** — six collider types, eight joint types, full contact and sensor event system
- **Lock-free audio mixer** — multi-channel routing, built-in DSP effects (gain, EQ, compressor, delay, distortion, reverb), and a custom-effect plugin path for user-authored DSP in C++
- **Spatial audio** — distance attenuation, panning, and Doppler driven by an `AudioListener2D` component
- **TypeScript scripting** — full type definitions, JSX-based UI, bytecode compilation via QuickJS-NG
- **Asset packs** — bundled assets with virtual filesystem mounting via PhysFS
- **Particle system** — CPU emitter with gravity, damping, color/size easing, and blend modes
- **JSX UI layer** — flex layout, hooks (`useState`, `useEffect`), and inline event handling

## Installation

```bash
git clone git@github.com:bokken-technologies/bokken.git
cd bokken
make
```

## Usage

TODO: Write usage instructions here.

## Development

TODO: Write development instructions here.

## Contributing

Contributions are welcome. Before submitting a pull request, please read [`CONTRIBUTING.md`](CONTRIBUTING.md) — in particular, all contributors are required to sign the Contributor License Agreement (CLA). This is automated via a GitHub bot when you open your first pull request.

The standard fork-and-PR flow:

1. Fork it (<https://github.com/bokken-technologies/bokken/fork>)
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Add some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create a new Pull Request

## Contributors

- [Giorgi Kavrelishvili](https://github.com/grkek) — creator and maintainer
