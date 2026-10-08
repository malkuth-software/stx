"""Create an explicitly experimental decoder projection; shipping sources stay unchanged."""
import argparse
from pathlib import Path
import shutil


def replace_once(path: Path, old: str, new: str) -> None:
    contents: str = path.read_text(encoding='utf-8')
    if contents.count(old) != 1:
        raise RuntimeError('Projection anchor changed: ' + str(path))
    contents = contents.replace(old, new)
    path.write_text(contents, encoding='utf-8')


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    arguments: argparse.Namespace = parser.parse_args()
    source: Path = arguments.source.resolve()
    output: Path = arguments.output.resolve()
    if output == source or source in output.parents:
        raise RuntimeError('Projection must be outside the shipping codec directory')
    output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source / 'src', output / 'src', dirs_exist_ok=True)
    header: Path = output / 'src/packet.hpp'
    implementation: Path = output / 'src/packet.cpp'
    replace_once(header, '#include "synthesis.hpp"',
                 '#include "synthesis.hpp"\n#include "bfft_imdct.hpp"')
    replace_once(header, '    Butterfly butterfly{scalar_butterfly};',
                 '    Butterfly butterfly{scalar_butterfly};\n'
                 '    std::array<std::unique_ptr<experiment::BfftImdct>, 2> experimental_transforms{};')
    replace_once(implementation, '    workspace.butterfly = select_butterfly(synthesis);',
                 '    workspace.butterfly = select_butterfly(synthesis);\n'
                 '    for (unsigned int index = 0; index < 2; ++index) {\n'
                 '        workspace.experimental_transforms[index] = '
                 'std::make_unique<experiment::BfftImdct>(setup.identification.blocks[index], true);\n'
                 '    }')
    replace_once(implementation,
                 'inverse_mdct(setup.transforms[mode.large ? 1 : 0], spectrum, time, workspace.real, workspace.imaginary, workspace.butterfly);',
                 'experiment::BfftImdct& transform = *workspace.experimental_transforms[mode.large ? 1 : 0];\n'
                 '            transform.execute(spectrum, time);')


if __name__ == '__main__':
    main()
