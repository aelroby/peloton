//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// transaction_manager_util.h
//
// Identification: src/concurrency/transaction_manager_util.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include "concurrency/transaction.h"

namespace peloton {

namespace concurrency {

concurrency::Transaction* BeginTransaction();

void CommitTransaction(concurrency::Transaction *);

void AbortTransaction(concurrency::Transaction *);
  
}
}
