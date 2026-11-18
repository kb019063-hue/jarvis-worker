# 14. ✅ `has_class()` built a `stringstream` per call

> Moved out of `AUDIT.md` — written there as a `## 14. ✅ `has_class()` built a `stringstream` per call` section.
> The text below is the section as it stood, unchanged.


```cpp
bool has_class(const std::string& c) const {
    auto list = classes();              // stringstream + vector<string> + copies
    return std::find(list.begin(), list.end(), c) != list.end();
}
```

Measured: **1,100 ns/call** — a microsecond to answer a substring question. It
is called once per class selector per element during the cascade, so it
dominated style resolution. Now an in-place scan of the `class` attribute.
