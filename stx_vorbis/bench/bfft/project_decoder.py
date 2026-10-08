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
    replace_once(output / 'src/setup.hpp', '#include "memory.hpp"',
                 '#include "memory.hpp"\n#include "bfft/bounded_imdct.hpp"')
    replace_once(output / 'src/setup.hpp', '    std::array<double, 256> inverse_db{};',
                 '    std::array<double, 256> inverse_db{};\n'
                 '    std::array<std::optional<experiment::BfftPlan>, 2> bfft_plans{};')
    # Original transform type stays intact for the independent control. The
    # candidate provisions only overlap windows and BODFT coefficient plans.
    replace_once(output / 'src/setup.cpp',
                 '    for (unsigned int index = 0; index < 2; ++index) prepare_transform(setup.transforms[index], setup.identification.blocks[index]);',
                 '    for (unsigned int index = 0; index < 2; ++index) {\n'
                 '        const unsigned int block = setup.identification.blocks[index];\n'
                 '        setup.bfft_plans[index].emplace(block, *memory);\n'
                 '        Transform& transform = setup.transforms[index];\n'
                 '        transform.block = block;\n'
                 '        transform.window.resize(block / 2);\n'
                 '        for (unsigned int sample = 0; sample < block / 2; ++sample) {\n'
                 '            const double inner = std::sin(std::numbers::pi * (static_cast<double>(sample) + 0.5) / block);\n'
                 '            transform.window[sample] = std::sin(0.5 * std::numbers::pi * inner * inner);\n'
                 '        }\n'
                 '    }')
    replace_once(header, 'classifications(memory) {}',
                 'classifications(memory), bfft_workspace(*memory) {}')
    replace_once(header, '    Butterfly butterfly{scalar_butterfly};',
                 '    Butterfly butterfly{scalar_butterfly};\n'
                 '    experiment::BfftWorkspace bfft_workspace;')
    replace_once(implementation, '    workspace.real.resize(block); workspace.imaginary.resize(block);',
                 '    workspace.bfft_workspace.prepare(*setup.bfft_plans[0], *setup.bfft_plans[1]);')
    replace_once(implementation,
                 'inverse_mdct(setup.transforms[mode.large ? 1 : 0], spectrum, time, workspace.real, workspace.imaginary, workspace.butterfly);',
                 'const unsigned int transform_index = mode.large ? 1 : 0;\n'
                 '            workspace.bfft_workspace.execute(*setup.bfft_plans[transform_index], transform_index, spectrum, time);')
    replace_once(output / 'src/decoder.cpp',
                 '+ workspace.pcm.capacity() * sizeof(float) + workspace.classifications.capacity() * sizeof(unsigned int);',
                 '+ workspace.pcm.capacity() * sizeof(float) + workspace.classifications.capacity() * sizeof(unsigned int)\n'
                 '                        + workspace.bfft_workspace.bytes();')


if __name__ == '__main__':
    main()
