# Include

#insert "file";
Pulls in the contents of file and compiles it.

#insertI6 "file";
Pulls in the contents of file, wrapped in an #i6{} constuct.

##include "file";
is short hand for #i6{#include "file"}

There is no valid directive #include

# base data types

int 
string
object
object derrived subclasses

var - still strongly typed, but typed inferred by the compiler

# classes

class myClass{
    int count;
}

the above is the same as...

class myClass : object{
    int count;
}

//NOT allowed... or is it?
class myNum : int{

}

extend myClass{
    int number;
}

equates to ...

class myClass{
    int count;
    int number;
}

class string{
    string operator + (string s){
        return this.append(s);
    }
    string operator = (string s){
        return this.set(s);
    }
    bool operator == (string s){
        return this.equals(s);
    }
    bool operator ?= (string s){
        return this.equals(s,true);
    }
}


Arithmetic Operators: +, -, *, /, %

Relational Operators: ==, !=, <, >, <=, >=, ?=

Assignment Operators: =, +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=
Bitwise Operators: &, |, ^, <<, >>
Unary Operators: !, ++ (increment), -- (decrement)

Subscript Operator: []
