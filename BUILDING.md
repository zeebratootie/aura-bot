Building
--------

Get the source code from the project [repository][1] at Gitlab.

### Windows

Windows users must use VS2019 or later. Visual Studio 2019 Community edition works.
When installing Visual Studio, select in the `Desktop development with C++` category the `Windows 8.1 SDK` or `Windows 10 SDK` 
(depending on your OS version). Additionally, the `MSVC v142 - VS 2019 C++ x64/x86 build tools (v14.29)`

#### Steps for Windows

- Open the solution file `aura.sln` with VS2019.
- Choose one of the supported configurations according to the following table

|Configuration|Extra Components|
|:---| :--- |
|ReleaseLite|None|
|Release|C++ Requests, MiniUPnP, D++|
|Experimental|C++ Requests, MiniUPnP, D++, pjass, MDNS|

- Choose Win32 or x64 as the platform.
- Compile the solution.
- Find the generated binary in the `.msvc\Release` folder or an alternative according to your chosen configuration.

|Configuration|Platform|Output folder|
|:---| :--- | :--- |
|ReleaseLite|Win32|`.msvc\ReleaseLite`|
|ReleaseLite|x64|`.msvc\ReleaseLite-x64`|
|Release|Win32|`.msvc\Release`|
|Release|x64|`.msvc\Release-x64`|
|Experimental|Win32|`.msvc\Experimental`|
|Experimental|x64|`.msvc\Experimental-x64`|

**Note**: The recommended configuration is `Release-x64`. However, if you have trouble building it, 
you may fall back to the ``ReleaseLite`` configuration instead. Alternatively, you may manually disable troublesome components 
in the project ``"Configuration Properties"``, according to the [Optional Components section](#optional-components).

**Note**: For the component D++, v10.0.31 is the latest version supporting Windows 7. 
By default, Aura (including the `Release` configuration) is built with D++ v10.1, which is NOT supported in Windows 7.
For an Aura version supporting Windows 7, check out the ``w7`` branch. [5]

**Note**: In order to use the `Release` or `Experimental` configurations, several dynamic libraries must be installed 
(or placed next to the `aura` executable). Find the full list of DLLs in the [Optional Components section](#optional-components). 
They can be downloaded from the official `D++` releases [7]

**Note**: Support for game versions v1.30 onwards requires the `MDNS` component (which by default is available in the `Experimental` 
configurations). Make sure that your `%BONJOUR_SDK_HOME%` environment variable is correctly setup, then build aura with the `Experimental` configuration.

### Linux

Linux users will probably need some packages for it to build:

* Debian/Ubuntu -- `apt-get install git build-essential m4 libgmp3-dev libssl-dev cmake libbz2-dev zlib1g-dev libcurl4-openssl-dev curl`
* Arch Linux -- `pacman -S base-devel cmake libssl-dev libgmp3-dev curl libssl-dev`

#### Steps for Linux

For building StormLib execute the following commands (line by line):

	cd aura-bot/deps/StormLib
	mkdir build
	cd build
	cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=1 -DSTORM_UNICODE=1 ..
	make
	sudo make install

Next, build bncsutil:

	cd ../..
	cd bncsutil/src/bncsutil
	make
	sudo make install

Continue building miniupnpc

	cd ../../..
	cd miniupnpc
	make
	sudo make install

  (Or disable it by setting an environment variable: ``export AURABUILD_MINIUPNP=0``)
  
Afterwards, [C++ Requests][4]

	cd ../..
	git clone https://github.com/libcpr/cpr.git cpr-src
	cd cpr-src
	mkdir build
	cd build
	cmake .. -DCPR_USE_SYSTEM_CURL=ON -DBUILD_SHARED_LIBS=1
	cmake --build . --parallel
	cmake --install .

  (Or disable it by setting an environment variable: ``export AURABUILD_CPR=0``)

Optionally, [D++][7] for Discord integration. Note that this step can take around half an hour.

	cd ../../
	git clone https://github.com/brainboxdotcc/DPP.git dpp-src
	cd dpp-src
	mkdir build
	cd build
	cmake .. -DBUILD_SHARED_LIBS=ON -DBUILD_VOICE_SUPPORT=OFF -DDPP_BUILD_TEST=OFF
	cmake --build . -j4
	make install

  (Or disable it by setting an environment variable: ``export AURABUILD_DPP=0``)

Then, proceed to build Aura:

	cd ../../..
	make

Now you can run Aura by executing `./aura` or install it to your PATH using `sudo make install`.

**Note**: gcc version needs to be 8 or higher along with a compatible libc.

**Note**: clang needs to be 5 or higher along with ld gold linker (ie. package binutils-gold for ubuntu),
clang lower than 16 requires -std=c++17

**Note**: StormLib installs itself in `/usr/local/lib` which isn't in PATH by default
on some distros such as Arch or CentOS.

### OS X

**Warning**: These instructions for OS X haven't been tested.

#### Requirements

* OSX ≥10.15 (Catalina) or higher.
* Latest available Xcode for your platform and/or the Xcode Command Line Tools.
One of these might suffice, if not just get both.
* A recent `libgmp`.

You can use [Homebrew](http://brew.sh/) to get `libgmp`. When you are at it, you can also use it to install StormLib instead of compiling it on your own:

	brew install gmp
	brew install stormlib   # optional

Now proceed by following the [steps for Linux users](#steps-for-linux) and omit StormLib in case you installed it using `brew`.

### Optional components

#### Makefile

When using Makefile and setting the appropriate environment variables to disable components, as described in the
Linux build steps, this section will be automatically taken care of.

#### MSVC

Follow this table to enable/disable components.

|Component|Preprocessor directive (OFF)|Linked libraries (ON)|Dynamic libraries (.dll) (ON) |
|:---:| :--- | :--- | :--- |
| C++ Requests | ``DISABLE_CPR`` | ``cpr.lib;libcurl.lib;Crypt32.lib;Wldap32.lib`` | None |
| MiniUPnP | ``DISABLE_MINIUPNP`` | ``miniupnpc.lib`` | None |
| D++ | ``DISABLE_DPP`` | ``dpp.lib`` | ``dpp.dll;libcrypto-1.1.dll,libssl-1_1.dll,opus.dll,zlib1.dll`` |
| D++ (x64) | ``DISABLE_DPP`` | ``dpp.lib`` | ``dpp.dll;libcrypto-1.1-x64.dll,libssl-1_1-x64.dll,opus.dll,zlib1.dll`` |
| pjass | ``DISABLE_PJASS`` | ``pjass.lib`` | None |
| MDNS (Bonjour &#9415;) | ``DISABLE_MDNS`` | ``bonjour.lib`` | ``dnssd.dll`` |

The following software must be installed as a requirement for some components.

|Component|Build|Runtime|
|:---:| :--- | :--- |
| C++ Requests | None | None |
| MiniUPnP | None | None |
| D++ | None | Dynamic libraries from previous table |
| D++ (x64) | None | Dynamic libraries from previous table |
| pjass | `flex` and `bison` [8] | None |
| MDNS (Bonjour &#9415;) | Bonjour SDK for Windows [2] | Bonjour Printer Services for Windows [3] |


**Note**: (Release/Custom) `dpp.dll` and other libraries required in runtime by `D++` are available in DPP releases [7].

**Note**: (Experimental/Custom) `dnssd.dll` and other components required by `MDNS` must be preinstalled as a requirement to [2].

**Note**: (Experimental/Custom) `flex` and `bison` are required by `pjass` and must be installed separately [8].

[1]: https://gitlab.com/ivojulca/aura-bot
[2]: https://developer.apple.com/download/all/?q=Bonjour%20SDK%20for%20Windows
[3]: https://support.apple.com/en-us/106380
[4]: https://github.com/libcpr/cpr
[5]: https://gitlab.com/ivojulca/aura-bot/-/tree/w7
[6]: https://github.com/brainboxdotcc/DPP
[7]: https://github.com/brainboxdotcc/DPP/releases
[8]: https://github.com/lexxmark/winflexbison
