WIP

# WASM Compilation
## Install Emscripten
Clone the SDK onto parent folder
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
```

Install the latest version
```bash
./emsdk install latest
./emsdk activate latest
```

Load environment variables
```bash
source ./emsdk_env.sh # Linux/macOS
```
```bash
emsdk_env.bat # Windows
```

Verify:
```bash
emcc --version
```

## Compile with Emscripten
From the project root (livebpm)

```bash
mkdir build-wasm && cd build-wasm
emcmake cmake ..
cmake --build .
```

This generates
* `livebpm_wasm.js` - JavaScript Loader
* `livebpm_wasm.mwasm` - Binary WebAssembly Module

## Serve Locally and Test
Ensure you are on the build-wasm folder

Copy the web files
```bash
cp ../wasm/processor.js .
cp ../wasm/index.html .
```

Start with python
```bash
python -m http.server 8080
```
or Node
```bash
npx serve .
```