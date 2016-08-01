//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// statement_update.h
//
// Identification: src/include/parser/statement_update.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include "parser/sql_statement.h"

namespace peloton {
namespace parser {

/**
 * @struct UpdateClause
 * @brief Represents "column = value" expressions
 */
class UpdateClause {
 public:
  char* column;
  expression::AbstractExpression* value;

  ~UpdateClause() {
    free (column);
    delete value;
  }
};

/**
 * @struct UpdateStatement
 * @brief Represents "UPDATE"
 */
struct UpdateStatement : SQLStatement {
  UpdateStatement()
      : SQLStatement(STATEMENT_TYPE_UPDATE),
        table(NULL),
        updates(NULL),
        where(NULL) {}

  virtual ~UpdateStatement() {
    delete table;

    if (updates) {
      for (auto clause : *updates) delete clause;
    }

    delete updates;
    delete where;
  }

  // TODO: switch to char* instead of TableRef
  TableRef* table;
  std::vector<UpdateClause*>* updates;
  expression::AbstractExpression* where;
};

}  // End parser namespace
}  // End peloton namespace
