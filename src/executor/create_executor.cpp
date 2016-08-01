//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// create_executor.cpp
//
// Identification: src/executor/create_executor.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//


#include "../include/executor/create_executor.h"

#include <vector>

#include "catalog/bootstrapper.h"

namespace peloton {
namespace executor {

//Constructor for drop executor
CreateExecutor::CreateExecutor(const planner::AbstractPlan *node,
                               ExecutorContext *executor_context)
    : AbstractExecutor(node, executor_context) {
	context = executor_context;
}

// Initialize executer
// Nothing to initialize now
bool CreateExecutor::DInit() {
  LOG_INFO("Initializing Create Executer...");
  LOG_INFO("Create Executer initialized!");
  return true;

}

bool CreateExecutor::DExecute() {
  LOG_INFO("Executing Create...");
  const planner::CreatePlan &node = GetPlanNode<planner::CreatePlan>();
  std::string table_name = node.GetTableName();
  std::unique_ptr<catalog::Schema> schema(node.GetSchema());

  Result result = catalog::Bootstrapper::global_catalog->CreateTable(DEFAULT_DB_NAME, table_name, std::move(schema));
  context->GetTransaction()->SetResult(result);

  if(context->GetTransaction()->GetResult() == Result::RESULT_SUCCESS){
	  LOG_INFO("Creating table succeeded!");
  }
  else if(context->GetTransaction()->GetResult() == Result::RESULT_FAILURE) {
	  LOG_INFO("Creating table failed!");
  }
  else {
	  LOG_INFO("Result is: %d", context->GetTransaction()->GetResult());
  }

  return false;
}

}
}
