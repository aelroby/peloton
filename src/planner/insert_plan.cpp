
//===----------------------------------------------------------------------===//
//
//                         PelotonDB
//
// insert_plan.cpp
//
// Identification: /peloton/src/planner/insert_plan.cpp
//
// Copyright (c) 2015, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
 
#include "planner/insert_plan.h"
#include "planner/project_info.h"
#include "storage/data_table.h"
#include "storage/tuple.h"
#include "parser/insert_parse.h"
#include "parser/statement_select.h"
#include "parser/statement_insert.h"
#include "catalog/column.h"
#include "catalog/bootstrapper.h"

namespace peloton{
namespace planner{
InsertPlan::InsertPlan(storage::DataTable *table, oid_t bulk_insert_count)
    : target_table_(table), bulk_insert_count(bulk_insert_count) {
}

// This constructor takes in a project info
InsertPlan::InsertPlan(
    storage::DataTable *table,
    std::unique_ptr<const planner::ProjectInfo> &&project_info,
    oid_t bulk_insert_count)
    : target_table_(table),
      project_info_(std::move(project_info)),
      bulk_insert_count(bulk_insert_count) {
}

// This constructor takes in a tuple
InsertPlan::InsertPlan(storage::DataTable *table,
                    std::unique_ptr<storage::Tuple> &&tuple,
                    oid_t bulk_insert_count)
    : target_table_(table),
      tuple_(std::move(tuple)),
      bulk_insert_count(bulk_insert_count) {
}

// This constructor takes a parse tree
InsertPlan::InsertPlan(parser::InsertParse *parse_tree, oid_t bulk_insert_count) : bulk_insert_count(bulk_insert_count) {

  std::vector<std::string> columns = parse_tree->getColumns();
  std::vector<Value> values = parse_tree->getValues();

  target_table_ = catalog::Bootstrapper::global_catalog->GetTableFromDatabase(DEFAULT_DB_NAME, parse_tree->GetTableName());
  PL_ASSERT(target_table_);
  catalog::Schema* table_schema = target_table_->GetSchema();

  if(columns.size() == 0){ // INSERT INTO table_name VALUES (val1, val2, ...);
	    PL_ASSERT(values.size() == table_schema->GetColumnCount());
	    std::unique_ptr<storage::Tuple> tuple(new storage::Tuple(table_schema, true));
	    int col_cntr = 0;
	    for(Value const& elem : values) {
	    	switch (elem.GetValueType()) {
	    	  case VALUE_TYPE_VARCHAR:
	    	  case VALUE_TYPE_VARBINARY:
	    		  tuple->SetValue(col_cntr++, elem, GetPlanPool());
	    		  break;

	    	    default: {
	    	    	tuple->SetValue(col_cntr++, elem, nullptr);
	    	    }
	    	  }
	    }
	    tuple_ = std::move(tuple);
  }
  else{  // INSERT INTO table_name (col1, col2, ...) VALUES (val1, val2, ...);
	    PL_ASSERT(columns.size() <= table_schema->GetColumnCount());  // columns has to be less than or equal that of schema
	    std::unique_ptr<storage::Tuple> tuple(new storage::Tuple(table_schema, true));
	    int col_cntr = 0;
	    auto table_columns = table_schema->GetColumns();
	    for(catalog::Column const& elem : table_columns){
	    	std::size_t pos = std::find(columns.begin(), columns.end(), elem.GetName()) - columns.begin();
			switch (elem.GetType()) {
			case VALUE_TYPE_VARCHAR:
			case VALUE_TYPE_VARBINARY: {
				if(pos >= columns.size()) {
					tuple->SetValue(col_cntr, ValueFactory::GetNullStringValue(), GetPlanPool());
				}
				else {
					tuple->SetValue(col_cntr, values[pos], GetPlanPool());
				}
				break;
			}

			default: {
				if(pos >= columns.size()) {
					tuple->SetValue(col_cntr, ValueFactory::GetNullStringValue(), GetPlanPool());
				}
				else {
					tuple->SetValue(col_cntr, values[pos], nullptr);
				}
			}
			}
			++col_cntr;
	    }
	    tuple_ = std::move(tuple);
  }
  LOG_INFO("Tuple to be inserted: %s", tuple_->GetInfo().c_str());
}

InsertPlan::InsertPlan(parser::InsertStatement *parse_tree, oid_t bulk_insert_count) : bulk_insert_count(bulk_insert_count) {

  auto values = parse_tree->values;
  auto cols = parse_tree->columns;

  target_table_ = catalog::Bootstrapper::global_catalog->GetTableFromDatabase(DEFAULT_DB_NAME, parse_tree->table_name);
  if(target_table_){
	  catalog::Schema* table_schema = target_table_->GetSchema();
	  if(cols == NULL){
			PL_ASSERT(values->size() == table_schema->GetColumnCount());
			std::unique_ptr<storage::Tuple> tuple(new storage::Tuple(table_schema, true));
			int col_cntr = 0;
			for(expression::AbstractExpression* elem : *values) {
				expression::ConstantValueExpression *const_expr_elem = dynamic_cast<expression::ConstantValueExpression *>(elem);
				switch (const_expr_elem->GetValueType()) {
				  case VALUE_TYPE_VARCHAR:
				  case VALUE_TYPE_VARBINARY:
					  tuple->SetValue(col_cntr++, const_expr_elem->getValue(), GetPlanPool());
					  break;

					default: {
						tuple->SetValue(col_cntr++, const_expr_elem->getValue(), nullptr);
					}
				  }
			}
			tuple_ = std::move(tuple);
	  }
	  else{ // INSERT INTO table_name (col1, col2, ...) VALUES (val1, val2, ...);
			PL_ASSERT(cols->size() <= table_schema->GetColumnCount());  // columns has to be less than or equal that of schema
			std::unique_ptr<storage::Tuple> tuple(new storage::Tuple(table_schema, true));
			int col_cntr = 0;
			auto table_columns = table_schema->GetColumns();
			auto query_columns = parse_tree->columns;
			for(catalog::Column const& elem : table_columns){
				std::size_t pos = std::find(query_columns->begin(), query_columns->end(), elem.GetName()) - query_columns->begin();
				switch (elem.GetType()) {
				case VALUE_TYPE_VARCHAR:
				case VALUE_TYPE_VARBINARY: {
					if(pos >= query_columns->size()) { // Not found. Set value to null
						tuple->SetValue(col_cntr, ValueFactory::GetNullStringValue(), GetPlanPool());
					}
					else {
						expression::ConstantValueExpression *const_expr_elem = dynamic_cast<expression::ConstantValueExpression *>(values->at(pos));
						tuple->SetValue(col_cntr, const_expr_elem->getValue(), GetPlanPool());
					}
					break;
				}

				default: {
					if(pos >= query_columns->size()) {  // If the column does not exist, insert a null value as string
						tuple->SetValue(col_cntr, ValueFactory::GetNullStringValue(), GetPlanPool());
					}
					else {
						expression::ConstantValueExpression *const_expr_elem = dynamic_cast<expression::ConstantValueExpression *>(values->at(pos));
						tuple->SetValue(col_cntr, const_expr_elem->getValue(), nullptr);
					}
				}
				}
				++col_cntr;
			}
			tuple_ = std::move(tuple);
	  }
	  LOG_INFO("Tuple to be inserted: %s", tuple_->GetInfo().c_str());
  }
  else {
	  LOG_INFO("Table does not exist!");
  }
}

VarlenPool *InsertPlan::GetPlanPool() {
  // construct pool if needed
  if (pool_.get() == nullptr) pool_.reset(new VarlenPool(BACKEND_TYPE_MM));
  // return pool
  return pool_.get();
}

}
}
