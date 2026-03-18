# claude.md

## Beguiler is the compiler for the Beguile language. 

It is written in CPP.   

It translates Beguile code (.bgl) into Inform 6 code (.inf), then passes the result to the I6 compiler to generate the final output (v3, v5, v8, .ulx). 

On a successful compile it will eventually do other things with the output from I6, so it waits for I6 to complete, rather than handing off produced I6 and ending. 

There is a language spec which should always be updated to reflect relevant changes to the language. 

Beguile has a language library (folder: beguiLib) which accompanies Beguiler and contains Beguile code.  Core features of the Beguile language, including opt-in features, are contained within it. 

## Important design principals of Beguiler: 

"Emitters", a code templating concept, is a core feature of the language.  Whenever it can reasonably be done, I6 code should come from emitters, reducing how much I6 specific knowledge the binary requires.   

The language is syntactically similar to C#, C++, and Typescript; it should follow patterns established in these languages. 

Some language elements, unique to I6,  have been folded into Beguile; however, most of the syntactical idiosyncrasies have been replaced with object-oriented constructs. 

The language should be syntactically consistent.  Avoid the introduction of new language features which don't align with established syntax patterns. 

The compiler reads the file in two-passes: a light weight parse to register all declared types, so they can be instantiated before they are declared; and the second parse loop which does the majority of the work. 

## Application architecture: 

The beguiler source code is separated into different logical components.  Some of the most important are: 

* Lexer – which reads files in sequence and produces tokens 

* Tokens – represents groups of read in characters, with a recognized type, that the parser can make sense of 

* Parser – reads the tokens the lexer provides and interprets them according to the Beguile syntax and builds a parse tree which will be emitted. 

* Language Service – a component to assist the parser in looking up language-specific information, such as declared types and keywords. 

* Emitter – a module which translates the parse tree into actual I6 code. 

## Final target-state for Beguiler includes: 

* A completed, fully functional Beguiler compiler including the completed begiLib folder. 

* A full suite of regression tests, validating changes to the compiler  

* A port of functionality of the python "blorbifier" into Beguiler's CPP. 

