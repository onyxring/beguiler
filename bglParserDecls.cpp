// ===============================================================================
// bglParserDecls.cpp - non-class/object declarations: enum, variable, array,
// routine, grammar.
//
// Extracted from bglParser.cpp (Phase 3 of the refactor). Covers everything
// declaration-shaped that doesn't belong in bglParserClassObjectDecl.cpp.
//
//   processEnumDeclaration            - enum / bnum / extern enum / extern bnum
//   processArrayDeclaration           - array<T> name[N] = { ... };
//   processVariableDeclaration        - T name; / T name = value;
//   processRoutineDeclaration         - free function declarations
//   processGrammarDeclaration         - grammar foo { ... } at file scope
//   processGrammarObjectDeclaration   - the grammar-object's instance body
//   parseOptionalGrammarRulePriority  - optional priority at end of a grammar rule
//   parseGrammarLineContent           - parse one { trigger, pattern, -> action } line
//   parseGrammarLines                 - parse the outer { line, line } around grammar
// ===============================================================================
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype>
#include <tuple>
#include <optional>
#include <string_view>

#include "helpers.h"
#include "settings.h"
#include "typeDef.h"
#include "bglParser.h"
#include "fileLexer.h"
#include "token.h"
#include "bglLanguageService.h"
#include "bglParserHelpers.h"

using namespace std;


// ===============================================================================
// processEnumDeclaration
// ===============================================================================
bool bglParser::processEnumDeclaration(token tok, bool isExternal, token nameOverride){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Enumerations are only allowed in global context:'{0}'", (string) tok));
    bool isBnum=false;
    if(tok.is(token::bnumDeclaration)) isBnum=true;
    token name = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken({eTokenType::identifier, eTokenType::dataType}); //enum name
    enumDef& newEnum=languageService.registerEnum((string)name, isExternal, name.originalValue);
    newEnum.isBnum = isBnum;
    if(!tok.docComment.empty())          newEnum.docComment = tok.docComment;
    else if(!name.docComment.empty())    newEnum.docComment = name.docComment;
    // Optional shared-base clause (mirrors pre-scanner); see bglPreScanner.cpp for semantics.
    if(file.peekToken().is(":")){
        file.getToken();
        token baseTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        if(!isBnum)
            parsingError(format("enum '{0}': shared-base inheritance is only valid for bnum declarations", newEnum.dName()));
        enumDef* base = dynamic_cast<enumDef*>(&languageService.getType(baseTok.value));
        if(!base || !base->isBnum)
            parsingError(format("bnum '{0}': base '{1}' is not a declared bnum", newEnum.dName(), baseTok.originalValue));
        newEnum.baseBnum = base;
    }
    bool hasBase = newEnum.baseBnum != nullptr;
    tok=file.getToken(token::braceOpen);
    // If the pre-scanner already populated values, consume the body and return
    bool alreadyPopulated = !newEnum.namedValues.empty();
    int val=1;
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside enum — missing closing '}'");
        tok=file.getToken();
        if(tok.is(token::braceClose)) break;
        if(alreadyPopulated){
            // Just drain remaining tokens until closing brace
            while(tok.isNot(token::braceClose) && tok.isNot(eTokenType::eof)) tok=file.getToken();
            break;
        }
        enumValueDef& newVal=*new enumValueDef();
        newVal.name=tok.value;
        newVal.displayName=tok.originalValue;
        newVal.docComment=tok.docComment;  // doc-comment attached to the value's name token
        tok=file.getToken({token::braceClose, token::comma, token::assignment});
        if(tok.is(token::assignment)){
            bool negate = false;
            if(file.peekToken().is("-")){ file.getToken(); negate = true; }
            token numTok=file.getToken(eTokenType::integer);
            val=stoi(numTok.value);
            if(negate) val = -val;
            if(isBnum && negate)
                parsingError(format("bnum '{0}': negative value {1} is not allowed", newEnum.dName(), val));
            if(isBnum && !hasBase && val != 0 && (val & (val - 1)) != 0)
                parsingError(format("bnum '{0}': explicit value {1} is not a power of 2", newEnum.dName(), val));
            tok=file.getToken({token::braceClose, token::comma});
        }
        newVal.value=val;
        if(isBnum)
            val<<=1;
        else
            val++;
        newEnum.namedValues.push_back(&newVal);
    }
    return false;
}


// ===============================================================================
// Array / variable / routine declarations
// ===============================================================================
bool bglParser::processArrayDeclaration(token dataType, token name, string elementType, token symbol, abstractObject& contextObj, bool isExternal) {
    arrayDeclaration& arrDecl = *(new arrayDeclaration());
    arrDecl.src = file.currentLocation();
    arrDecl.name = (string)name;
    arrDecl.type = languageService.getType(elementType == "char" ? "bytearray" : "array");
    arrDecl.elementType = elementType;
    arrDecl.isExternal = isExternal;
    if(elementType == "char") arrDecl.isByteArray = true;

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;

    if(symbol.is(token::bracketOpen)) {
        // array<T> name[N];
        token sizeTok = file.getToken(eTokenType::integer);
        arrDecl.arraySize = stoi(sizeTok.value);
        file.getToken(token::bracketClose);
        file.getToken(token::endStatement);
    } else if(symbol.is(token::assignment)) {
        // Check for string initializer: array<char> name = "text";
        token firstVal = file.peekToken(1);
        if(firstVal.is(eTokenType::quote) || firstVal.is(eTokenType::rawQuote)){
            if(elementType != "char" && elementType != "charliteral")
                parsingError("String initializer is only valid for array<char>");
            token strTok = file.getToken();
            arrDecl.stringInitializer = strTok.value;
            file.getToken(token::endStatement);
        } else if(firstVal.is(token::braceOpen)){
            // array<T> name = { v1, v2, ... };
            file.getToken(token::braceOpen);
            initializerList* list = new initializerList();
            token t = file.getToken();
            while(!t.is(token::braceClose) && !t.is(eTokenType::eof)) {
                expression* elem = parseExpression(t, {",", token::braceClose}, func, body);
                list->elements.push_back(elem);
                if(elem->terminator == token::braceClose) break;
                t = file.getToken();
            }
            file.getToken(token::endStatement);
            // type-check each element against the declared element type
            if(!elementType.empty() && elementType != "var"){
                for(size_t i = 0; i < list->elements.size(); i++){
                    expression* elem = list->elements[i];
                    if(!elem->resolvedType.empty() && !isTypeCompatible(elem->resolvedType, elementType))
                        parsingError(format("Array element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, elementType));
                }
            }
            arrDecl.declaredExpressionValue = list;
        } else {
            // array<T> name = expression; — pointer-aliasing init. The RHS is an arbitrary
            // expression evaluating to an array pointer (typically a function return or another
            // array global). No storage is allocated for this local; the slot just holds the
            // pointer. arraySize stays 0 so the emitter's local-array allocation path skips it.
            token first = file.getToken();
            expression* expr = parseExpression(first, {token::endStatement}, func, body);
            arrDecl.declaredExpressionValue = expr;
        }
    }
    // else symbol is endStatement: extern/forward declaration — no size or initializer

    // Local byte arrays draw hybrid-buffer backing from the framePool, but only the
    // sized form (array<char> buf[N]) is wired up. String/list initializers would
    // need their literal bytes copied in per call, which isn't implemented — reject
    // them with guidance rather than emitting an unallocated pointer.
    if(body != nullptr && arrDecl.isByteArray
       && (!arrDecl.stringInitializer.empty() || dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue)))
        parsingError("Initialized local byte arrays (array<char> = \"...\" or {...}) are not yet supported. "
                     "Declare it at file scope, or use a sized local (array<char> buf[N]) and assign elements.");

    if(body != nullptr)
        body->statements.push_back(&arrDecl);
    else
        languageService.registerInstance(arrDecl);
    return false;
}

//--process a variable declaration statement and add it to the parse tree
//-- handles both simple declarations and declarations with assignment
//-- also handles parameter declarations within function definitions
//-- e.g.:
//--      int myVar;
//--      int myVar=99;
//--      int myParam)
//--      int myParam=5) 
//--      int myParam=5, ...

bool bglParser::processVariableDeclaration(token dataType, token variableName, token symbol, abstractObject& contextObj, bool isExternal, bool isConst, string i6alias, bool isRef){
    // Ban bare `array` as a type — every array variable must declare its element type.
    // `array<T>` / `array<char>` are handled earlier via processArrayDeclaration and never
    // reach here as a plain variable declaration.
    if(dataType.value == "array")
        parsingError("bare 'array' is not a valid type — declare the element type: array<T>");
    // Extern Type name ; where Type is verb-derived → route to object declaration (verb instance)
    if(isExternal && symbol.is(token::endStatement)){
        bool verbDerived = false;
        if(classDef* cls = dynamic_cast<classDef*>(&languageService.getType(dataType.value))){
            function<bool(classDef*)> checkVerb = [&](classDef* c) -> bool {
                if(!c) return false;
                if(c->name == "verb") return true;
                for(classDef* b : c->baseClasses) if(checkVerb(b)) return true;
                return false;
            };
            verbDerived = checkVerb(cls);
        }
        if(verbDerived)
            return processObjectDeclaration(dataType, variableName, true, "", "", false);
    }
    variableDeclaration& varDecl = *new variableDeclaration();
    varDecl.src = file.currentLocation();
    varDecl.name=(string) variableName;
    varDecl.displayName = variableName.originalValue;
    if(!dataType.docComment.empty())          varDecl.docComment = dataType.docComment;
    else if(!variableName.docComment.empty()) varDecl.docComment = variableName.docComment;
    varDecl.type=languageService.getType((string) dataType);
    if(!i6alias.empty()) varDecl.i6name = i6alias;
    varDecl.isExternal=isExternal;
    varDecl.isConst=isConst;
    varDecl.isRefLocal=isRef;
    // For func<...> types, getType returns the base "func" type. Set the full parameterized name.
    {
        string dtLower = (string)dataType;
        transform(dtLower.begin(), dtLower.end(), dtLower.begin(), ::tolower);
        if(dtLower.rfind("func<", 0) == 0) varDecl.type.name = dtLower;
    }

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;

    // `ref` is only valid on function-local declarations: it overrides the default
    // value-semantics dispatch for a single variable, opting into reference (pointer-
    // alias) semantics. Globals don't need it — file-scope class-typed variables are
    // already real I6 objects with operator= dispatch. Combining with `const`, `extern`,
    // or other qualifiers doesn't make sense.
    if(isRef){
        if(func == nullptr)
            parsingError("'ref' is only valid on local variable declarations");
        if(isConst)
            parsingError("'ref' cannot be combined with 'const'");
        if(isExternal)
            parsingError("'ref' cannot be combined with 'extern'");
    }

    // Disallow local variable names that shadow a global, a class member, or an object member;
    // also disallow duplicate declarations within the same scope.
    if(func != nullptr){
        // Duplicate in same scope
        if(body != nullptr)
            for(statement* s : body->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == varDecl.name)
                        parsingError(format("Variable '{0}' is already declared in this scope", varDecl.name));
        for(typeDef* g : languageService.globals)
            if(g->name == varDecl.name){
                if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                    const string& t = vd->type.name;
                    if(t == "grammartoken" || t == "attribute" || t == "property" || t == "verb") continue;
                }
                parsingWarning("Local variable '" + varDecl.name + "' shadows global of the same name; the global is unreachable from this scope.");
            }
        if(currentClass != nullptr){
            for(typeMember* m : currentClass->members)
                if(m->name == varDecl.name)
                    parsingWarning("Local variable '" + varDecl.name + "' shadows a member of class '" + currentClass->name + "'.");
            // Walk base class hierarchy for inherited members (vars and functions) — warning only
            function<void(classDef*)> checkBases = [&](classDef* c){
                for(typeMember* m : c->members)
                    if(m->name == varDecl.name)
                        if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                            parsingWarning("Local variable '" + varDecl.name + "' shadows inherited member '" + varDecl.name + "' from class '" + c->dName() + "'.");
                for(classDef* base : c->baseClasses) checkBases(base);
            };
            for(classDef* base : currentClass->baseClasses) checkBases(base);
        }
        if(currentObject != nullptr)
            for(typeMember* m : currentObject->members)
                if(m->name == varDecl.name)
                    parsingWarning("Local variable '" + varDecl.name + "' shadows a member of object '" + currentObject->name + "'.");
        // Inside a lambda: warn if local shadows a capturable outer variable (param or local
        // from the enclosing function). Without this warning, the outer variable becomes
        // silently unreachable — the lambda-local wins at Tier 1, preventing Tier 7 capture.
        if(lambdaOuterFunc != nullptr){
            for(paramDef* p : lambdaOuterFunc->params)
                if(p->name == varDecl.name){
                    parsingWarning("Local variable '" + varDecl.name + "' shadows capturable parameter '" + varDecl.name + "' from enclosing function.");
                    break;
                }
            statementBlock* rootBody = dynamic_cast<statementBlock*>(lambdaOuterFunc->body);
            if(rootBody != nullptr){
                function<bool(statementBlock*)> findInBlock = [&](statementBlock* blk) -> bool {
                    if(!blk) return false;
                    for(statement* s : blk->statements)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                            if(vd->name == varDecl.name) return true;
                    return false;
                };
                // Check immediate enclosing body and function root body
                bool found = false;
                if(lambdaOuterBody != nullptr) found = findInBlock(lambdaOuterBody);
                if(!found && rootBody != lambdaOuterBody) found = findInBlock(rootBody);
                if(found)
                    parsingWarning("Local variable '" + varDecl.name + "' shadows capturable local '" + varDecl.name + "' from enclosing function.");
            }
        }
    }

    bool isAuto = (dataType.value == "auto");
    if(isAuto && symbol.value != token::assignment)
        parsingError("'auto' requires an initializer to infer the type");

    if(symbol.value==token::assignment){
        token first = file.getToken();
        if(first.is(token::braceOpen)){
            // initializer list: { expr, expr, ... }
            initializerList* list = new initializerList();
            token t = file.getToken();
            while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                expression* elem = parseExpression(t, {",", token::braceClose}, func, body);
                list->elements.push_back(elem);
                if(elem->terminator == token::braceClose) break;
                t = file.getToken();
            }
            // consume the trailing semicolon
            file.getToken(token::endStatement);

            // type-check each element against the declared element type
            string dtStr = (string)dataType;
            string expectedElemType;
            // For array<T>, extract T as the expected element type
            if(dtStr.size() > 6 && dtStr.substr(0, 6) == "array<" && dtStr.back() == '>')
                expectedElemType = dtStr.substr(6, dtStr.size() - 7);
            // Fallback: infer from the class's single-param method
            if(expectedElemType.empty()){
                classDef* listClass = dynamic_cast<classDef*>(&languageService.getType(dtStr));
                if(listClass != nullptr)
                    for(typeMember* m : listClass->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            if(fd->params.size() == 1){ expectedElemType = fd->params[0]->type.name; break; }
            }
            for(size_t i = 0; i < list->elements.size(); i++){
                expression* elem = list->elements[i];
                if(elem->resolvedType.empty())
                    parsingError(format("Undeclared identifier in initializer list (element {0})", i));
                else if(!expectedElemType.empty() && !isTypeCompatible(elem->resolvedType, expectedElemType))
                    parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
            }

            varDecl.declaredExpressionValue = list;
        } else if(first.is("$") && file.peekToken(1).is(eTokenType::quote)){
            // Interpolated string literal initializer: string s = $"..."
            varDecl.interpSegments = parseInterpolatedSegments(func, body);
            file.getToken(token::endStatement);  // consume ';'
            expression* rhs = new expression();
            rhs->resolvedType = "interpolatedstringliteral";
            varDecl.declaredExpressionValue = rhs;
            // Emitter lookup for operator=(interpolatedstringliteral)
            classDef* classType=dynamic_cast<classDef*>(&languageService.getType((string)dataType));
            if(classType != nullptr){
                functionDef* assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name=="interpolatedstringliteral";
                }));
                if(assignOp && assignOp->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(assignOp->body)){
                        varDecl.initEmitterBody  = processBglConditionals(blk->i6Body);
                        varDecl.initEmitterParam = assignOp->params[0]->name;
                    }
                }
                if(!assignOp)
                    parsingError(format("Type '{0}' has no operator=(interpolatedStringLiteral) emitter", (string)dataType));
            }
        } else {
            // Set expected type for the RHS so name resolution can disambiguate (e.g. an
            // enum value of the declared type wins over a same-name value of an unrelated enum).
            // For `auto`, no expected type is known until we see the RHS — leave it empty.
            string savedExpected = currentExpectedType;
            if(!isAuto) currentExpectedType = (string)dataType;
            expression* rhs = parseExpression(first, {token::endStatement}, func, body);
            currentExpectedType = savedExpected;
            varDecl.declaredExpressionValue = rhs;

            // auto type inference: resolve type from RHS, checking operator auto() for type promotion
            if(isAuto && rhs != nullptr && !rhs->resolvedType.empty()){
                string inferredType = rhs->resolvedType;
                // Check if the RHS type has operator auto() — use its return type instead
                classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(inferredType));
                if(rhsCls){
                    for(typeMember* m : rhsCls->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            if(fd->name == "auto"){ inferredType = fd->returnType.name; break; }
                }
                varDecl.type = languageService.getType(inferredType);
                if(varDecl.type.name.empty()) varDecl.type.name = inferredType;
                dataType.value = inferredType;  // update for downstream type checks
            } else if(isAuto){
                parsingError("Cannot infer type from initializer expression");
            }

            //type check: if the declared type is a class, verify the assigned value is accepted by one of its operator= signatures.
            // `ref` locals opt out of all operator= dispatch — they're plain pointer-alias.
            classDef* classType=dynamic_cast<classDef*>(&languageService.getType((string)dataType));
            if(classType != nullptr && rhs != nullptr && !isRef){
                string valueTypeName = rhs->resolvedType;
                if(!valueTypeName.empty()){
                    // Two-pass: exact type match first, then var wildcard — so specific overloads always beat the catch-all.
                    // Always run findMemberInHierarchy so we can capture the emitter body if found.
                    functionDef* assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m);
                        return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name==valueTypeName;
                    }));
                    if(!assignOp) assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m);
                        return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name=="var";
                    }));
                    bool found = assignOp != nullptr || isTypeCompatible(valueTypeName, (string)dataType);
                    // If operator= is an emitter, capture its body so the emitter uses it instead of plain assignment
                    // Skip if RHS contains $target — the opcode handles its own store
                    if(assignOp && assignOp->isEmitter && rhs->text().find("$target") == string::npos){
                        if(auto* blk = dynamic_cast<i6Block*>(assignOp->body)){
                            varDecl.initEmitterBody  = processBglConditionals(blk->i6Body);
                            varDecl.initEmitterParam = assignOp->params[0]->name;
                            // Pre-substitute $class — the declared LHS type. Mirrors the
                            // assignment-statement path; $self/$target stay deferred to emit time.
                            varDecl.initEmitterBody = i6Emitter::replaceWord(varDecl.initEmitterBody, "$class", classType->i6Name());
                        }
                    }
                    // Non-emitter operator=: mirror the assignment-statement dispatch
                    // (bglParser.cpp resolveEmitter, non-emitter branch). Synthesize a
                    // one-line dispatch body that calls the mangled `_opeq` routine
                    // exactly once with the RHS as its argument. Without this, declarations
                    // like `Window w = factory();` fell through to plain pointer-assign
                    // even when the user had defined a copy operator on the class.
                    //
                    // Gated on `classHasStoredFields && !inheritsFromObject` to match the
                    // backing-synthesis predicate: tree-citizen classes (`: object`) use
                    // reference semantics for locals — there's no per-local backing object
                    // to dispatch operator= on, so the call would target `nothing` and
                    // silently fail at runtime. Plain classes with stored fields DO have
                    // synthesized backing, so dispatch lands correctly.
                    else if(assignOp && !assignOp->isEmitter && !assignOp->isPrePassStub
                              && classHasStoredFields(classType) && !inheritsFromObject(classType)){
                        if(assignOp->i6name.empty()) assignOp->i6name = mangleOperatorName(assignOp->name);
                        string paramName = assignOp->params[0]->name;
                        varDecl.initEmitterBody  = format("$target.{0}(${1});", assignOp->i6name, paramName);
                        varDecl.initEmitterParam = paramName;
                    }
                    // Mirror the assignment-statement check: TypeCompatible fallback with no
                    // operator= on a stored-field, non-tree-citizen class would emit silent
                    // pointer-assign and surprise the user expecting value-semantics.
                    if(found && assignOp == nullptr && classHasStoredFields(classType) && !inheritsFromObject(classType))
                        parsingError(format("Type '{0}' has no operator=. Declare 'operator =' on the class to define copy semantics for its fields, mark the local as 'ref' to opt into pointer-reference semantics, or inherit from 'object' for tree-citizen reference semantics.",
                            typeDisplayName((string)dataType)));
                    if(!found){
                        // Fallback: check if RHS type has emitter DeclaredType operator(){}
                        classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(valueTypeName));
                        if(rhsCls != nullptr)
                            if(typeMember* m = findMemberInHierarchy(rhsCls, [&](typeMember* m){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                return opFn && opFn->name=="operator()" && opFn->params.empty() && opFn->isEmitter && !opFn->isExplicit
                                       && opFn->returnType.name==(string)dataType && dynamic_cast<i6Block*>(opFn->body)!=nullptr;
                            })){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                auto* blk = dynamic_cast<i6Block*>(opFn->body);
                                string b = processBglConditionals(blk->i6Body);
                                string argText = rhs->text();
                                size_t pos = 0;
                                while((pos = b.find("$self", pos)) != string::npos){ b.replace(pos, 5, argText); pos += argText.size(); }
                                rhs->tokens.clear();
                                rhs->tokens.push_back(b);
                                rhs->resolvedType = (string)dataType;
                                found = true;
                            }
                    }
                    if(!found) parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", typeDisplayName(valueTypeName), typeDisplayName((string)dataType)));
                }
            }
        }
    }

    // Flush ternary-lowering injections before the variable declaration that uses _bgl_temp
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();

    // Class-typed function locals need backing storage for value-semantics assignment to
    // dispatch correctly. A bare int local slot can't be the target of `operator=` —
    // `localA._opeq(rhs)` would dispatch against the slot's int contents (initially 0 =
    // `nothing` in I6), not a real object. Synthesize a global I6 object instance per
    // (routine, local) and route source references to it via i6name. Gated on:
    //   (a) class has own stored fields (otherwise no state to back), AND
    //   (b) class does NOT inherit from `object` (tree citizens use reference semantics
    //       via inherited operator=(object); synthesizing backing for them would
    //       zero-init `parent`/`child`/`sibling`/`attributes` and yank objects out of
    //       the world tree), AND
    //   (c) the local isn't `ref`-qualified (the user opted into pointer-alias).
    // The error path (silent value-semantics-without-operator= gap) already fires upstream
    // for plain classes without operator= — this synthesis covers the happy path where
    // operator= exists and the user expects per-local independent state.
    if(func != nullptr && body != nullptr && !isExternal && !isConst && !isRef){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varDecl.type.name));
        if(cls && !cls->isEmitterClass && !cls->isAlias && !cls->isExternal
                 && classHasStoredFields(cls) && !inheritsFromObject(cls)){
            string backingName = "_bglLocal_" + func->name + "_" + varDecl.name;
            variableDeclaration* backing = new variableDeclaration();
            backing->name = backingName;
            backing->displayName = backingName;
            backing->type = varDecl.type;
            backing->src = varDecl.src;
            languageService.registerInstance(*backing);
            varDecl.i6name = backingName;
            varDecl.isClassLocalWithBacking = true;
        }
    }

    if(body != nullptr)
        body->statements.push_back(&varDecl);
    else
        languageService.registerInstance(varDecl);

    // init / deinit: if the variable's type is a class with init/deinit emitters, inject them
    // For local variables: inject init as an i6RawNode and register deinit in func->cleanups.
    // For global variables: record init in languageService.globalInits for the synthesised bglInit routine.
    if(!isConst && body != nullptr && func != nullptr){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varDecl.type.name));
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0) continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                string bodyText = processBglConditionals(blk->i6Body);
                // trim
                size_t s=bodyText.find_first_not_of(" \t\n\r"); if(s!=string::npos) bodyText=bodyText.substr(s);
                size_t e=bodyText.find_last_not_of(" \t\n\r");  if(e!=string::npos) bodyText=bodyText.substr(0,e+1);
                if(bodyText.empty()) continue;
                // substitute $self / $val with variable name
                string substituted = replaceWord(bodyText, "$self", varDecl.name);
                substituted = replaceWord(substituted, "$val",  varDecl.name);
                if(fn->name == "init"){
                    i6RawNode& initNode = *(new i6RawNode());
                    initNode.text = substituted;
                    // insert before varDecl (which is currently the last element)
                    body->statements.insert(body->statements.end()-1, &initNode);
                } else if(fn->name == "deinit"){
                    func->cleanups.push_back({varDecl.name, substituted});
                }
            }
        }
    }

    // Global scope: record init body in globalInits for bglInit
    if(!isConst && body == nullptr && func == nullptr){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varDecl.type.name));
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0 || fn->name != "init") continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                string bodyText = processBglConditionals(blk->i6Body);
                size_t s=bodyText.find_first_not_of(" \t\n\r"); if(s!=string::npos) bodyText=bodyText.substr(s);
                size_t e=bodyText.find_last_not_of(" \t\n\r");  if(e!=string::npos) bodyText=bodyText.substr(0,e+1);
                if(bodyText.empty()) continue;
                string subbed = replaceWord(bodyText, "$self", varDecl.name);
                subbed = replaceWord(subbed, "$val", varDecl.name);
                languageService.globalInits.push_back({varDecl.name, subbed});
            }
        }
    }

    return false;
}

bool bglParser::processRoutineDeclaration(token returnType, token name, abstractObject& contextObject, bool isExternal, bool isEmitter, bool isReplace, bool isDefault){
    functionDef& funcDef=*(new functionDef());
    funcDef.name=(string) name; funcDef.displayName=name.originalValue;
    funcDef.isDefault=isDefault;
    // Use the name token's source location (preserved from when it was read) — currentLocation()
    // would return where we are NOW (after consuming the name), which may be on a later line.
    funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
    funcDef.returnType=languageService.getType((string) returnType);
    funcDef.isExternal=isExternal;
    funcDef.isEmitter=isEmitter;
    if(!returnType.docComment.empty())   funcDef.docComment = returnType.docComment;
    else if(!name.docComment.empty())    funcDef.docComment = name.docComment;
    // Non-emitter operator routines (name starts with non-identifier char) need a mangled
    // i6name so the emitted I6 routine has a valid property identifier. Call sites use the
    // same mangler to look it up. Emitters are inlined and don't need the mangled name.
    if(!isEmitter && !funcDef.name.empty() && !isalpha((unsigned char)funcDef.name[0]) && funcDef.name[0] != '_')
        funcDef.i6name = mangleOperatorName(funcDef.name);

    // Register into globals EARLY (replacing any pre-scan stub) so that LSP error recovery
    // preserves partial parse state even if the body parse throws. The full body and params
    // are populated via references into this same object below.
    //
    // In .inf-mode, Pass 1 walks every #bgl island before Pass 2 starts, so all function
    // stubs cluster at the front of `globals` regardless of where their islands sit in the
    // .inf body. Replacing the stub in place would leave the funcDef there too — emitting
    // it ahead of compositeNodes for raw I6 (and `#includeI6` directives) that Pass 2
    // push_backs later. WadeWar3 hit this: `[initialise; ... location = ...]` was emitted
    // before `#include "...#parser.h"` (which declares `location`), and I6 rejected the
    // assignment. So in .inf-mode we erase the stub and push_back the real funcDef,
    // landing it at the current Pass-2 position. Other stubs remain visible for
    // forward-reference resolution during this function's body parse.
    //
    // Redefinition guard: a plain (non-extern) definition whose name is already declared `extern`
    // is only allowed when that extern was marked `default` — a library stub explicitly meant to be
    // overridden. `extern` otherwise asserts "this exists in I6", so silently redefining it is a
    // mistake (a name collision, or a missing `default`). A `default` extern is dropped here so the
    // real definition takes over cleanly. `replace` has its own path (below) and is exempt.
    if(!isExternal && !isReplace){
        for(auto it = languageService.globals.begin(); it != languageService.globals.end(); ++it){
            auto* ex = dynamic_cast<functionDef*>(*it);
            if(ex && ex->isExternal && !ex->isPrePassStub && ex->name == funcDef.name){
                if(!ex->isDefault)
                    parsingError(format("'{0}' is already declared extern (it exists externally); a plain "
                        "definition would silently redefine it. If it's an overridable library stub, mark the "
                        "declaration `extern default`; otherwise rename to avoid the collision.", funcDef.name));
                languageService.globals.erase(it);
                break;
            }
        }
    }

    // In .bgl-mode, Pass 1 walks the source in order, so prescan stubs are already at
    // their source-order positions — claim in place preserves source order naturally.
    bool registeredEarly = false;
    if(languageService.isInfMode){
        for(auto it = languageService.globals.begin(); it != languageService.globals.end(); ++it){
            if(auto* stub = dynamic_cast<functionDef*>(*it)){
                if(stub->name == funcDef.name && stub->isPrePassStub){
                    languageService.globals.erase(it);
                    break;
                }
            }
        }
        languageService.globals.push_back(&funcDef);
        registeredEarly = true;
    } else {
        for(typeDef*& g : languageService.globals){
            if(auto* stub = dynamic_cast<functionDef*>(g)){
                if(stub->name == funcDef.name && stub->isPrePassStub){
                    g = &funcDef;
                    registeredEarly = true;
                    break;
                }
            }
        }
        if(!registeredEarly){
            languageService.globals.push_back(&funcDef);
            registeredEarly = true;
        }
    }

    processParameterList(funcDef);

    // Synthesize per-(function, param) backing globals for any byVal-class params,
    // BEFORE body parsing so identifier resolution inside the body sees param.i6name
    // (the backing's name) and routes reads/writes through the copy.
    synthesizeParamBackings(funcDef);

    // ── replace chaining (non-emitter): rename existing before body parse ──
    // For non-emitter replace, we rename the existing function to a mangled name
    // BEFORE parsing the body so that replaced() calls can resolve during parsing.
    // Emitter replace keeps the simpler body-swap path (no chaining).
    if(isReplace && !isEmitter){
        functionDef* existing = nullptr;
        for(typeDef* g : languageService.globals){
            if(auto* fd = dynamic_cast<functionDef*>(g)){
                if(fd->name != funcDef.name) continue;
                if(fd->isPrePassStub) continue; // pre-scan stub doesn't count as a real definition
                existing = fd;
                break;
            }
        }
        if(existing){
            // Determine N for mangled name: count existing _bgl_replaced_NAME_* in globals
            int n = 0;
            string prefix = "_bgl_replaced_" + funcDef.name + "_";
            for(typeDef* g : languageService.globals)
                if(auto* fd = dynamic_cast<functionDef*>(g))
                    if(fd->name.rfind(prefix, 0) == 0) n++;
            string mangledName = prefix + to_string(n);
            existing->name = mangledName;
            funcDef.replacedTarget = mangledName;
            funcDef.replacedFunc = existing;
        } else {
            parsingWarning(format("replace: no existing global function '{0}' found; treating as new definition", funcDef.name));
        }
    }

    if(isEmitter && file.peekToken().is(token::endStatement)){
        // Semicolon-terminated emitter: pass-through (value unchanged)
        file.getToken(); // consume ';'
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body=" $self";
        funcDef.body=&rawblock;
    } else if(isEmitter){
        file.getToken(token::braceOpen);
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
        funcDef.body=&rawblock;
    } else if(isExternal){
        // extern non-emitter routine: declared bodyless with ';'. The implementation lives in I6
        // (a library routine) or is supplied by a Beguile `default` override — so the extern itself
        // emits nothing. A body would misleadingly look like an (empty) definition; reject it.
        if(file.peekToken().is(token::braceOpen))
            parsingError(format("extern function '{0}' cannot have a body — declare it with ';'. "
                                "Its implementation comes from I6, or from a `default` override.", funcDef.name));
        file.getToken(token::endStatement);
        funcDef.body = new statementBlock();
    } else {
        file.getToken(token::braceOpen);
        funcDef.body=new statementBlock();
        functionDef* savedFunc = currentFunc;
        currentFunc = &funcDef;
        openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
        while(processNextStatement(funcDef)==false){
        }
        closeCompileContext(eCompileContext::codeBlock);
        currentFunc = savedFunc;
        if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
            parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
    }

    // ── replace: emitter body-swap (existing behavior) ──
    if(isReplace && isEmitter){
        for(typeDef* g : languageService.globals){
            if(auto* existing = dynamic_cast<functionDef*>(g)){
                if(existing->name != funcDef.name) continue;
                if(existing->params.size() != funcDef.params.size()) continue;
                bool paramsMatch = true;
                for(size_t i = 0; i < funcDef.params.size() && paramsMatch; i++)
                    if(funcDef.params[i]->type.name != existing->params[i]->type.name)
                        paramsMatch = false;
                if(!paramsMatch) continue;
                existing->body = funcDef.body;
                existing->isEmitter = funcDef.isEmitter;
                existing->returnType = funcDef.returnType;
                return false;
            }
        }
        parsingWarning(format("replace: no existing emitter '{0}' found with matching signature; treating as new definition", funcDef.name));
    }

    // ── replace chaining: dead code elimination ──
    // If replaced() was never called in the body, the predecessor (and its entire
    // backward chain) will never execute — mark them dead so the emitter skips them.
    if(isReplace && !isEmitter && funcDef.replacedFunc && !funcDef.replacedWasCalled){
        functionDef* f = funcDef.replacedFunc;
        while(f){
            f->isReplacedDead = true;
            f = f->replacedFunc;
        }
    }

    // funcDef was already registered in globals above (before body parse) so LSP error
    // recovery preserves the partial definition. Nothing more to do here.
    return false;
}


// ===============================================================================
// Grammar declarations
// ===============================================================================
bool bglParser::processGrammarDeclaration(token nameOverride){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("'grammar' declarations are only allowed in global context");

    token name = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken(eTokenType::identifier);
    string grammarName = name.originalValue.empty() ? name.value : name.originalValue;

    file.getToken(token::braceOpen);
    if(file.peekToken().is(token::braceOpen))
        parsingError(format("'grammar {0} {{ ... }}' bodies hold grammarRule members, not bare grammar lines. To add lines to an existing verb use 'extend {0} {{ grammar += {{ ... }}; }}'; to author a cross-cutting grammar object use 'grammar {0} {{ grammarRule r = {{Verb, {{ ... }}}}; }}'.", grammarName));
    return processGrammarObjectDeclaration(grammarName);
}

// Parse a grammar object body with grammarRule members.
// Assumes outer '{' already consumed. grammarName is the object name.
// Members are: grammarRule name = {VerbRef, {.word, TOKEN, ...}};
// or array<grammarRule> name = {{VerbRef, {.word, ...}}, ...};
// Type can be inferred from the grammarTable class members.

bool bglParser::processGrammarObjectDeclaration(const string& grammarName){
    // Resolve grammarTable class for type inference
    classDef* gtClass = dynamic_cast<classDef*>(&languageService.getType("grammarrulelist"));

    // Create a grammarRuleListDecl to hold all the rules
    grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
    gtd.name = grammarName;
    gtd.type = languageService.getType("grammarrulelist");

    // Helper: resolve a verb reference token, return original-case verb name or error
    auto resolveVerbRef = [this](const token& verbTok) -> string {
        string lower = verbTok.value;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == lower)
                return verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
        return "";
    };

    // Helper: parse a single grammarRule initializer {VerbRef, {.word, TOKEN, ...}, priority?}
    // Assumes outer '{' already consumed. Optional 3rd positional element is the line's sort
    // priority (default 10 from BLR `class verb`).
    auto parseGrammarRuleInit = [&](grammarRuleDecl& rd){
        // First element: verb reference
        token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        rd.targetVerb = resolveVerbRef(verbTok);
        file.getToken(token::comma);
        // Second element: grammar line pattern {.word, TOKEN, ...}
        file.getToken(token::braceOpen);
        rd.line = parseGrammarLineContent();
        // Optional 3rd element: priority
        rd.line.priority   = parseOptionalGrammarRulePriority(languageService.getClassFieldIntDefault("verb", "priority", 10));
        rd.line.isOwnLine  = false;
        // Closing } of the outer initializer
        file.getToken(token::braceClose);
    };

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside settings/object body — missing closing '}'");
        // Determine type — explicit or inferred
        string memberType;
        bool isArray = false;
        if(tok.is("array")){
            // explicit array<grammarRule>
            file.getToken("<");
            string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
            file.getToken(">");
            memberType = "grammarrule";
            isArray = true;
            tok = file.getToken(eTokenType::identifier); // member name
        } else if(tok.isDataType() && tok.value == "grammarrule"){
            // explicit grammarRule
            memberType = "grammarrule";
            tok = file.getToken(eTokenType::identifier); // member name
        } else if(tok.is(eTokenType::identifier)){
            // inferred type from grammarTable class
            // tok is the member name; infer type from class
            if(gtClass){
                // Check if assignment follows — peek for = or {
                token afterName = file.peekToken();
                if(afterName.is(token::assignment)){
                    // Look at initializer shape to determine type
                    // For now, we'll determine after reading the initializer
                    memberType = "grammarrule"; // default inference
                }
            }
            if(memberType.empty())
                parsingError(format("Cannot infer type for '{0}' in grammar object body", tok.value));
        } else {
            parsingError(format("Unexpected '{0}' in grammar object body", tok.value));
        }

        string memberName = tok.value;
        file.getToken(token::assignment);

        token first = file.getToken(token::braceOpen);

        // Validate shape matches declared type
        token peek2 = file.peekToken();
        if(isArray){
            // array<grammarRule>: must be {{VerbRef, {pattern}}, ...}
            if(!peek2.is(token::braceOpen))
                parsingError(format("array<grammarRule> '{0}' requires nested initializer list of rule pairs", memberName));
            token inner = file.getToken();
            while(inner.is(token::braceOpen)){
                grammarRuleDecl& rd = *(new grammarRuleDecl());
                rd.name = memberName;
                rd.type = languageService.getType("grammarrule");
                parseGrammarRuleInit(rd);
                gtd.rules.push_back(&rd);
                grammarLine gl = rd.line;
                gl.targetVerb = rd.targetVerb;
                gtd.grammarLines.push_back(gl);
                token sep = file.getToken({token::comma, token::braceClose});
                if(sep.is(token::braceClose)) break;
                inner = file.getToken();
            }
        } else {
            // grammarRule: must be {VerbRef, {pattern}} — not nested
            if(peek2.is(token::braceOpen))
                parsingError(format("grammarRule '{0}' takes a single rule initializer; use array<grammarRule> for multiple rules", memberName));
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = memberName;
            rd.type = languageService.getType("grammarrule");
            parseGrammarRuleInit(rd);
            gtd.rules.push_back(&rd);
            grammarLine gl = rd.line;
            gl.targetVerb = rd.targetVerb;
            gtd.grammarLines.push_back(gl);
        }

        // Consume optional semicolon after member declaration
        if(file.peekToken().is(token::endStatement)) file.getToken();

        tok = file.getToken();
    }

    languageService.globals.push_back(&gtd);
    return false;
}

// After parseGrammarLineContent has consumed the inner `}` of a `{Verb, {pattern}}` literal,
// peek for an optional third positional element — `, priority` — and return it. Returns
// `defaultPriority` (caller-supplied; typically BLR's `class verb` default of 10) when no
// third element is present.

int bglParser::parseOptionalGrammarRulePriority(int defaultPriority){
    if(!file.peekToken().is(token::comma)) return defaultPriority;
    file.getToken();  // consume the comma
    token prioTok = file.getToken();
    try { return stoi(prioTok.value); }
    catch(...) {
        parsingError(format("grammarRule priority: expected integer literal, got '{0}'",
            prioTok.originalValue.empty() ? prioTok.value : prioTok.originalValue));
    }
    return defaultPriority;
}

// Parse a single grammar line's content: trigger word + pattern tokens.
// Assumes the opening '{' has already been consumed.
// Returns a grammarLine with verbWord and I6-ready patternTokens.

grammarLine bglParser::parseGrammarLineContent(){
    auto escDictWord = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch; return e;
    };
    auto makeI6Word = [&escDictWord](const token& t) -> string {
        string e = escDictWord(t.value);
        if(t.isPlural) return "'" + e + "/p'";
        if(e.size() == 1) return "'" + e + "//'";
        return "'" + e + "'";
    };

    grammarLine line;

    // First token: the verb trigger word — a dict word, with optional |-alternation for
    // multi-trigger lines (e.g. `.type|.enter|.put`). Extras land in additionalVerbWords;
    // they emit as one combined `verb 'w1' 'w2' 'w3' ...` directive when all are first-
    // occurrence, or fan out to per-trigger directives otherwise (see emitGrammarLines).
    token trigger = file.getToken(eTokenType::dictionaryWord);
    line.verbWord = trigger.value;   // raw word, e.g. "put"
    while(file.peekToken().is("|")) {
        file.getToken();  // consume '|'
        token alt = file.getToken(eTokenType::dictionaryWord);
        line.additionalVerbWords.push_back(alt.value);
    }

    // Remaining pattern tokens, comma-separated until }
    token tok = file.getToken({token::comma, token::braceClose});
    while(tok.is(token::comma)){
        // Optional opening paren — syntactic sugar for a dict-word alternatives group
        bool hasParens = file.peekToken().is("(");
        if(hasParens) file.getToken();  // consume '('

        token pt = file.getToken({eTokenType::dictionaryWord, eTokenType::identifier, eTokenType::dataType});

        if(pt.is(eTokenType::dictionaryWord)){
            // Dict word — may be chained with | alternatives: .word1 | .word2 | .word3
            string i6tok = makeI6Word(pt);
            while(file.peekToken().is("|")){
                file.getToken();  // consume '|'
                token alt = file.getToken(eTokenType::dictionaryWord);
                i6tok += "/" + makeI6Word(alt);
            }
            if(hasParens) file.getToken(")");  // consume ')'
            line.patternTokens.push_back(i6tok);
        } else {
            if(hasParens)
                parsingError("Parenthesized grammar group must contain dictionary word alternatives (.word | .word2 | ...)");

            // identifier or dataType in grammar pattern
            string tokenStr = pt.value;
            string display = pt.originalValue.empty() ? pt.value : pt.originalValue;

            // Resolve declared type. Grammar-line context: prefer enum-value matches
            // (e.g. `extern enum grammarToken { noun, held, ... }`) over global decls,
            // because some grammar-token names (`noun`) shadow extern globals of the same
            // name (the I6 runtime `noun` object). In a grammar line position the token
            // meaning always wins; outside grammar lines the global wins via normal
            // ambiguity resolution.
            string resolvedType;
            for(typeDef* t : languageService.objectTypes){
                auto* ed = dynamic_cast<enumDef*>(t);
                if(!ed) continue;
                bool found = false;
                for(enumValueDef* v : ed->namedValues)
                    if(v->name == tokenStr){
                        resolvedType = ed->name;
                        tokenStr = ed->isExternal ? v->name : to_string(v->value);
                        found = true; break;
                    }
                if(found) break;
            }
            if(resolvedType.empty()){
                for(typeDef* g : languageService.globals){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                        if(vd->name == tokenStr){
                            resolvedType = vd->type.name;
                            if(!vd->i6name.empty()) tokenStr = vd->i6name;
                            break;
                        }
                    if(auto* fd = dynamic_cast<functionDef*>(g))
                        if(fd->name == tokenStr){ resolvedType = "function"; break; }
                }
            }

            // `reverse` pseudo-token — action-target modifier, not a pattern token.
            // Must be the LAST element in the line literal (next token must be `}`).
            // Sets line.isReverse and skips push_back so it doesn't end up in the I6 pattern.
            if(resolvedType == "grammartoken" && tokenStr == "reverse"){
                token next = file.peekToken();
                if(next.isNot(token::braceClose))
                    parsingError("'reverse' must be the last element in a grammar line literal — no tokens may follow it");
                line.isReverse = true;
                tok = file.getToken({token::comma, token::braceClose});  // consume `}`
                break;
            }

            if(file.peekToken().is("(")){
                // NOUN(Routine) / SCOPE(Routine) — parameterized grammar token
                if(resolvedType != "grammartoken")
                    parsingError(format("'{0}' in grammar pattern: only grammarToken types can be parameterized with (Routine)", display));
                file.getToken();  // consume '('
                token routine = file.getToken({eTokenType::identifier, eTokenType::dataType});
                file.getToken(")");  // consume ')'
                functionDef* rfd = nullptr;
                for(typeDef* g : languageService.globals)
                    if(auto* fd = dynamic_cast<functionDef*>(g))
                        if(fd->name == routine.value){ rfd = fd; break; }
                if(!rfd)
                    parsingError(format("'{0}' in '{1}({0})' does not name a declared global function", routine.value, display));
                if(rfd->returnType.name != "bool" && rfd->returnType.name != "ebool")
                    parsingError(format("'{0}' in '{1}({0})' must return bool (returns '{2}')", routine.value, display, rfd->returnType.name));
                tokenStr = pt.value + "=" + routine.value;  // I6 form: noun=Routine
            } else {
                // bare token: must be a declared grammarToken, attribute, or global function
                if(resolvedType != "grammartoken" && resolvedType != "attribute" && resolvedType != "function")
                    parsingError(format("'{0}' in grammar pattern is not a declared grammarToken, attribute, or global function", display));
            }
            line.patternTokens.push_back(tokenStr);
        }
        tok = file.getToken({token::comma, token::braceClose});
    }
    // tok is now } (closing brace of this line)
    return line;
}

// Parse a grammar line list: { {.put, held, .on, noun}, {.hang, held, .on, noun} }
// First token of each line is the verb trigger word (must be a dictionaryWord).
// Remaining tokens are pattern tokens: dict words become I6 'word', identifiers stay as-is.
//
// Single-line shorthand ("pretty lie"): when the first content token after the outer `{`
// is a dictionaryWord, the entire outer braces are treated as the one and only grammar
// line — `grammar = {.give, held, .to, creature}` desugars to
// `grammar = { {.give, held, .to, creature} }`. Mirrors the existing extern-verb body
// shorthand (parseObjectBody) so the same shape works at any grammar-list site.

vector<grammarLine> bglParser::parseGrammarLines(){
    vector<grammarLine> result;

    file.getToken(token::braceOpen);   // outer {

    // Single-line shorthand: outer `{` directly followed by a dictionaryWord means the
    // outer braces ARE the line literal. parseGrammarLineContent reads from the current
    // position (just past `{`) and consumes through the matching `}` — which is the
    // outer close here.
    if(file.peekToken().is(eTokenType::dictionaryWord)){
        result.push_back(parseGrammarLineContent());
        return result;
    }

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside grammar — missing closing '}'");
        tok.assert(token::braceOpen, "Expected '{' to start a grammar line");
        result.push_back(parseGrammarLineContent());

        // After }, expect , (more lines) or } (end of list)
        tok = file.getToken({token::comma, token::braceClose});
        if(tok.is(token::comma))
            tok = file.getToken();   // either { for next line, or } for end
    }
    return result;
}
