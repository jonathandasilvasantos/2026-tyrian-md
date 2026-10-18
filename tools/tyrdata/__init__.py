"""Readers and Genesis converters for the Tyrian 2.1 PC data (agents.md §2).

Formats are documented in docs/data_formats.md; each reader cites the
OpenTyrian function it mirrors.
"""
import os

DATA_DIR = os.environ.get(
    "TYRIAN_DATA", os.path.join(os.path.dirname(__file__), "..", "..", "tyrian-data"))


def data_path(name):
    return os.path.join(DATA_DIR, name)
