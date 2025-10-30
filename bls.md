# Beguile Language Specification 1.0
2025.10.29
Jim Fisher  (OnyxRing)

## Introduction
Beguile is a language for writing Interactive Fiction for the Z-Machine and Glulx  platforms.  It is strongly typed, object-oriented  and borrows inspiration from C++, C#, Typescript, and a handful of other languages.  

The compiler for the Beguile language is named "Beguiler".  Like I7, it is a transpiler which generates I6 intermediate code and leverages the Inform 6 compiler before outputting the final .blorb result.  Because of this, the fingerprints of Inform 6 can be seen throughout its syntax.

This is the specification for that language.

## Comments
Beguile allows "comments", text which is ignored by the compiler, in two ways:

* **//** - indicating the rest of the line is a comment, but the start of the next line is not (unless it too begins with //)
* **/*** and ***/** - indicating everything in between these two patterns is ignored.  This can be used to comment out multiple lines, or just portions of a single line.

Here's an example containing both types of comments:

````
/* This routine prints the full name
from two passed in parameters */ 
void getFormalName(string f /*first name*/, string l /*last name*/){ 
    print f+" "+l; //TODO: add a return value later
}
````

From the compiler's perspective, the above is seen as this:

````
void getFormalName(string n, string p){
    print f+" "+l;
}
````

## Core data types
The following data types are considered core to the language:


> **int** - short for integer, an int can represent any whole number within the range of -32,768 and 32,767 on the Z-machine, and -2,147,483,648 and -2,147,483,647 on Glulx.  
>
>**string** - a malleable string object used to contain and work with text values.
>
>**object** - The object type is the foundation from which all other data types inherit. That is, integers are a special type of object, as are strings. 
         
Objects contain "member" variables and functions. The base object type implements the following members:

>**print()** - a routine used to print the value of an object.  
**toString()** - a routine used to convert the value into a string data type.
    
Since integers and string are objects, the following are perfectly legal (although strange):

````
8.print();
"hello".print();
````   
New objects are defined using the "classes" syntax (covered later in this document).

## Variables
Variables are names which are associated with data type values. The syntax for ***declaring*** a variable is:

***datatype identifier [= value];***

*The optional elements of the above syntax are denoted in brackets.*

The following are legal examples of declaring a variable:
> int b; // defaults to 0
> string name; //defaults to empty string ("")

As are:

> int b=9;
> string name = "Caesar";

However, since Beguile is a type-safe language, the following statements are *not* legal and will raise compile time errors:

> int b = "Augustus"; //error! expected int value
> string name = 99; //error! expected string value

This is because we are attempting to ***assign*** a value to a variable which does not match its type. This rule holds true for assignments *after* a variable has been declared as well:

> int b=9;
> b=33; //legal
> b="33"; //error!

As mentioned above, all other data types inherit from object (that is, are specialized versions of object). Because of this, a variable of type object can contain any value regardless of type.
 
````
    object retval="tree";
    retval=9;
````

## Routines 
Routines are named blocks of code which can be called from  other code.  They may return a single value, and may take zero or more values as parameters.  The syntax is as follows *(the section followed by **...** can be repeated)*:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;*returnDataType identifier([parameter [,parameter]...]){
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;//lines of code go here
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;}*

Where *parameter* takes the form of:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;*dataType identifier [=defaultValue]*

The above nested syntax definition, while precise, can be a hard to follow.  Here's an example of it all put together: a routine taking two parameters and returning a single result:

    int printAndReturnCalc(int tenDigit, int oneDigit=3){
        int retval; 
    
        retval = (tenDigit * 10)+oneDigit;
        retval.print();
        return retval;
    }

***Optional Parameters***
Parameters for which we specify a default value are called optional parameters.  It is not required to define a default value for a parameter; however, once one *is* defined, *all subsequent parameters* for the routine ***must*** also be optional. The following is, therefore, *not* legal:

````
int func(int a=9, int b){ //illegal
    ...
    return a;
}
````
but this *is* legal...
````
int func(int a, int b=8){ //legal
    ...
    return a;
}
````
***Return values*** The final return statement returns a value to the code which "called" this routine.  If a routine does not return a value, then the data type should be declared as ````void````.  If so, ````return```` cannot specify a return value and can be skipped entirely when it would otherwise appear as the last statement in the routine:

````
void printCalc(int tenDigit, int oneDigit=3){
    ((tenDigit * 10)+oneDigit).print();
}
````

***Calling Routines*** 
Calling a routine is done using the following syntax: 

````
name([paramValue[, paramValue]...]);
````

If the routine returns a value, it can be assigned to a variable of the appropriate type. 

Here are a couple examples of calling a routine:

````
int result = printAndReturnCalc(2,4);   
printCalc(2); //take the default value for the second parameter
````

Notice in the second example, we specified only the first, non-optional parameter.  This behaves exactly as though we had specified the default value for that parameter.

***Types must match***
As discussed under the data types section, all declared data types must match. This includes parameters...
````
int func(int a="toy"){ //error, default value must be an int value
    return 1;
}
````
...variable assignments...
```
int func(int a){ 
    string retval = 0;  //error, a must be a string value
    return 1;
}
````
...and return values:
````
int func(int a){
    string retval="jim";
    return retval; //error, must match the declared returnDataType (int)
}
````

## Initializing objects

To define your own objects

## Classes
To define your own objects

#declarations

    macros
    #include vs #insert
    #i6
    #beguile
    #define
    #if






