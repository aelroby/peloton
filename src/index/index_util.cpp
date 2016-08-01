//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// btree_index.cpp
//
// Identification: src/index/btree_index.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <map>

#include "common/types.h"
#include "common/logger.h"
#include "common/value.h"
#include "common/value_factory.h"

#include "index/index_util.h"
#include "index/index.h"

namespace peloton {
namespace index {

bool IfForwardExpression(ExpressionType e) {
  if (e == EXPRESSION_TYPE_COMPARE_GREATERTHAN ||
      e == EXPRESSION_TYPE_COMPARE_GREATERTHANOREQUALTO) {
    return true;
  }
  return false;
}

bool IfBackwardExpression(ExpressionType e) {
  if (e == EXPRESSION_TYPE_COMPARE_LESSTHAN ||
      e == EXPRESSION_TYPE_COMPARE_LESSTHANOREQUALTO) {
    return true;
  }
  return false;
}

bool ValuePairComparator(const std::pair<peloton::Value, int> &i,
                         const std::pair<peloton::Value, int> &j) {
  if (i.first.Compare(j.first) == VALUE_COMPARE_EQUAL) {
    return i.second < j.second;
  }
  return i.first.Compare(j.first) == VALUE_COMPARE_LESSTHAN;
}

void ConstructIntervals(oid_t leading_column_id,
                        const std::vector<Value> &values,
                        const std::vector<oid_t> &key_column_ids,
                        const std::vector<ExpressionType> &expr_types,
                        std::vector<std::pair<Value, Value>> &intervals) {
  // Find all contrains of leading column.
  // Equal --> > < num
  // > >= --->  > num
  // < <= ----> < num
  std::vector<std::pair<peloton::Value, int>> nums;
  for (size_t i = 0; i < key_column_ids.size(); i++) {
    if (key_column_ids[i] != leading_column_id) {
      continue;
    }

    // If leading column
    if (IfForwardExpression(expr_types[i])) {
      nums.push_back(std::pair<Value, int>(values[i], -1));
    } else if (IfBackwardExpression(expr_types[i])) {
      nums.push_back(std::pair<Value, int>(values[i], 1));
    } else {
      assert(expr_types[i] == EXPRESSION_TYPE_COMPARE_EQUAL);
      nums.push_back(std::pair<Value, int>(values[i], -1));
      nums.push_back(std::pair<Value, int>(values[i], 1));
    }
  }

  // Have merged all constraints in a single line, sort this line.
  std::sort(nums.begin(), nums.end(), Index::ValuePairComparator);
  assert(nums.size() > 0);

  // Build intervals.
  Value cur;
  size_t i = 0;
  if (nums[0].second < 0) {
    cur = nums[0].first;
    i++;
  } else {
    cur = Value::GetMinValue(nums[0].first.GetValueType());
  }

  while (i < nums.size()) {
    if (nums[i].second > 0) {
      if (i + 1 < nums.size() && nums[i + 1].second < 0) {
        // right value
        intervals.push_back(std::pair<Value, Value>(cur, nums[i].first));
        cur = nums[i + 1].first;
      } else if (i + 1 == nums.size()) {
        // Last value while right value
        intervals.push_back(std::pair<Value, Value>(cur, nums[i].first));
        cur = Value::GetNullValue(nums[0].first.GetValueType());
      }
    }
    i++;
  }

  if (cur.IsNull() == false) {
    intervals.push_back(std::pair<Value, Value>(
        cur, Value::GetMaxValue(nums[0].first.GetValueType())));
  }

  // Finish invtervals building.
};

void FindMaxMinInColumns(oid_t leading_column_id,
                         const std::vector<Value> &values,
                         const std::vector<oid_t> &key_column_ids,
                         const std::vector<ExpressionType> &expr_types,
                         std::map<oid_t, std::pair<Value, Value>> &non_leading_columns) {
  // find extreme nums on each column.
  LOG_TRACE("FindMinMax leading column %d\n", leading_column_id);
  for (size_t i = 0; i < key_column_ids.size(); i++) {
    oid_t column_id = key_column_ids[i];
    if (column_id == leading_column_id) {
      continue;
    }

    if (non_leading_columns.find(column_id) == non_leading_columns.end()) {
      auto type = values[i].GetValueType();
      // std::pair<Value, Value> *range = new std::pair<Value,
      // Value>(Value::GetMaxValue(type),
      //                                            Value::GetMinValue(type));
      // std::pair<oid_t, std::pair<Value, Value>> key_value(column_id, range);
      non_leading_columns.insert(std::pair<oid_t, std::pair<Value, Value>>(
          column_id, std::pair<Value, Value>(Value::GetNullValue(type),
                                             Value::GetNullValue(type))));
      //  non_leading_columns[column_id] = *range;
      // delete range;
      LOG_TRACE("Insert a init bounds\tleft size %lu\t right description %s\n",
                non_leading_columns[column_id].first.GetInfo().size(),
                non_leading_columns[column_id].second.GetInfo().c_str());
    }

    if (IfForwardExpression(expr_types[i]) ||
        expr_types[i] == EXPRESSION_TYPE_COMPARE_EQUAL) {
      LOG_TRACE("min cur %lu compare with %s\n",
                non_leading_columns[column_id].first.GetInfo().size(),
                values[i].GetInfo().c_str());
      if (non_leading_columns[column_id].first.IsNull() ||
          non_leading_columns[column_id].first.Compare(values[i]) ==
              VALUE_COMPARE_GREATERTHAN) {
        LOG_TRACE("Update min\n");
        non_leading_columns[column_id].first =
            ValueFactory::Clone(values[i], nullptr);
      }
    }

    if (IfBackwardExpression(expr_types[i]) ||
        expr_types[i] == EXPRESSION_TYPE_COMPARE_EQUAL) {
      LOG_TRACE("max cur %s compare with %s\n",
                non_leading_columns[column_id].second.GetInfo().c_str(),
                values[i].GetInfo().c_str());
      if (non_leading_columns[column_id].first.IsNull() ||
          non_leading_columns[column_id].second.Compare(values[i]) ==
              VALUE_COMPARE_LESSTHAN) {
        LOG_TRACE("Update max\n");
        non_leading_columns[column_id].second =
            ValueFactory::Clone(values[i], nullptr);
      }
    }
  }

  // check if min value is right bound or max value is left bound, if so, update
  for (const auto &k_v : non_leading_columns) {
    if (k_v.second.first.IsNull()) {
      non_leading_columns[k_v.first].first =
          Value::GetMinValue(k_v.second.first.GetValueType());
    }
    if (k_v.second.second.IsNull()) {
      non_leading_columns[k_v.first].second =
          Value::GetMaxValue(k_v.second.second.GetValueType());
    }
  }
};

}  // End index namespace
}  // End peloton namespace
