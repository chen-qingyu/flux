from importlib import import_module

run = import_module(".lib", __name__).run

__all__ = ["run"]
