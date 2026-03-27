# flux Python SDK

Python bindings for the flux BPMN simulator.

The package exposes a single entrypoint:

- `flux.run(file, seed=42)`

It reads a BPMN file, runs the simulator, and writes CSV reports into the `output/` directory.
