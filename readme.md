# Code

## Compilation
The project uses [CMake](https://cmake.org/) for building the source code.
To compile, run the following commands from the root directory of the repository:

```sh
mkdir build && cd build
# cmake -DCMAKE_BUILD_TYPE=Debug .. #debug mode
cmake -DCMAKE_BUILD_TYPE=Release .. -DBS=4096
make -j
```
## comparison

>>> 正在测试数据集 (K变动): yelp
  [K= 1] Time: 0.35ms oblivious shuffle time: 14.97ms 
  [K= 2] Time: 0.33ms oblivious shuffle time: 11.37ms 
  [K= 3] Time: 0.29ms oblivious shuffle time: 11.73ms 
  [K= 4] Time: 0.31ms oblivious shuffle time: 11.34ms 
  [K= 5] Time: 0.27ms oblivious shuffle time: 0.00ms 
  [K= 6] Time: 0.35ms oblivious shuffle time: 24.12ms 

>>> 正在测试数据集 (K变动): tweets
  [K= 1] Time: 0.47ms oblivious shuffle time: 145.70ms 
  [K= 2] Time: 0.63ms oblivious shuffle time: 168.46ms 
  [K= 3] Time: 0.76ms oblivious shuffle time: 242.64ms 
  [K= 4] Time: 1.06ms oblivious shuffle time: 395.46ms 
  [K= 5] Time: 0.76ms oblivious shuffle time: 257.67ms 
  [K= 6] Time: 1.16ms oblivious shuffle time: 422.05ms 

>>> 正在测试数据集 (K变动): foursquare
  [K= 1] Time: 0.21ms oblivious shuffle time: 48.31ms 
  [K= 2] Time: 0.50ms oblivious shuffle time: 169.10ms 
  [K= 3] Time: 0.53ms oblivious shuffle time: 177.37ms 
  [K= 4] Time: 0.57ms oblivious shuffle time: 213.91ms 
  [K= 5] Time: 0.69ms oblivious shuffle time: 262.09ms 
  [K= 6] Time: 0.85ms oblivious shuffle time: 414.68ms 

>>> 正在测试数据集 (K变动): synthetic
  [K= 1] Time: 0.45ms oblivious shuffle time: 118.94ms 
  [K= 2] Time: 0.65ms oblivious shuffle time: 187.75ms 
  [K= 3] Time: 0.82ms oblivious shuffle time: 295.60ms 
  [K= 4] Time: 0.89ms oblivious shuffle time: 333.88ms 
  [K= 5] Time: 0.97ms oblivious shuffle time: 359.62ms 
  [K= 6] Time: 1.21ms oblivious shuffle time: 530.27ms