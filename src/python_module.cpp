#include <pybind11/pybind11.h>

#include "core/app.hpp"

namespace py = pybind11;

PYBIND11_MODULE(lib, module)
{
    module.doc() = "Python SDK for the flux simulator engine.";

    module.def(
        "run",
        &flux::run,
        py::arg("model_name"),
        py::arg("model_content"),
        py::arg("output_dir") = "output",
        py::arg("external_dir") = "data/external",
        py::arg("random_seed") = 42,
        "Run simulation from a BPMN content string, write CSV reports to output_dir.");
}
