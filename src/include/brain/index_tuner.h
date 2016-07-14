//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// index_tuner.h
//
// Identification: src/include/brain/index_tuner.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

#include "common/types.h"

namespace peloton {

namespace index{
class Index;
}

namespace storage{
class DataTable;
}

namespace brain {

class Sample;

//===--------------------------------------------------------------------===//
// Index Tuner
//===--------------------------------------------------------------------===//

class IndexTuner {

 public:

  IndexTuner(const IndexTuner &) = delete;
  IndexTuner &operator=(const IndexTuner &) = delete;
  IndexTuner(IndexTuner &&) = delete;
  IndexTuner &operator=(IndexTuner &&) = delete;

  IndexTuner();

  ~IndexTuner();

  // Singleton
  static IndexTuner &GetInstance();

  // Start tuning
  void Start();

  // Tune layout
  void Tune();

  // Stop tuning
  void Stop();

  // Add table to list of tables whose layout must be tuned
  void AddTable(storage::DataTable* table);

  // Clear list
  void ClearTables();

 protected:

  // Index tuning helper
  void IndexTuneHelper(storage::DataTable* table);

  void BuildIndex(storage::DataTable *table,
                  index::Index *index);

  void BuildIndices(storage::DataTable *table);

  void Analyze(storage::DataTable* table);

 private:

  // Tables whose indices must be tuned
  std::vector<storage::DataTable*> tables;

  std::mutex index_tuner_mutex;

  // Stop signal
  std::atomic<bool> index_tuning_stop;

  // Tuner thread
  std::thread index_tuner_thread;

  //===--------------------------------------------------------------------===//
  // Tuner Parameters
  //===--------------------------------------------------------------------===//

  // Sleeping period (in us)
  oid_t sleep_duration = 10;

  // Threshold sample count
  oid_t sample_count_threshold = 100;

  // # of tile groups to be indexed per iteration
  oid_t max_tile_groups_indexed = 50;

  // storage footprint (MB)
  size_t max_storage_footprint = 2 * 1024;

};


}  // End brain namespace
}  // End peloton namespace
