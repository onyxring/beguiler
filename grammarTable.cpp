// grammarTable.cpp — Grammar rule table for the V2 pattern-matching parser dispatcher.
// Each rule is: { name, pattern, handler }
// Table order determines priority — first match wins when multiple rules complete.
//
// Pattern token types:
//   TYPE_NAME  — a registered class/enum/base type (lexer: dataType)
//   NEW_NAME   — an identifier (declaration names and references to non-type symbols)

#include "bglParser.h"
#include "bglLanguageService.h"

using P    = PatternElement;
using Self = bglParser;

void bglParser::initGrammarTable() {
    // Shadow the global TYPE_NAME with a semantic predicate that also accepts namespace type paths
    PatternElement TYPE_NAME = P::semantic(
        [this](token& t){ return t.is(eTokenType::dataType) || isNamespacedTypePath(t); },
        "type name");
    grammarRules = {
        //  name                         pattern                                                        handler
        {"enum declaration",         {P::anyOf({"enum","bnum"}), TYPE_NAME},                        &Self::processEnum},
        {"class declaration",        {"class", TYPE_NAME},                                          &Self::processClass},
        {"grammar declaration",      {"grammar", NEW_NAME},                                         &Self::processGrammar},
        {"array declaration",        {"array", "<"},                                                &Self::processArray},
        {"func type declaration",    {"func", "<"},                                                 &Self::processFunc},
        {"typed object declaration", {TYPE_NAME, NEW_NAME, ":", TYPE_NAME},                         &Self::processTypedObject},
        {"aliased declaration",      {TYPE_NAME, NEW_NAME, "as", NEW_NAME},                         &Self::processAliased},
        {"routine declaration",      {TYPE_NAME, NEW_NAME, "("},                                    &Self::processRoutine},
        {"object declaration",       {TYPE_NAME, NEW_NAME, "{"},                                    &Self::processObject},
        {"variable declaration",     {TYPE_NAME, NEW_NAME, "="},                                    &Self::processVariable},
        {"variable declaration",     {TYPE_NAME, NEW_NAME, ";"},                                    &Self::processVariable},

        // Statements (code-block scope) ==========================================
        {"break statement",          {"break", ";"},                                                &Self::processBreak},
        {"continue statement",       {"continue", ";"},                                             &Self::processContinue},
        {"rtrue statement",          {"rtrue", ";"},                                                &Self::processRtrue},
        {"rfalse statement",         {"rfalse", ";"},                                               &Self::processRfalse},
        {"rtrue with message",       {"rtrue", "("},                                                &Self::processRtrueWithMessage},
        {"rfalse with message",      {"rfalse", "("},                                               &Self::processRfalseWithMessage},
        {"return statement (void)",  {"return", ";"},                                               &Self::processReturnVoid},
        {"return statement (expr)",  {"return"},                                                    &Self::processReturnExpr},
        {"if statement",             {"if", "("},                                                   &Self::processIf},
        {"while statement",          {"while", "("},                                                &Self::processWhile},
        {"do statement",             {"do", "{"},                                                   &Self::processDo},
        {"for statement",            {"for", "("},                                                  &Self::processFor},
        {"switch statement",         {"switch", "("},                                               &Self::processSwitch},
        {"try statement",            {"try", "{"},                                                  &Self::processTry},
        {"throw statement",          {"throw"},                                                     &Self::processThrow},
        {"delete statement",         {"delete"},                                                    &Self::processDelete},

        // Directives (preprocessor) =============================================
        // All directives dispatch through processDirective via a single handler.
        {"#include directive",       {"#include"},                                                  &Self::processDirectiveDispatch},
        {"#once directive",          {"#once"},                                                     &Self::processDirectiveDispatch},
        {"#startup directive",       {"#startup"},                                                  &Self::processDirectiveDispatch},
        {"#using directive",         {"#using"},                                                    &Self::processDirectiveDispatch},
        {"#emitfirst directive",     {"#emitfirst"},                                                &Self::processDirectiveDispatch},
        {"#emitlast directive",      {"#emitlast"},                                                 &Self::processDirectiveDispatch},
        {"#includei6 directive",     {"#includei6"},                                                &Self::processDirectiveDispatch},
        {"#i6 directive",            {"#i6"},                                                       &Self::processDirectiveDispatch},
        {"#define directive",        {"#define"},                                                   &Self::processDirectiveDispatch},
        {"#undef directive",         {"#undef"},                                                    &Self::processDirectiveDispatch},
        {"#if directive",            {"#if"},                                                       &Self::processDirectiveDispatch},
        {"#elif directive",          {"#elif"},                                                     &Self::processDirectiveDispatch},
        {"#else directive",          {"#else"},                                                     &Self::processDirectiveDispatch},
        {"#endif directive",         {"#endif"},                                                    &Self::processDirectiveDispatch},
        {"##ifdef directive",        {"##ifdef"},                                                   &Self::processDirectiveDispatch},
        {"##ifndef directive",       {"##ifndef"},                                                  &Self::processDirectiveDispatch},
        {"##else directive",         {"##else"},                                                    &Self::processDirectiveDispatch},
        {"##endif directive",        {"##endif"},                                                   &Self::processDirectiveDispatch},
        {"#beguilerSettings",        {"#beguilersettings"},                                         &Self::processDirectiveDispatch},
        {"#message directive",       {"#message"},                                                  &Self::processDirectiveDispatch},
        {"#error directive",         {"#error"},                                                    &Self::processDirectiveDispatch},
        {"#warning directive",       {"#warning"},                                                  &Self::processDirectiveDispatch},
        {"#exit directive",          {"#exit"},                                                     &Self::processDirectiveDispatch},
    };
    grammarInitialized = true;
}
