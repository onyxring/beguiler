#include <string>
#include "parser.h"

using namespace std;

parseNode parseNode::add(parseNode node){
    children.push_back(node);
    return node;
}
variant<parseNode, token>& parseNode::operator[](string key) {
    return properties[key];
}