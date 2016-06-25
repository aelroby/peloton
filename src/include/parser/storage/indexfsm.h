//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// indexfsm.h
//
// Identification: src/include/parser/storage/indexfsm.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//


/*-------------------------------------------------------------------------
 *
 * indexfsm.h
 *	  POSTGRES free space map for quickly finding an unused page in index
 *
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/storage/indexfsm.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef INDEXFSM_H_
#define INDEXFSM_H_

#include "parser/storage/block.h"
#include "parser/utils/relcache.h"

extern BlockNumber GetFreeIndexPage(Relation rel);
extern void RecordFreeIndexPage(Relation rel, BlockNumber page);
extern void RecordUsedIndexPage(Relation rel, BlockNumber page);

extern void IndexFreeSpaceMapVacuum(Relation rel);

#endif   /* INDEXFSM_H_ */
