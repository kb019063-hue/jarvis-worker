# 15. ✅ `text_content()` was O(n²) in depth

> Moved out of `AUDIT.md` — written there as a `## 15. ✅ `text_content()` was O(n²) in depth` section.
> The text below is the section as it stood, unchanged.


```cpp
std::string Node::text_content() const {
    std::string res;
    for (const auto& ch : m_children) res += ch->text_content();  // fresh string per subtree
    return res;
}
```

Every level copied all of its descendants' text again. Now appends into one
buffer.

---
