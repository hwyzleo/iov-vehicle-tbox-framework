#include "store_serializer.h"
#include <cassert>
#include <iostream>
#include <string>
#include <cmath>

using namespace hwyz::store;

void test_int_serialization() {
    Serializer serializer;

    int value = 42;
    std::string bytes = serializer.serialize(value);
    assert(!bytes.empty());

    int result = 0;
    bool success = serializer.deserialize(bytes, result);
    assert(success == true);
    assert(result == 42);
}

void test_string_serialization() {
    Serializer serializer;

    std::string value = "hello world";
    std::string bytes = serializer.serialize(value);
    assert(!bytes.empty());

    std::string result;
    bool success = serializer.deserialize(bytes, result);
    assert(success == true);
    assert(result == "hello world");
}

void test_double_serialization() {
    Serializer serializer;

    double value = 3.14159;
    std::string bytes = serializer.serialize(value);
    assert(!bytes.empty());

    double result = 0.0;
    bool success = serializer.deserialize(bytes, result);
    assert(success == true);
    assert(std::abs(result - value) < 0.00001);
}

void test_bool_serialization() {
    Serializer serializer;

    bool value = true;
    std::string bytes = serializer.serialize(value);
    assert(!bytes.empty());

    bool result = false;
    bool success = serializer.deserialize(bytes, result);
    assert(success == true);
    assert(result == true);
}

void test_invalid_deserialization() {
    Serializer serializer;

    std::string invalidBytes = "invalid_data";
    int result = 0;
    bool success = serializer.deserialize(invalidBytes, result);
    assert(success == false);
}

int main() {
    test_int_serialization();
    test_string_serialization();
    test_double_serialization();
    test_bool_serialization();
    test_invalid_deserialization();
    std::cout << "All serializer tests passed!" << std::endl;
    return 0;
}
