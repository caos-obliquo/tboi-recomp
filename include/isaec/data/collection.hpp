// Parsed game collections.
//
// The game keeps item, enemy, and level definitions in XML and in a few packed
// tables. Collection is the neutral in-memory model the binding layer reads:
// a flat table of key/value pairs plus an ordered id list, which is enough to
// back `Isaac.GetItemByID` and friends without exposing a parser type to Lua.

#ifndef ISAEC_DATA_COLLECTION_HPP
#define ISAEC_DATA_COLLECTION_HPP

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "isaec/platform/types.hpp"

namespace isaec::data {

enum class ValueType : u8 {
    Null,
    Bool,
    Int,
    Float,
    String,
    List,  // nested collection of values
};

struct Value;
using ValueList = std::vector<Value>;

// A dynamically typed value, matching what the Lua layer needs to hand back.
struct Value {
    ValueType type = ValueType::Null;
    bool boolean = false;
    i64 integer = 0;
    f64 number = 0.0;
    std::string text;
    ValueList items;

    static Value null() { return Value{}; }
    static Value of_bool(bool v);
    static Value of_int(i64 v);
    static Value of_float(f64 v);
    static Value of_string(std::string v);

    [[nodiscard]] bool is_null() const noexcept { return type == ValueType::Null; }
};

// A named table of values, addressable by key or by ordinal id.
class Collection {
public:
    explicit Collection(std::string name = {});

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    // Insert or overwrite by key. Order of first insertion is preserved so
    // iteration is stable across runs.
    Collection& set(std::string key, Value value);

    // Insert by key and assert the id is next in sequence. Returns false when
    // the ordinal does not line up, which catches truncated data early.
    bool push(std::string key, Value value);

    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] const Value* find(std::string_view key) const;
    [[nodiscard]] Value at(std::string_view key) const;

    // Ordinal lookup, as used by the game's ID-based getters.
    [[nodiscard]] bool has_id(usize id) const;
    [[nodiscard]] const Value* find_id(usize id) const;

    [[nodiscard]] usize size() const noexcept { return order_.size(); }
    [[nodiscard]] bool empty() const noexcept { return order_.empty(); }

    // Keys in insertion order.
    [[nodiscard]] const std::vector<std::string>& keys() const noexcept { return order_; }

    void clear();

private:
    std::string name_;
    std::vector<std::string> order_;
    std::unordered_map<std::string, Value> by_key_;
    std::unordered_map<std::string, usize> id_of_key_;
};

}  // namespace isaec::data

#endif  // ISAEC_DATA_COLLECTION_HPP
