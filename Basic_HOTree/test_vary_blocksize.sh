#!/bin/bash

# 定义你想要测试的 BlockSize 列表
BLOCK_SIZES=(2048 16384 131072 1048576)

RESULT_DIR="./exp_result"
# 根据你的 C++ 代码，结果保存在 ../exp_result 目录下
QUERY_CSV="$RESULT_DIR/query_vary_BS.csv"
INIT_CSV="$RESULT_DIR/initialization_vary_BS.csv"

# 2. 预清理指令：如果文件存在则删除，确保本次实验结果是全新的
echo "正在清理旧的实验数据..."
rm -f "$QUERY_CSV" "$INIT_CSV"

# 创建并进入 build 目录
cd build
for bs in "${BLOCK_SIZES[@]}"
do
    echo "-------------------------------------------"
    echo "Testing BlockSize = $bs"
    echo "-------------------------------------------"

    # 1. 使用 CMake 配置，并传入当前的 BlockSize 值
    cmake .. -DBS=$bs -DBUILD_TEST_CODE=OFF

    # 2. 编译特定目标
    make  -j

    # 3. 运行测试程序
    ./benchmarks/initialization_vary_blocksize
    ./benchmarks/query_vary_blocksize

    echo "BlockSize $bs test have finished and results are appended to CSV。"
done

echo "All test have finished"