# Bokken

A cross-platform 2D game engine with a custom asset pipeline, a
component-based scene model, and a TypeScript scripting runtime built on
QuickJS-NG.

Bokken is for developers who like owning their tools: the asset packer, the
scripting bridge, the render pipeline, and the command-line workflow are all
first-party rather than glued together from black boxes.

It features component-based scenes, TypeScript scripting with QuickJS-NG compilation, Box2D v3 physics, a multi-channel audio mixer with DSP, dynamic 2D lighting with soft shadows, a JSX-based UI layer with hooks, a CPU particle system, PhysFS virtual asset archives, and a comprehensive CLI tool for building and debugging projects.

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
