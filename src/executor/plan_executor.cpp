//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// plan_executor.cpp
//
// Identification: src/executor/plan_executor.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//


#include <vector>

#include "common/logger.h"
#include "concurrency/transaction_manager_factory.h"
#include "executor/executors.h"
#include "executor/executor_context.h"
#include "executor/plan_executor.h"
#include "storage/tuple_iterator.h"

namespace peloton {
namespace bridge {

/*
 * Added for network invoking efficiently
 */
executor::ExecutorContext *BuildExecutorContext(
    const std::vector<Value> &params, concurrency::Transaction *txn);

executor::AbstractExecutor *BuildExecutorTree(
    executor::AbstractExecutor *root, const planner::AbstractPlan *plan,
    executor::ExecutorContext *executor_context);

void CleanExecutorTree(executor::AbstractExecutor *root);

/**
 * @brief Build a executor tree and execute it.
 * Use std::vector<Value> as params to make it more elegant for networking
 * Before ExecutePlan, a node first receives value list, so we should pass
 * value list directly rather than passing Postgres's ParamListInfo
 * @return status of execution.
 */
peloton_status PlanExecutor::ExecutePlan(const planner::AbstractPlan *plan,
                                         const std::vector<Value> &params) {
  peloton_status p_status;

  if (plan == nullptr) return p_status;

  LOG_INFO("PlanExecutor Start ");

  bool status;
  bool init_failure = false;
  bool single_statement_txn = false;

  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = peloton::concurrency::current_txn;
  // This happens for single statement queries in PG
  if (txn == nullptr) {
    single_statement_txn = true;
    txn = txn_manager.BeginTransaction();
  }
  PL_ASSERT(txn);

  LOG_INFO("Txn ID = %lu ", txn->GetTransactionId());
  LOG_INFO("Building the executor tree");

  // Use const std::vector<Value> &params to make it more elegant for network
  std::unique_ptr<executor::ExecutorContext> executor_context(
      BuildExecutorContext(params, txn));

  // Build the executor tree
  std::unique_ptr<executor::AbstractExecutor> executor_tree(
      BuildExecutorTree(nullptr, plan, executor_context.get()));

  LOG_INFO("Initializing the executor tree");

  // Initialize the executor tree
  status = executor_tree->Init();

  // Abort and cleanup
  if (status == false) {
    init_failure = true;
    txn->SetResult(Result::RESULT_FAILURE);
    goto cleanup;
  }

  LOG_INFO("Running the executor tree");

  // Execute the tree until we get result tiles from root node
  for (;;) {
    status = executor_tree->Execute();

    // Stop
    if (status == false) {
      break;
    }

    std::unique_ptr<executor::LogicalTile> logical_tile(
        executor_tree->GetOutput());

    // Some executors don't return logical tiles (e.g., Update).
    if (logical_tile.get() == nullptr) {
      continue;
    }

    // Go over the logical tile
  }

  // Set the result
  p_status.m_processed = executor_context->num_processed;
  p_status.m_result_slots = nullptr;

// final cleanup
cleanup:

  LOG_INFO("About to commit: single stmt: %d, init_failure: %d, status: %d",
            single_statement_txn, init_failure, txn->GetResult());

  // should we commit or abort ?
  if (single_statement_txn == true || init_failure == true) {
    auto status = txn->GetResult();
    switch (status) {
      case Result::RESULT_SUCCESS:
        // Commit
    	LOG_INFO("Commit Transaction");
        p_status.m_result = txn_manager.CommitTransaction();
        break;

      case Result::RESULT_FAILURE:
      default:
        // Abort
    	LOG_INFO("Abort Transaction");
        p_status.m_result = txn_manager.AbortTransaction();
    }
  }

  // clean up executor tree
  CleanExecutorTree(executor_tree.get());

  return p_status;
}

/**
 * @brief Build a executor tree and execute it.
 * Use std::vector<Value> as params to make it more elegant for networking
 * Before ExecutePlan, a node first receives value list, so we should pass
 * value list directly rather than passing Postgres's ParamListInfo
 * @return number of executed tuples and logical_tile_list
 */
int PlanExecutor::ExecutePlan(
    const planner::AbstractPlan *plan, const std::vector<Value> &params,
    std::vector<std::unique_ptr<executor::LogicalTile>> &logical_tile_list) {
  if (plan == nullptr) return -1;

  LOG_TRACE("PlanExecutor Start ");

  bool status;
  bool init_failure = false;
  bool single_statement_txn = false;

  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = peloton::concurrency::current_txn;

  // This happens for single statement queries in PG
  if (txn == nullptr) {
    single_statement_txn = true;
    txn = txn_manager.BeginTransaction();
  }
  PL_ASSERT(txn);

  LOG_TRACE("Txn ID = %lu ", txn->GetTransactionId());
  LOG_TRACE("Building the executor tree");

  // Use const std::vector<Value> &params to make it more elegant for network
  std::unique_ptr<executor::ExecutorContext> executor_context(
      BuildExecutorContext(params, txn));

  // Build the executor tree
  std::unique_ptr<executor::AbstractExecutor> executor_tree(
      BuildExecutorTree(nullptr, plan, executor_context.get()));

  LOG_TRACE("Initializing the executor tree");

  // Initialize the executor tree
  status = executor_tree->Init();

  // Abort and cleanup
  if (status == false) {
    init_failure = true;
    txn->SetResult(Result::RESULT_FAILURE);
    goto cleanup;
  }

  LOG_TRACE("Running the executor tree");

  // Execute the tree until we get result tiles from root node
  for (;;) {
    status = executor_tree->Execute();

    // Stop
    if (status == false) {
      break;
    }

    std::unique_ptr<executor::LogicalTile> logical_tile(
        executor_tree->GetOutput());

    // Some executors don't return logical tiles (e.g., Update).
    if (logical_tile.get() == nullptr) {
      continue;
    }

    logical_tile_list.push_back(std::move(logical_tile));
  }

// final cleanup
cleanup:

  LOG_TRACE("About to commit: single stmt: %d, init_failure: %d, status: %d",
            single_statement_txn, init_failure, txn->GetResult());

  // clean up executor tree
  CleanExecutorTree(executor_tree.get());

  // should we commit or abort ?
  if (single_statement_txn == true || init_failure == true) {
    auto status = txn->GetResult();
    switch (status) {
      case Result::RESULT_SUCCESS:
        // Commit
        return executor_context->num_processed;

        break;

      case Result::RESULT_FAILURE:
      default:
        // Abort
        return -1;
    }
  }
  return executor_context->num_processed;
}

/**
 * @brief Pretty print the plan tree.
 * @param The plan tree
 * @return none.
 */
void PlanExecutor::PrintPlan(const planner::AbstractPlan *plan,
                             std::string prefix) {
  if (plan == nullptr) {
	  LOG_INFO("Plan is null");
	  return;
  }

  prefix += "  ";

  LOG_INFO("%s->Plan Info :: %s ", prefix.c_str(), plan->GetInfo().c_str());

  auto &children = plan->GetChildren();

  LOG_INFO("Number of children in plan: %d ", (int)children.size());

  for (auto &child : children) {
    PrintPlan(child.get(), prefix);
  }
}

/**
 * @brief Build Executor Context
 */
executor::ExecutorContext *BuildExecutorContext(
    const std::vector<Value> &params, concurrency::Transaction *txn) {
  return new executor::ExecutorContext(txn, params);
}

/**
 * @brief Build the executor tree.
 * @param The current executor tree
 * @param The plan tree
 * @param Transation context
 * @return The updated executor tree.
 */
executor::AbstractExecutor *BuildExecutorTree(
    executor::AbstractExecutor *root, const planner::AbstractPlan *plan,
    executor::ExecutorContext *executor_context) {
  // Base case
  if (plan == nullptr) return root;

  executor::AbstractExecutor *child_executor = nullptr;

  auto plan_node_type = plan->GetPlanNodeType();
  switch (plan_node_type) {
    case PLAN_NODE_TYPE_INVALID:
      LOG_ERROR("Invalid plan node type ");
      break;

    case PLAN_NODE_TYPE_SEQSCAN:
    	LOG_INFO("Adding Sequential Scan Executer");
      child_executor = new executor::SeqScanExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_INDEXSCAN:
    	LOG_INFO("Adding Index Scan Executer");
      child_executor = new executor::IndexScanExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_INSERT:
    	LOG_INFO("Adding Insert Executer");
      child_executor = new executor::InsertExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_DELETE:
    	LOG_INFO("Adding Delete Executer");
      child_executor = new executor::DeleteExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_UPDATE:
    	LOG_INFO("Adding Update Executer");
      child_executor = new executor::UpdateExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_LIMIT:
    	LOG_INFO("Adding Limit Executer");
      child_executor = new executor::LimitExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_NESTLOOP:
    	LOG_INFO("Adding Nested Loop Joing Executer");
      child_executor =
          new executor::NestedLoopJoinExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_MERGEJOIN:
    	LOG_INFO("Adding Merge Join Executer");
      child_executor = new executor::MergeJoinExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_HASH:
    	LOG_INFO("Adding Hash Executer");
      child_executor = new executor::HashExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_HASHJOIN:
    	LOG_INFO("Adding Hash Join Executer");
      child_executor = new executor::HashJoinExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_PROJECTION:
    	LOG_INFO("Adding Projection Executer");
      child_executor = new executor::ProjectionExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_MATERIALIZE:
    	LOG_INFO("Adding Materialization Executer");
      child_executor =
          new executor::MaterializationExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_AGGREGATE_V2:
    	LOG_INFO("Adding Aggregate Executer");
      child_executor = new executor::AggregateExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_ORDERBY:
    	LOG_INFO("Adding Order By Executer");
      child_executor = new executor::OrderByExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_DROP:
    	LOG_INFO("Adding Drop Executer");
      child_executor = new executor::DropExecutor(plan, executor_context);
      break;

    case PLAN_NODE_TYPE_CREATE:
    	LOG_INFO("Adding Create Executer");
      child_executor = new executor::CreateExecutor(plan, executor_context);
      break;

    default:
      LOG_ERROR("Unsupported plan node type : %d ", plan_node_type);
      break;
  }

  // Base case
  if (child_executor != nullptr) {
    if (root != nullptr)
      root->AddChild(child_executor);
    else
      root = child_executor;
  }

  // Recurse
  auto &children = plan->GetChildren();
  for (auto &child : children) {
    child_executor =
        BuildExecutorTree(child_executor, child.get(), executor_context);
  }

  return root;
}

/**
 * @brief Clean up the executor tree.
 * @param The current executor tree
 * @return none.
 */
void CleanExecutorTree(executor::AbstractExecutor *root) {
  if (root == nullptr) return;

  // Recurse
  auto children = root->GetChildren();
  for (auto child : children) {
    CleanExecutorTree(child);
  }
}

}  // namespace bridge
}  // namespace peloton
