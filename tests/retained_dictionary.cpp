#include "../src/RetainedDictionary.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <variant>
#include <vector>

struct EngineObject {
    int references = 0, copyCalls = 0;
    EngineObject* copy() { ++copyCalls; return nullptr; } // Not copyable, like engine identities.
};
struct Dictionary {
    using Key = std::variant<intptr_t, std::string>;
    std::map<Key, EngineObject*> entries;
    static inline std::vector<Dictionary*> allocated;
    int references = 1;
    static Dictionary* create() { auto d = new Dictionary; allocated.push_back(d); return d; }
    void retain() { ++references; }
    template<class K> void setObject(EngineObject* object, K const& key) {
        assert(object);
        auto& previous = entries[Key(key)];
        if (previous == object) return;
        if (previous) --previous->references;
        previous = object; ++object->references;
    }
    void removeAllObjects() { for (auto [key, object] : entries) { (void)key; if (object) --object->references; } entries.clear(); }
    ~Dictionary() { removeAllObjects(); }
};
struct Enumerate {
    template<class Callback> void operator()(Dictionary* d, Callback callback) const {
        for (auto const& [key, object] : d->entries)
            std::visit([&](auto const& k) { callback(k, object); }, key);
    }
};
int main() {
    EngineObject first, second;
    auto original = Dictionary::create(); original->setObject(&first, intptr_t(12));
    auto saved = cf::retainedDictionarySnapshot(original, Enumerate{});
    assert(saved != original && saved->entries.at(intptr_t(12)) == &first);
    assert(first.references == 2 && first.copyCalls == 0);
    original->removeAllObjects(); original->setObject(&second, intptr_t(99));
    assert(saved->entries.size() == 1 && first.references == 1);
    auto identity = original;
    cf::restoreRetainedDictionary(original, saved, Enumerate{});
    assert(original == identity && original->entries.at(intptr_t(12)) == &first);
    assert(first.references == 2 && second.references == 0);
    cf::restoreRetainedDictionary(saved, saved, Enumerate{}); // Alias must not erase itself.
    assert(saved->entries.size() == 1);
    auto strings = Dictionary::create(); strings->setObject(&second, std::string("contact"));
    auto savedStrings = cf::retainedDictionarySnapshot(strings, Enumerate{});
    assert(savedStrings->entries.at(std::string("contact")) == &second);
    Dictionary* missing = nullptr;
    cf::restoreRetainedDictionary(missing, savedStrings, Enumerate{});
    assert(missing && missing->references == 2 && missing->entries.at(std::string("contact")) == &second);
    auto empty = cf::retainedDictionarySnapshot<Dictionary>(nullptr, Enumerate{});
    assert(empty->entries.empty());
    cf::restoreRetainedDictionary(original, static_cast<Dictionary*>(nullptr), Enumerate{});
    assert(original->entries.empty() && first.references == 1);
    // An absent value must never reach setObject/retain.
    empty->entries[intptr_t(42)] = nullptr;
    auto filtered = cf::retainedDictionarySnapshot(empty, Enumerate{});
    assert(filtered->entries.empty());
    assert(first.copyCalls == 0 && second.copyCalls == 0);
    for (auto d : Dictionary::allocated) delete d;
    Dictionary::allocated.clear();
    assert(first.references == 0 && second.references == 0);
    std::cout << "Retained dictionary regression tests passed\n";
}
