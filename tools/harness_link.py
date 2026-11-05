#!/usr/bin/env python3
"""Where stx's tools find template_harness's checkers.

harness.lock at the root of this checkout pins the harness commit. CI checks
it out inside the checkout, at ./template_harness (.github/actions/
harness-checkout with product-path: .); a local workspace may hold it beside
the checkout instead (<workspace>/stx and <workspace>/template_harness).

Importing this module puts the first of those harness/ directories that
exists first on sys.path, so a tool here imports the harness's checkers by
name (tools/check_stx_skia_style.py imports check_house_style), and fails
with a message naming both places when neither exists.
"""
from __future__ import annotations
from pathlib import Path
import sys

CHECKOUT: Path = Path(__file__).resolve().parents[1]
CANDIDATES: tuple[Path, ...] = (CHECKOUT / 'template_harness' / 'harness',
                                CHECKOUT.parent / 'template_harness' / 'harness')


def harness_directory() -> Path:
    """The pinned harness's harness/ directory; raises when it is not checked out."""
    candidate: Path
    for candidate in CANDIDATES:
        if (candidate / 'check_house_style.py').is_file():
            return candidate
    raise RuntimeError('template_harness is not checked out at ' + str(CANDIDATES[0].parent) + ' or '
                       + str(CANDIDATES[1].parent) + ' (harness.lock names the commit)')


_directory: str = str(harness_directory())
if _directory not in sys.path:
    sys.path.insert(0, _directory)
