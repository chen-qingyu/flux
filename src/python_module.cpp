#include <pybind11/pybind11.h>

#include "core/app.hpp"

namespace py = pybind11;

PYBIND11_MODULE(_native, module)
{
    module.doc() = "Python SDK for the flux simulator engine.";
    module.def(
        "run",
        &flux::run,
        py::arg("file"),
        py::arg("seed") = 42,
        "Run the simulation engine and write reports into output/.");
}