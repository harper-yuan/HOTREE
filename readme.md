# HOTree
This directory contains the implementation of the proposed paper "HOTree: High-Throughput Oblivious Spatial Keyword Indexing via Intra- and Inter-query Parallelism on Secure TEEs".Please refer to the implementation at [OBIR-tree](https://github.com/XYWANG-XDU/OBIR-tree) for comparison.

## External Dependencies
The following libraries need to be installed separately and should be available to the build system and compiler.

- C++17 and [CMake](https://cmake.org/)
- [Boost](https://www.boost.org/) (1.72.0 or later)
- [Intel SGX SDK](https://www.intel.com/content/www/us/en/developer/tools/software-guard-extensions/overview.html) (Supporting SGX2)

## Run all code

### HOTree

#### Compilation

The project uses [CMake](https://cmake.org/) for building the source code.

To compile, run the following commands from the directory `Basic_HOTree/` of the repository:

```sh
cd Basic_HOTree
mkdir build && cd build

# cmake -DCMAKE_BUILD_TYPE=Debug .. #debug mode

cmake -DCMAKE_BUILD_TYPE=Release .. -DBS=4096 -DBUILD_TEST_CODE=1

make -j

ctest # for correctness test
```

#### Test command

```sh
./benchmarks/query_vary_k # for initialization and query time varying k
./benchmarks/query_vary_N # for initialization and query time varying N
```



### Optimized HOTree

#### Query time varying k

Run the following commands to test querying time varying k

```bash
make clean
make -j SGX_PRERELEASE=1 SGX_DEBUG=0
./app
```

#### Query time varying N

Update the file `Optimized_HOTree/App/App.cpp`

```bash
188 // #include "benchmark/query_vary_k.hpp" 
189 #include "benchmark/query_vary_N.hpp"
.
.
.
208 // query_vary_k();
209 query_vary_N();
```

Recompile to test querying time varying N

```bash
make clean
make -j SGX_PRERELEASE=1 SGX_DEBUG=0
./app
```



## Dataset 

If you want to add a dataset, create a folder under the folder `dataset` and add file `dataset.txt` and file `keywords_dict.txt` under the folder to record all data and keyword lists respectively.

for example, a file `dataset.txt` will be like this:
```bash
JazzClub 40.733596 -74.003139
Gym 40.758102 -73.975734
IndianRestaurant 40.732456 -74.003755
IndianRestaurant 42.345907 -71.087001
SandwichPlace 39.933178 -75.159262
BowlingAlley 40.652766 -74.003092
DiveBar 40.726961 -73.980039
Bar 40.756353 -73.967676
SeafoodRestaurant 37.779837 -122.494471
Bar 34.092793 -118.281469
Nightclub 40.591334 -73.960725
JazzClub 40.733630 -74.002288
Pub 41.941562 -87.664011
```

for example, a file `keywords_dict.txt` will be like this:
```bash
& Probates
3D Printing
ATV Rentals/Tours
Acai Bowls
Accessories
Accountants
Acne Treatment
Active Life
```