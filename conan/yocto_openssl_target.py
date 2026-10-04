"""OpenSSL Configure targets for the pinned Yocto SDK profiles."""
import argparse


# These are upstream OpenSSL target names, not replacement Conan recipes.
# The SDK compiler retains its CPU tuning and endianness flags. Yocto's
# mips64/mips64el tunes use the n64 ABI, not OpenSSL's linux-mips64 n32 ABI.
OPENSSL_TARGETS = {
    'x86_i686': 'linux-x86',
    'x86_core2': 'linux-x86',
    'armv6': 'linux-armv4',
    'armv6hf': 'linux-armv4',
    'armv7': 'linux-armv4',
    'armv7hf': 'linux-armv4',
    'mips': 'linux-mips32',
    'mipsle': 'linux-mips32',
    'mips64': 'linux64-mips64',
    'mips64le': 'linux64-mips64',
    'ppc64': 'linux-ppc64',
    'ppc64le': 'linux-ppc64le',
    'riscv64': 'linux64-riscv64',
}


def openssl_target(arch, version):
    if version not in ('5.0.10', '6.0.3'):
        return None
    if arch == 'loong64':
        return 'linux64-loongarch64' if version == '6.0.3' else None
    # The public recipe already maps x86_64 and ARM64 correctly.
    return OPENSSL_TARGETS.get(arch)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version')
    parser.add_argument('arch')
    args = parser.parse_args()
    target = openssl_target(args.arch, args.version)
    if target:
        print(target)
