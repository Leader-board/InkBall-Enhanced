# Runtime notices for the supplied Windows executable

The optional prebuilt x64 executable was compiled using Ubuntu's MinGW-w64 GCC 13 POSIX package, version `13.2.0-6ubuntu1+26.1`, with MinGW-w64 11.0.1 headers/runtime. The executable statically links the GCC C++ support libraries; Windows system DLLs remain operating-system dependencies. The Visual Studio projects use MSVC and do not require this compiler.

Included verbatim license texts and package notices:

- `GCC-RUNTIME-COPYRIGHT.txt`: installed MinGW GCC package copyright notice, including libgcc/libstdc++ licensing and third-party notices. The installed package notice contains historical GCC packaging text; it is retained unchanged.
- `GCC-RUNTIME-EXCEPTION-3.1.txt`: complete GCC Runtime Library Exception version 3.1, extracted verbatim from the preceding notice.
- `GPL-3.0.txt`: complete GNU General Public License version 3.
- `LIBSTDCXX-NOTICE.txt`: introductory copyright/license notice from the installed GCC 13 MinGW-w64 `<vector>` header, with a short provenance heading.
- `MINGW-W64-COPYRIGHT.txt`: complete installed MinGW-w64 copyright and license notices, including ZPL/BSD/public-domain components.

These runtime notices do not change the separate rights attached to the original Microsoft game assets or the other source files in this project. The compiler itself is not distributed.

Provenance: `/usr/share/doc/gcc-mingw-w64-base/copyright`, `/usr/share/doc/mingw-w64-common/copyright`, `/usr/share/common-licenses/GPL-3`, and `/usr/lib/gcc/x86_64-w64-mingw32/13-posix/include/c++/vector` from the installed toolchain used for this build.
