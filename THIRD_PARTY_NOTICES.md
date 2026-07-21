# Third-Party Notices

This project vendors third-party software and contains modified third-party
derivatives. Keep this file with source and binary distributions of CTR Native.

## PsyCross / Psy-X

Source: <https://github.com/OpenDriver2/PsyCross>

PsyCross provided the starting point for parts of CTR Native's
Psy-Q-compatible PS1 hardware abstraction layer, including compatible GPU, GTE,
SPU, CD, and controller library interfaces. CTR Native now owns those headers
and native platform implementations in `include/` and `platform/` while
preserving Psy-Q-shaped APIs.

CTR Native contains modified/project-owned PsyCross derivatives in these
component areas:

- `include/psx/`: Psy-Q-compatible facade headers
- `include/platform/`: native GPU/renderer facade types and support headers
- `platform/`: native PS1 facade implementations, GTE/GPU/render support,
  platform shell code, and generated GL loader sources

Individual source files may carry narrower provenance notes where the original
PsyCross source path is useful during maintenance.

License: MIT

Copyright (c) 2020 REDRIVER2 Project

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## PSn00bSDK

Source: <https://github.com/Lameguy64/PSn00bSDK>

Path: `include/psn00bsdk`

CTR Native vendors a small PSn00bSDK header subset for PS1/Psy-Q-compatible
types, constants, and inline helpers used by the shared source. CTR Native does
not vendor or link `libpsn00b` into the native PC executable.

This notice applies to PSn00bSDK core files only. `mkpsxiso` and `dumpsxiso`
are separate GPLv2-or-later tools and are not distributed as part of CTR Native.

License: Mozilla Public License 2.0

The vendored header files retain their original copyright and license notices.
A copy of the MPL 2.0 license can be obtained at:
<https://mozilla.org/MPL/2.0/>

## Archipelago Networking Dependencies (AP build only)

Path: `ap/vendor/` (not tracked in this repository; required to configure with
`-DCTR_AP=ON`, see the README)

The Archipelago build (`ctr_native_ap`) compiles and statically links the
following header-only libraries. They are not part of the vanilla engine build
and are not distributed in this repository's source tree, but these notices
apply to any binary distribution of `ctr_native_ap`. The exact pinned revision
of each is recorded in `ap/vendor/versions.lock` (the commit shas below are
copied from it); the build is verified against that lock at configure time.

### apclientpp

Source: <https://github.com/black-sliver/apclientpp>

Vendored version: 0.6.4 (commit `79621690a3e845645f43888b0fe234a99c74892e`; upstream publishes no tag, so the pin is the commit)

License: MIT — Copyright (c) 2022 black-sliver, FelicitusNeko

### wswrap

Source: <https://github.com/black-sliver/wswrap>

Vendored version: 1.03.00 (commit `aeba7ac428028723fb26ce92488f260660f786b1`; upstream publishes no tag, so the pin is the commit)

License: MIT — Copyright (c) 2021 black-sliver

### nlohmann/json

Source: <https://github.com/nlohmann/json>

Vendored version: 3.11.3 (tag `v3.11.3`, commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`)

License: MIT — Copyright (c) 2013-2021 Niels Lohmann

The MIT License applying to the three components above:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### WebSocket++

Source: <https://github.com/zaphoyd/websocketpp>

Vendored version: 0.8.2 (tag `0.8.2`, commit `56123c87598f8b1dd471be83ca841ceae07f95ba`)

License: BSD 3-Clause

Copyright (c) 2015, Peter Thorson. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
- Neither the name of the WebSocket++ Project nor the names of its
  contributors may be used to endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL PETER THORSON BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Asio

Source: <https://github.com/chriskohlhoff/asio>

Vendored version: 1.30.2 (tag `asio-1-30-2`, commit `12e0ce9e0500bf0f247dbd1ae894272656456079`)

License: Boost Software License 1.0

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

### valijson

Source: <https://github.com/tristanpenman/valijson>

Not shipped: the AP build defines `AP_NO_SCHEMA`, which compiles out apclientpp's valijson includes, so no valijson code is linked into `ctr_native_ap`. It is marked optional in `ap/vendor/versions.lock` and is not fetched. This notice is retained for completeness in case `AP_NO_SCHEMA` is ever removed; if that happens, pin a revision in the lock and update this entry with the shipped version.

License: BSD 2-Clause

Copyright (c) 2016, Tristan Penman
Copyright (c) 2016, Akamai Technolgies, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## SDL3

Path: `externals/SDL`

SDL3 provides cross-platform host windowing, input, timing, and audio device
support for CTR Native.

Vendored version: 3.4.10 (`release-3.4.10`)

Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
