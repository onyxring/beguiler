// ═══════════════════════════════════════════════════════════════════════════════
// bglParserHelpers.h — small pure helpers shared across bglParser*.cpp files.
//
// Functions here are free (non-member) and have no side effects on parser state.
// They were originally `static` helpers in bglParser.cpp; promoted here so the
// expression-parser extraction (and future extractions) can call them from
// other .cpp files without duplication.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once
#include <string>
#include "typeDef.h"

// Replace whole-word occurrences of `from` with `to` in `str`, case-insensitively.
// Word boundaries are non-alnum and non-`_`/non-`$` (so identifier-like names are
// matched whole, but `.member` accesses are NOT — `$` is treated as part of an
// identifier for substitution-placeholder safety). Note: distinct from
// i6Emitter::replaceWord, which additionally treats `.` as a left-disqualifier.
std::string replaceWord(std::string str, const std::string& from, const std::string& to);

// Look up a type's original-case display name from the language service. Falls
// back to the (lowercased) input when the type has no recorded displayName.
std::string typeDisplayName(const std::string& typeName);

// Build a valid I6 property identifier from an operator symbol, e.g. "=" → "_opeq".
// Used so non-emitter operator routines can be defined as I6 methods and called
// as `lhs._opXX(rhs)`. The mapping must be stable across all callers.
std::string mangleOperatorName(const std::string& opName);

// Recursively check whether a statementBlock contains any return statement on
// any execution path. Used for lambda return-type inference: a lambda body
// without any return is inferred `void`; otherwise the inferred type is `var`.
bool hasReturn(statementBlock* blk);

// True iff every execution path through `blk` is guaranteed to terminate with a
// `return` statement. Criteria: a top-level return; an if-else where both arms
// all-paths-return; a switch with a default case where every case body all-
// paths-returns. Loops (for/while/do) do NOT count — body may not execute.
bool allPathsReturn(statementBlock* blk);

// True if a class has at least one stored (non-emitter, non-static, non-attribute,
// non-grammar) data field declared directly on it — a field whose value would
// need to be copied for value-semantics assignment to be meaningful. Own-members-
// only by design (does NOT walk the hierarchy).
bool classHasStoredFields(classDef* cls);

// True if the class participates in the I6 world tree — direct or transitive
// inheritance from `object`. Alias classes are also treated as tree citizens
// because they dissolve to their parent for I6 emission. Gates the value-
// semantics-operator= silent-emission error.
bool inheritsFromObject(classDef* cls);

// True iff the variable named `name` is declared `const` in the current scope.
// Searches: enclosing statement block's locals, the enclosing function's outer
// body's locals, then global variableDeclarations. Returns false for unknown
// names, enums, primitives — they have no const-ness to query.
bool isConstVariable(const std::string& name, functionDef* func, statementBlock* body);
