#include <sstream>
#include <string>

using namespace std;

#ifndef JSON_H
#define JSON_H

#define JSON_ARRAY 1
#define JSON_STRING 2
#define JSON_NUMBER 3
#define JSON_OBJECT 4

struct JsonNodeBase {
    int type;
    virtual ~JsonNodeBase() {}
    virtual JsonNodeBase *clone() const = 0;
    virtual std::string toString() const {
        return "None";
    }
};

template <typename Derived>
struct JsonNode : public JsonNodeBase {
    virtual JsonNodeBase *clone() const {
            return new Derived(static_cast<Derived const&>(*this));
    }
};

#define Derive_JsonNode_CRTP(Type) struct Type: public JsonNode<Type>

Derive_JsonNode_CRTP(JsonArray) {
    std::vector<JsonNodeBase*> array;
    JsonArray() {}
    JsonArray(std::vector<JsonNodeBase*> arr) {
        for (size_t i = 0; i < arr.size(); ++i){
            array.push_back(arr[i]->clone());
        }
    }

    std::string toString() const {
        stringstream ss;
        ss << "[";
        for (size_t i = 0; i < array.size(); ++i) {
            ss << array[i]->toString();
            if (i != (array.size() - 1))
                ss << ",";
        }
        ss << "]";
        return ss.str();
    }
};

Derive_JsonNode_CRTP (JsonString){
    std::string str;
    JsonString() {}
    JsonString(const std::string& s): str(s) {}

    std::string toString() const {
        return "\"" + str + "\"";
    }
};

Derive_JsonNode_CRTP(JsonNumber) {
    double number;
    JsonNumber() {}
    JsonNumber(double n): number(n) {}

    std::string toString() const {
        stringstream ss;
        ss << number;
        return ss.str();
    }
};

Derive_JsonNode_CRTP(JsonObject) {
    JsonObject() {}

    std::map<std::string, JsonNodeBase*> elements;

    void add(const std::string& field, JsonNodeBase* node) {
        JsonNodeBase* js = node->clone();
        elements.insert(std::pair<std::string, JsonNodeBase*> (field, js));
    }

    std::string toString() const {
        stringstream ss;
        ss << "{";
        size_t i;
        auto it = elements.begin();
        for (i = 0; i < elements.size(); ++i) {
            ss << "\"" << (*it).first << "\":";
            ss << (*it).second->toString();
            if (i != elements.size() - 1) {
                ss << ",";
            }
            ++it;
        }
        ss << "}";
        return ss.str();
    }

    void parse(const std::string& str) {
        return;
    }
};

void test_class_JsonObject_method_toString() {
    JsonObject obj1;
    JsonNodeBase* node = new JsonString("Hello world");
    JsonNodeBase* n1 = new JsonNumber(1);
    JsonNodeBase* n2 = new JsonString("gfgf");
    JsonNodeBase* node1 = new JsonArray({n1, n2});
    obj1.add("obj", node);
    obj1.add("obj2", node1);
    delete node;
    std::cout << obj1.toString() << std::endl;
}

//void test_class_Message_method_ToString()
//{
//    Message msg("Hello world", "ABC", "XYZ");
//    std::cout << msg.ToString() << std::endl;
//}

#endif // JSON_H
