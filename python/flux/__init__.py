from importlib import import_module

run = import_module("._native", __name__).run

__all__ = ["run"]
