## Current TODOs
1. currently, the compiler will throw an error if a local variable shadows a global variable.  The same error should be thrown if a parameter name shadows a global.

2. currently, the compiler allows local variables and parameters to shadow members of the same instance.  Let's treat this in the same way we block global shadowing.

3. A member function requires a reference to an object's members to be prefixed with self. as the object.  If the current instance implements the same named member, the user should not be required to specify it.  The compiler should simply emit self.member.  Here's the summary behavior which I think was expressed previously:

a. if the member is defined in the scope of the current routine, use that.  I6 will resolve correctly.
b. if the member is defined on the object instance (or any of its ancestors), resolve to that by emitting "self.member".
c. if the member is defined as a global, resolve to that as is.  I6 will resolve correctly.

I'm not sure if this was intentionally implemented with different logic, or if this is a newly introduced bug.

4. In the _cloakOfDarkness.bgl file, room alias class defines self referencing property types, similar to...

alias class room : object { 
    room s_to; 
}

Later, an assignment of a different type (object) compiles correctly (you may need to modify the current assignment to get this to break, since it currently assigns a value of type room):

room foyer{
    s_to = cloak;
}

I'd expect an error to be thrown, because the inherited type of s_to is of type room but cloak is of type object.

I'm not sure if this is because alias classes are handled inappropriately (they are output as type object is I6 code, but should still be a unique type), or if this is a bug in the fact that the alias class references itself, or something larger.

5. the line number used when generating parse errors is not always correct.  In the current cloak of darkness example, I accidentally wrote this, on line 130:

    stateText = cloak.parent() == hook ? "with a cloak hanging on it.\n" : "screwed to the wall.\n"; 
    
    as this...

    statText = cloak.parent() == hook ? "with a cloak hanging on it.\n" : "screwed to the wall.\n"; 

    The error message was:
    
    /Users/jim/projects/IF-Projects/./WIP/cloakOfDarkness/_cloakOfDarkness.bgl:140:19: error: Undeclared variable 'stattext'

    which points to the wrong line number.

6. 12.2 compatability rules says:
    Built-in primitive compatibility — the compiler treats the following pairs as mutually compatible without any operator declaration:
    intliteral ↔ int (integer literals freely mix with integer variables)
    stringliteral ↔ string
    charliteral is not in this list; it is compatible with char only via declared operators.

    But I don't think char to charLiteral compatibility should be different the int to intLiteral or string to stringLiteral.  I think all type compatibility should be made through declared operators, not built in. 