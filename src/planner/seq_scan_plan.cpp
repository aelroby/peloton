//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// seq_scan_plan.cpp
//
// Identification: src/planner/seq_scan_plan.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//


#include "planner/seq_scan_plan.h"
#include "storage/data_table.h"
#include "catalog/manager.h"
#include "common/types.h"
#include "common/macros.h"
#include "common/logger.h"

#include "catalog/bootstrapper.h"
#include "catalog/schema.h"

#include "parser/statement_select.h"


namespace peloton {
namespace planner {

//===--------------------------------------------------------------------===//
// Serialization/Deserialization
//===--------------------------------------------------------------------===//

/**
 * The SeqScanPlan has the following members:
 *   database_id, table_id, predicate, column_id, parent(might be NULL)
 * TODO: SeqScanPlan doesn't have children, so we don't need to handle it
 *
 * Therefore a SeqScanPlan is serialized as:
 * [(int) total size]
 * [(int8_t) plan type]
 * [(int) database_id]
 * [(int) table_id]
 * [(int) num column_id]
 * [(int) column id...]
 * [(int8_t) expr type]     : if invalid, predicate is null
 * [(bytes) predicate]      : predicate is Expression
 * [(int8_t) plan type]     : if invalid, parent is null
 * [(bytes) parent]         : parent is also a plan
 *
 * TODO: parent_ seems never be set or used
 */


SeqScanPlan::SeqScanPlan(parser::SelectStatement* select_node) {

  auto target_table = static_cast<storage::DataTable *>(catalog::Bootstrapper::global_catalog->GetTableFromDatabase(DEFAULT_DB_NAME, select_node->from_table->name));
  SetTargetTable(target_table);
  ColumnIds().clear();
  
  if(select_node->select_list->at(0)->GetExpressionType() != EXPRESSION_TYPE_STAR){
    for(auto col : *select_node->select_list){
      LOG_INFO("ExpressionType -------------> %d" ,col->GetExpressionType());
      auto col_name = col->getName();
      oid_t col_id = SeqScanPlan::GetColumnID(std::string(col_name));
      SetColumnId(col_id);
    }
  }

  else{
    auto allColumns = GetTable()->GetSchema()->GetColumns();
    for(uint i = 0; i < allColumns.size() ; i++)
      SetColumnId(i);
  }

  if(select_node->where_clause != NULL){
    auto pred = select_node->where_clause->Copy();
    ReplaceColumnExpressions(pred);
    SetPredicate(pred);
  
  }
  
  // if(select_node->limit != NULL){
  //   std::unique_ptr<planner::AbstractPlan> limit_plan(new planner::LimitPlan(select_node->limit->limit, select_node->limit->offset));
  //   this->AddChild(std::move(limit_plan));
  // }


}


bool SeqScanPlan::SerializeTo(SerializeOutput &output) {
  // A placeholder for the total size written at the end
  int start = output.Position();
  output.WriteInt(-1);

  // Write the SeqScanPlan type
  PlanNodeType plan_type = GetPlanNodeType();
  output.WriteByte(static_cast<int8_t>(plan_type));

  // Write database id and table id
  if (!GetTable()) {
    // The plan is not completed
    return false;
  }
  oid_t database_id = GetTable()->GetDatabaseOid();
  oid_t table_id = GetTable()->GetOid();

  output.WriteInt(static_cast<int>(database_id));
  output.WriteInt(static_cast<int>(table_id));

  // If column has 0 item, just write the columnid_count with 0
  int columnid_count = GetColumnIds().size();
  output.WriteInt(columnid_count);

  // If column has 0 item, nothing happens here
  for (int it = 0; it < columnid_count; it++) {
    oid_t col_id = GetColumnIds()[it];
    output.WriteInt(static_cast<int>(col_id));
  }

  // Write predicate
  if (GetPredicate() == nullptr) {
    // Write the type
    output.WriteByte(static_cast<int8_t>(EXPRESSION_TYPE_INVALID));
  } else {
    // Write the expression type
    ExpressionType expr_type = GetPredicate()->GetExpressionType();
    output.WriteByte(static_cast<int8_t>(expr_type));

    // Write predicate
    GetPredicate()->SerializeTo(output);
  }

  // Write parent, but parent seems never be set or used right now
  if (GetParent() == nullptr) {
    // Write the type
    output.WriteByte(static_cast<int8_t>(PLAN_NODE_TYPE_INVALID));
  } else {
    // Write the parent type
    PlanNodeType parent_type = GetParent()->GetPlanNodeType();
    output.WriteByte(static_cast<int8_t>(parent_type));

    // Write parent
    GetParent()->SerializeTo(output);
  }

  // Write the total length
  int32_t sz = static_cast<int32_t>(output.Position() - start - sizeof(int));
  PL_ASSERT(sz > 0);
  output.WriteIntAt(start, sz);

  return true;
}

/**
   * Therefore a SeqScanPlan is serialized as:
   * [(int) total size]
   * [(int8_t) plan type]
   * [(int) database_id]
   * [(int) table_id]
   * [(int) num column_id]
   * [(int) column id...]
   * [(int8_t) expr type]     : if invalid, predicate is null
   * [(bytes) predicate]      : predicate is Expression
   * [(int8_t) plan type]     : if invalid, parent is null
   * [(bytes) parent]         : parent is also a plan
 */
bool SeqScanPlan::DeserializeFrom(SerializeInputBE &input) {
  // Read the size of SeqScanPlan class
  input.ReadInt();

  // Read the type
  UNUSED_ATTRIBUTE PlanNodeType plan_type =
      (PlanNodeType)input.ReadEnumInSingleByte();
  PL_ASSERT(plan_type == GetPlanNodeType());

  // Read database id
  oid_t database_oid = input.ReadInt();

  // Read table id
  oid_t table_oid = input.ReadInt();

  // Get table and set it to the member
  storage::DataTable *target_table = static_cast<storage::DataTable *>(
      catalog::Manager::GetInstance().GetTableWithOid(database_oid, table_oid));
  SetTargetTable(target_table);

  // Read the number of column_id and set them to column_ids_
  oid_t columnid_count = input.ReadInt();
  for (oid_t it = 0; it < columnid_count; it++) {
    oid_t column_id = input.ReadInt();
    SetColumnId(column_id);
  }

  // Read the type
  ExpressionType expr_type = (ExpressionType)input.ReadEnumInSingleByte();

  // Predicate deserialization
  if (expr_type != EXPRESSION_TYPE_INVALID) {
    switch (expr_type) {
      //            case EXPRESSION_TYPE_COMPARE_IN:
      //                predicate_ =
      //                std::unique_ptr<EXPRESSION_TYPE_COMPARE_IN>(new
      //                ComparisonExpression (101));
      //                predicate_.DeserializeFrom(input);
      //              break;

      default: {
        LOG_ERROR(
            "Expression deserialization :: Unsupported EXPRESSION_TYPE: %u ",
            expr_type);
        break;
      }
    }
  }

  // Read the type of parent
  PlanNodeType parent_type = (PlanNodeType)input.ReadEnumInSingleByte();

  // Parent deserialization
  if (parent_type != PLAN_NODE_TYPE_INVALID) {
    switch (expr_type) {
      //            case EXPRESSION_TYPE_COMPARE_IN:
      //                predicate_ =
      //                std::unique_ptr<EXPRESSION_TYPE_COMPARE_IN>(new
      //                ComparisonExpression (101));
      //                predicate_.DeserializeFrom(input);
      //              break;

      default: {
        LOG_ERROR("Parent deserialization :: Unsupported PlanNodeType: %u ",
                  expr_type);
        break;
      }
    }
  }

  return true;
}
/**
 *
 * SeqScanPlan is serialized as:
 * [(int) total size]
 * [(int8_t) plan type]
 * [(int) database_id]
 * [(int) table_id]
 * [(int) num column_id]
 * [(int) column id...]
 * [(int8_t) expr type]     : if invalid, predicate is null
 * [(bytes) predicate]      : predicate is Expression
 * [(int8_t) plan type]     : if invalid, parent is null
 * [(bytes) parent]         : parent is also a plan
 *
 * So, the fixed size part is:
 *      [(int) total size]   4 +
 *      [(int8_t) plan type] 1 +
 *      [(int) database_id]  4 +
 *      [(int) table_id]     4 +
 *      [(int) num column_id]4 +
 *      [(int8_t) expr type] 1 +
 *      [(int8_t) plan type] 1 =
 *     the variant part is :
 *      [(int) column id...]: num column_id * 4
 *      [(bytes) predicate] : predicate->GetSerializeSize()
 *      [(bytes) parent]    : parent->GetSerializeSize()
 */
int SeqScanPlan::SerializeSize() {
  // Fixed size. see the detail above
  int size_fix = sizeof(int) * 4 + 3;
  int size_columnids = ColumnIds().size() * sizeof(int);
  int size = size_fix + size_columnids;

  if (Predicate()) {
    size = size + Predicate()->SerializeSize();
  }
  if (Parent()) {
    size = size + Parent()->SerializeSize();
  }

  return size;
}

oid_t SeqScanPlan::GetColumnID(std::string col_name){
  auto columns = GetTable()->GetSchema()->GetColumns();
  oid_t index = -1;
      for(oid_t i = 0; i < columns.size(); ++i) {
        if(columns[i].column_name == col_name){
          index = i;
          break;
        }
      }
      return index;
}

void SeqScanPlan::ReplaceColumnExpressions(expression::AbstractExpression* expression) {
  LOG_INFO("Expression Type --> %s", ExpressionTypeToString(expression->GetExpressionType()).c_str());
  LOG_INFO("Left Type --> %s", ExpressionTypeToString(expression->GetLeft()->GetExpressionType()).c_str());
  LOG_INFO("Right Type --> %s", ExpressionTypeToString(expression->GetRight()->GetExpressionType()).c_str());
  if(expression->GetLeft()->GetExpressionType() == EXPRESSION_TYPE_COLUMN_REF) {
    auto expr = expression->GetLeft();
    std::string col_name(expr->getName());
    LOG_INFO("Column name: %s", col_name.c_str());
    delete expr;
    expression->setLeft(ConvertToTupleValueExpression(col_name));
  }
  else if (expression->GetRight()->GetExpressionType() == EXPRESSION_TYPE_COLUMN_REF) {
    auto expr = expression->GetRight();
    std::string col_name(expr->getName());
    LOG_INFO("Column name: %s", col_name.c_str());
    delete expr;
    expression->setRight(ConvertToTupleValueExpression(col_name));
  }
  else {
    ReplaceColumnExpressions(expression->GetModifiableLeft());
    ReplaceColumnExpressions(expression->GetModifiableRight());

  }
}
/**
 * This function generates a TupleValue expression from the column name
 */
expression::AbstractExpression* SeqScanPlan::ConvertToTupleValueExpression (std::string column_name) {
  auto schema = GetTable()->GetSchema();
    auto column_id = schema->GetColumnID(column_name);
    LOG_INFO("Column id in table: %u", column_id);
    expression::TupleValueExpression *expr =
        new expression::TupleValueExpression(schema->GetType(column_id), 0, column_id);
  return expr;
}


}  // namespace planner
}  // namespace peloton
