# Code

## Compilation
The project uses [CMake](https://cmake.org/) for building the source code.
To compile, run the following commands from the root directory of the repository:

```sh
mkdir build && cd build
# cmake -DCMAKE_BUILD_TYPE=Debug .. #debug mode
cmake -DCMAKE_BUILD_TYPE=Release .. -DBS=4096 -DBUILD_TEST_CODE=1
make -j
ctest # for correctness test
```
## Evaluation
```sh
./benchmarks/query_vary_k # serval hours
./benchmarks/query_vary_N # in one hour

```

