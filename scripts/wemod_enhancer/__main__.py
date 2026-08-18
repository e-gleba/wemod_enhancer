"""Entry point for `python -m wemod_enhancer` and direct directory runs.

Running the package directory directly (`python bin/wemod_enhancer`) puts
the package directory itself on sys.path, not its parent — the absolute
import below would then fail. Prepending the parent directory makes both
invocation styles work.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from wemod_enhancer import cli  # noqa: E402

if __name__ == "__main__":
    cli()
