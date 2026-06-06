"""bachlib: shared Python tooling for the Composer closure harness.

This package consolidates the helpers that drive ``bach_cli`` across seeds,
score generated JSON, predict byte-stable carrier layouts, and combine the
closure gates. The mirror constants in :mod:`bachlib.mirror` are load-bearing
drift guards: they must stay byte-identical with the C++ harness fixtures in
``src/composer/harness_fixture.cpp``.

The public surface of every submodule is aggregated here (leaf-first to avoid
import cycles) so consumers and tests can use ``import bachlib as rpc`` and
reach the constants, predictors, and gate helpers through one namespace.
"""

from bachlib.common import *  # noqa: F401,F403  (re-export shared core helpers)
from bachlib.phases import *  # noqa: F401,F403  (re-export seed/phase tables)
from bachlib.mirror import *  # noqa: F401,F403  (re-export byte-stable mirrors)
from bachlib.predictors import *  # noqa: F401,F403  (re-export structural predictors)

# Pull only the engine names tests / consumers reach through the package
# namespace; main() / the CLI are intentionally not aggregated here.
from bachlib.closure import compute_passed, output_paths  # noqa: F401
