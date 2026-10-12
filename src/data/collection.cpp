#include "isaec/data/collection.hpp"

namespace isaec::data {

Value Value::of_bool(bool v) {
    Value value;
    value.type = ValueType::Bool;
    value.boolean = v;
    return value;
}

Value Value::of_int(i64 v) {
    Value value;
    value.type = ValueType::Int;
    value.integer = v;
    value.number = static_cast<f64>(v);
    return value;
}

Value Value::of_float(f64 v) {
    Value value;
    value.type = ValueType::Float;
    value.number = v;
    return value;
}

Value Value::of_string(std::string v) {
    Value value;
    value.type = ValueType::String;
    value.text = std::move(v);
    return value;
}

Collection::Collection(std::string name) : name_(std::move(name)) {}

Collection& Collection::set(std::string key, Value value) {
    if (by_key_.find(key) == by_key_.end()) {
        order_.push_back(key);
        id_of_key_.emplace(key, order_.size() - 1);
    }
    by_key_[std::move(key)] = std::move(value);
    return *this;
}

bool Collection::push(std::string key, Value value) {
    if (by_key_.find(key) != by_key_.end()) return false;
    if (id_of_key_.find(key) != id_of_key_.end()) return false;
    set(std::move(key), std::move(value));
    return true;
}

bool Collection::has(std::string_view key) const { return find(key) != nullptr; }

const Value* Collection::find(std::string_view key) const {
    const auto it = by_key_.find(std::string(key));
    return it == by_key_.end() ? nullptr : &it->second;
}

Value Collection::at(std::string_view key) const {
    const Value* found = find(key);
    return found != nullptr ? *found : Value::null();
}

bool Collection::has_id(usize id) const { return id < order_.size(); }

const Value* Collection::find_id(usize id) const {
    if (id >= order_.size()) return nullptr;
    const auto it = by_key_.find(order_[id]);
    return it == by_key_.end() ? nullptr : &it->second;
}

void Collection::clear() {
    name_.clear();
    order_.clear();
    by_key_.clear();
    id_of_key_.clear();
}

}  // namespace isaec::data
