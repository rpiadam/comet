# FoxComet ![Build Status](https://github.com/rpiadam/comet/workflows/CI/badge.svg)

FoxComet is a modern, highly scalable IRCv3 server implementation designed for production use. It implements IRCv3.1 and significant portions of IRCv3.2, providing a robust foundation for IRC networks.

FoxComet is designed to work seamlessly with IRCv3-capable services implementations such as [Atheme][atheme] or [Anope][anope].

   [atheme]: https://atheme.github.io/
   [anope]: http://www.anope.org/

## System Requirements

To build and run FoxComet, you will need:

 * A supported POSIX-compatible operating system (Linux, macOS, FreeBSD, etc.)
 * A working dynamic library system (shared library support)
 * A C99-compatible compiler (GCC or Clang recommended)
 * Build tools: `autoconf`, `automake`, `libtool`, `make`
 * Parser generators: `flex` (or `lex`) and `bison` (or `yacc`)
 * SQLite3 development libraries
 * `pkg-config` for dependency detection

## Supported Platforms

FoxComet is primarily developed and tested on Linux with glibc. While it should compile and run on most POSIX-compatible operating systems, extended platform support may require community maintenance. If you're interested in maintaining support for a specific platform, please contact us.

## Platform-Specific Notes

Known issues and platform-specific configuration requirements:

 * **macOS**: You must set the `LIBTOOLIZE` environment variable to point to `glibtoolize` before running `autogen.sh`:

   ```bash
   brew install libtool
   export LIBTOOLIZE="/usr/local/bin/glibtoolize"
   ./autogen.sh
   ```

   On Apple Silicon (M1/M2), use `/opt/homebrew/bin/glibtoolize` instead.

 * **FreeBSD**: If you are compiling with IPv6 enabled, you may experience issues with IPv4 due to the socket implementation. To resolve this, set:

   ```bash
   sysctl net.inet6.ip6.v6only=0
   ```

 * **Solaris**: You may need to set your `PATH` to include `/usr/gnu/bin` and `/usr/gnu/sbin` before `/usr/bin` and `/usr/sbin`, as Solaris's default tools may not be compatible with the configure script. When running as a 32-bit binary, start it as:

   ```bash
   ulimit -n 4095 ; LD_PRELOAD_32=/usr/lib/extendedFILE.so.1 ./foxcomet
   ```

## Building

### Quick Start

```bash
# Install build dependencies (Debian/Ubuntu)
sudo apt install build-essential pkg-config automake libtool libsqlite3-dev

# Or on RHEL/CentOS/Fedora
sudo dnf install gcc make automake libtool sqlite-devel pkg-config

# Build and install
./autogen.sh
./configure --prefix=/path/to/installation
make
make check  # Run test suite
make install
```

See `./configure --help` for additional build options.

### Feature-Specific Requirements

#### SSL/TLS Support

FoxComet supports multiple TLS/SSL libraries for client and server connections:

 * **OpenSSL 1.0.0 or newer** (recommended): `--enable-openssl`
 * **LibreSSL**: `--enable-openssl` (autodetected)
 * **mbedTLS**: `--enable-mbedtls`
 * **GnuTLS**: `--enable-gnutls`

**Note**: Certificate-based operator CHALLENGE authentication requires OpenSSL 1.0.0 or newer. However, CHALLENGE is not recommended for new deployments, so you may use any of the supported TLS libraries.

**Solaris Users**: Solaris distributions have removed ECC/ECDHE support from their OpenSSL builds. You may need to compile your own OpenSSL, or use an alternative TLS library (mbedTLS or GnuTLS).

## Documentation

 * For an overview of available documentation, see [doc/readme.txt](doc/readme.txt)
 * For release notes and recent changes, see [NEWS.md](NEWS.md)
 * Example configuration files are available in the `doc/` directory

## Important System Configuration

The following system files should be readable by the user running the server:

 * `/etc/services`
 * `/etc/protocols`
 * `/etc/resolv.conf`

These files are used during server initialization. If they are not accessible or contain invalid data, FoxComet will attempt to use `127.0.0.1` as a fallback resolver.

## Getting Help

 * **Bug Reports**: Open an issue on GitHub
 * **General Discussion**: Check the repository for discussion channels
 * **Documentation**: See the `doc/` directory for detailed documentation

## Source Code

The FoxComet source code is hosted on GitHub:

```bash
git clone https://github.com/rpiadam/comet.git
```

You can also browse the repository online at: https://github.com/rpiadam/comet
