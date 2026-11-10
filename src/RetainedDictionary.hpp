#pragma once

namespace cf {
// Cocos createWithDictionary performs virtual deep copies of its values.
// Collision logs contain engine identities, so copy membership and retain
// existing objects through setObject instead. enumerate must support both key types.
template<class Dictionary, class Enumerate>
Dictionary* retainedDictionarySnapshot(Dictionary* source, Enumerate const& enumerate) {
    auto snapshot = Dictionary::create();
    if (source) enumerate(source, [snapshot](auto const& key, auto* value) {
        if (value) snapshot->setObject(value, key);
    });
    return snapshot;
}

template<class Dictionary, class Enumerate>
void restoreRetainedDictionary(Dictionary*& target, Dictionary* snapshot, Enumerate const& enumerate) {
    if (target == snapshot && target) return;
    if (!target) {
        target = retainedDictionarySnapshot(snapshot, enumerate);
        target->retain(); // ownership normally held by PlayerObject
        return;
    }
    // Snapshot owns the values while target releases its old membership.
    target->removeAllObjects();
    if (snapshot) enumerate(snapshot, [target](auto const& key, auto* value) {
        if (value) target->setObject(value, key);
    });
}
}
