"""Make a test-only binary32 copy of the pinned, unoptimized profile algorithm.

Keep the enumeration of active faces and every boundary-cost recomputation.
Only types, floating literals, machine epsilon and largest finite value change.
The runtime library does not include or depend on this generated source.
"""
import hashlib
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_bytes()
expected = '6690afe08274cbaa0c6cb52787b4450f8e32b12f3305143aec7abfb8d1e4c8f0'
if hashlib.sha256(source).hexdigest() != expected:
    raise SystemExit('Pinned CONV source mismatch')
text = source.decode()
text = text[text.index('namespace paint {'):text.index('    static void tail_weights(')]
text = text.replace('namespace paint {', 'namespace float_reference {')
text = re.sub(r'\bdouble\b', 'float', text)
text = text.replace('2.2204460492503130808472633361816e-16', '1.1920928955078125e-7')
text = text.replace('1.7976931348623157e308', '3.4028234663852886e38')
text = re.sub(r'(?<![\w.])(\d+\.\d*(?:e[-+]?\d+)?)(?![\w.])', r'\1F', text)
Path(sys.argv[2]).write_text('#include <algorithm>\n#include <cmath>\n#include <vector>\n' + text + '\n};\n}\n}\n')
