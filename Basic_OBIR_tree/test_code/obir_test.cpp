#define BOOST_TEST_MODULE obir_test
#include <boost/test/included/unit_test.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include "obir_tree.h"
#include "DataReader.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(obir_correctness_test)

BOOST_AUTO_TEST_CASE(test_obir_topk) {
    // 1. Load data
    vector<string> dictionary = LoadDictionary("../../dataset/synthetic/keywords_dict.txt");
    vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic/dataset.txt", 1024); 
    vector<DataRecord> queries = readDataFromDataset("../../dataset/synthetic/query.txt", 10);

    if (data.empty() || queries.empty()) {
        BOOST_FAIL("Dataset load failed");
    }

    // 2. Build OBIRTree
    OBIRTree obirTree(dictionary);
    Client* client = nullptr;
    obirTree.Build(data, client);

    // 3. Perform queries
    int k = 5;
    for (int i = 0; i < queries.size(); i++) {
        double qx = queries[i].x_coord;
        double qy = queries[i].y_coord;
        string qText = queries[i].processed_text;

        cout << "Query " << i << ": qx=" << qx << ", qy=" << qy << ", k=" << k << endl;
        
        auto start = chrono::high_resolution_clock::now();
        vector<pair<double, DataRecord>> results = obirTree.SearchTopK(qx, qy, qText, k, client);
        auto end = chrono::high_resolution_clock::now();
        
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);

        cout << "Results size: " << results.size() << " (Time: " << duration.count() << "ms)" << endl;
        for (const auto& res : results) {
            cout << "  ID: " << res.second.id << ", Score: " << res.first << endl;
        }
        
        BOOST_CHECK(results.size() <= k);
        if (data.size() >= k) {
            BOOST_CHECK(results.size() > 0);
        }
    }
    
    delete client;
}

BOOST_AUTO_TEST_SUITE_END()
