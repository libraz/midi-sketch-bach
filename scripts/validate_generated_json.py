#!/usr/bin/env python3
"""Path-stable wrapper for bachlib.schema (invoked by C++ schema_validation_test)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bachlib.schema import main

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
