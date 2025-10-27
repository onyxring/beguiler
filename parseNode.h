#include <string>
#include <map>
#include <variant>

#include "token.h"

enum class eNodeType{
    directive,
    integer,
    symbol,
    quote,
    routine,
    variableDeclaration,
    objectDeclaration,
    classDeclaration,
    //expression,
};
class parseNode {
    public:
        eNodeType type;
        token keyToken;

        parseNode* parent;
        std::map<std::string, std::variant<parseNode, token>> properties;
        std::vector<parseNode> children;
               
};

