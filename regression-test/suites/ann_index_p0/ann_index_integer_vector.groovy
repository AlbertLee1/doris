// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// Doris ANN Index Integer Vector Test Suite
// Tests support for integer array types (TINYINT, SMALLINT, INT) as vectors

suite ("ann_index_integer_vector") {
    sql "set enable_common_expr_pushdown=true;"

    // 1) Test INT (INT32) vectors with L2 distance
    sql "drop table if exists tbl_ann_int32"
    sql """
    CREATE TABLE tbl_ann_int32 (
        id INT NOT NULL,
        embedding ARRAY<INT> NOT NULL,
        INDEX idx_emb (`embedding`) USING ANN PROPERTIES(
            "index_type"="hnsw",
            "metric_type"="l2_distance",
            "dim"="3"
        )
    ) ENGINE=OLAP
    DUPLICATE KEY(id)
    DISTRIBUTED BY HASH(id) BUCKETS 2
    PROPERTIES ("replication_num" = "1");
    """

    qt_sql_int32_insert """
    INSERT INTO tbl_ann_int32 VALUES
    (1, [1, 2, 3]),
    (2, [1, 2, 4]),
    (3, [10, 10, 10]),
    (4, [100, 100, 100]);
    """

    // Query: L2 distance with integer query vector
    qt_sql_int32_l2_query1 "select id, l2_distance(embedding, [1,2,3]) as dist from tbl_ann_int32 order by dist limit 3;"

    // Query: Using approximate version
    qt_sql_int32_l2_query2 "select id from tbl_ann_int32 order by l2_distance_approximate(embedding, [1,2,3]) limit 3;"

    // Range search
    qt_sql_int32_range "select id from tbl_ann_int32 where l2_distance_approximate(embedding, [1,2,3]) < 5.0 order by id;"

    // 2) Test SMALLINT (INT16) vectors with inner product
    sql "drop table if exists tbl_ann_int16"
    sql """
    CREATE TABLE tbl_ann_int16 (
        id INT NOT NULL,
        embedding ARRAY<SMALLINT> NOT NULL,
        INDEX idx_emb (`embedding`) USING ANN PROPERTIES(
            "index_type"="hnsw",
            "metric_type"="inner_product",
            "dim"="4"
        )
    ) ENGINE=OLAP
    DUPLICATE KEY(id)
    DISTRIBUTED BY HASH(id) BUCKETS 2
    PROPERTIES ("replication_num" = "1");
    """

    qt_sql_int16_insert """
    INSERT INTO tbl_ann_int16 VALUES
    (1, [1, 2, 3, 4]),
    (2, [5, 6, 7, 8]),
    (3, [10, 10, 10, 10]),
    (4, [1, 1, 1, 1]);
    """

    // Query: Inner product (higher is better)
    qt_sql_int16_ip_query1 "select id, inner_product(embedding, [1,2,3,4]) as ip from tbl_ann_int16 order by ip desc limit 3;"

    // Using approximate version
    qt_sql_int16_ip_query2 "select id from tbl_ann_int16 order by inner_product_approximate(embedding, [5,6,7,8]) desc limit 3;"

    // 3) Test TINYINT (INT8) vectors with cosine distance
    sql "drop table if exists tbl_ann_int8"
    sql """
    CREATE TABLE tbl_ann_int8 (
        id INT NOT NULL,
        embedding ARRAY<TINYINT> NOT NULL,
        INDEX idx_emb (`embedding`) USING ANN PROPERTIES(
            "index_type"="hnsw",
            "metric_type"="l2_distance",
            "dim"="5"
        )
    ) ENGINE=OLAP
    DUPLICATE KEY(id)
    DISTRIBUTED BY HASH(id) BUCKETS 2
    PROPERTIES ("replication_num" = "1");
    """

    qt_sql_int8_insert """
    INSERT INTO tbl_ann_int8 VALUES
    (1, [1, 2, 3, 4, 5]),
    (2, [1, 2, 3, 4, 6]),
    (3, [10, 10, 10, 10, 10]),
    (4, [5, 5, 5, 5, 5]);
    """

    // Query: L2 distance
    qt_sql_int8_l2_query "select id from tbl_ann_int8 order by l2_distance(embedding, [1,2,3,4,5]) limit 3;"

    // Cosine distance query
    qt_sql_int8_cosine_query "select id, cosine_distance(embedding, [1,2,3,4,5]) as dist from tbl_ann_int8 order by dist limit 3;"

    // 4) Test mixed integer types - verify distance calculations work correctly
    sql "drop table if exists tbl_ann_mixed_test"
    sql """
    CREATE TABLE tbl_ann_mixed_test (
        id INT NOT NULL,
        vec_int ARRAY<INT> NOT NULL,
        vec_float ARRAY<FLOAT> NOT NULL,
        INDEX idx_int (`vec_int`) USING ANN PROPERTIES(
            "index_type"="hnsw",
            "metric_type"="l2_distance",
            "dim"="3"
        )
    ) ENGINE=OLAP
    DUPLICATE KEY(id)
    DISTRIBUTED BY HASH(id) BUCKETS 2
    PROPERTIES ("replication_num" = "1");
    """

    qt_sql_mixed_insert """
    INSERT INTO tbl_ann_mixed_test VALUES
    (1, [1, 2, 3], [1.0, 2.0, 3.0]),
    (2, [4, 5, 6], [4.0, 5.0, 6.0]),
    (3, [7, 8, 9], [7.0, 8.0, 9.0]);
    """

    // Verify that integer vectors and float vectors produce similar results
    qt_sql_mixed_compare_int "select id, l2_distance(vec_int, [1,2,3]) as dist from tbl_ann_mixed_test order by dist;"
    qt_sql_mixed_compare_float "select id, l2_distance(vec_float, [1.0,2.0,3.0]) as dist from tbl_ann_mixed_test order by dist;"

    // 5) Test with quantization (SQ8) on integer vectors
    sql "drop table if exists tbl_ann_int32_sq8"
    sql """
    CREATE TABLE tbl_ann_int32_sq8 (
        id INT NOT NULL,
        embedding ARRAY<INT> NOT NULL,
        INDEX idx_emb (`embedding`) USING ANN PROPERTIES(
            "index_type"="hnsw",
            "metric_type"="l2_distance",
            "dim"="8",
            "quantizer"="sq8"
        )
    ) ENGINE=OLAP
    DUPLICATE KEY(id)
    DISTRIBUTED BY HASH(id) BUCKETS 2
    PROPERTIES ("replication_num" = "1");
    """

    qt_sql_int32_sq8_insert """
    INSERT INTO tbl_ann_int32_sq8 VALUES
    (1, [1, 2, 3, 4, 5, 6, 7, 8]),
    (2, [2, 3, 4, 5, 6, 7, 8, 9]),
    (3, [10, 20, 30, 40, 50, 60, 70, 80]),
    (4, [100, 100, 100, 100, 100, 100, 100, 100]);
    """

    qt_sql_int32_sq8_query "select id from tbl_ann_int32_sq8 order by l2_distance_approximate(embedding, [1,2,3,4,5,6,7,8]) limit 3;"

    // 6) Test large integer values don't overflow
    sql "drop table if exists tbl_ann_large_int"
    sql """
    CREATE TABLE tbl_ann_large_int (
        id INT NOT NULL,
        embedding ARRAY<INT> NOT NULL,
        INDEX idx_emb (`embedding`) USING ANN PROPERTIES(
            "index_type"="hnsw",
            "metric_type"="l2_distance",
            "dim"="3"
        )
    ) ENGINE=OLAP
    DUPLICATE KEY(id)
    DISTRIBUTED BY HASH(id) BUCKETS 2
    PROPERTIES ("replication_num" = "1");
    """

    qt_sql_large_int_insert """
    INSERT INTO tbl_ann_large_int VALUES
    (1, [1000, 2000, 3000]),
    (2, [1001, 2001, 3001]),
    (3, [10000, 20000, 30000]);
    """

    qt_sql_large_int_query "select id from tbl_ann_large_int order by l2_distance_approximate(embedding, [1000,2000,3000]) limit 2;"
}
