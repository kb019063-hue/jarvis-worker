// Phase 556, finding 556: Set.prototype.union invokes other.keys() before it
// snapshots the receiver. ECMA-262 orders GetIteratorFromMethod before copying
// [[SetData]], so side effects in keys() and the iterator's next getter count.

TEST(set_union_gets_keys_iterator_before_snapshotting_receiver) {
    TestPage page;
    const std::string answer = page.js.eval(R"JS(
        (function () {
            var receiver = new Set(['kept', 'removed']);
            var calls = [];
            var nextCount = 0;
            var iterator = {
                get next() {
                    calls.push('get next');
                    receiver.add('from-next-getter');
                    return function () {
                        calls.push(this === iterator ? 'next' : 'bad next this');
                        if (nextCount++ === 0) {
                            // This is after the snapshot and must not leak into
                            // the receiver-derived prefix of the result.
                            receiver.add('too-late');
                            return { done: false, value: 'other' };
                        }
                        return { done: true };
                    };
                }
            };
            var other = {
                get size() {
                    calls.push('size');
                    return {
                        valueOf: function () {
                            calls.push('ToNumber');
                            return 1;
                        }
                    };
                },
                get has() {
                    calls.push('has');
                    return function () { throw new Error('union must not call has'); };
                },
                get keys() {
                    calls.push('keys');
                    return function () {
                        calls.push(this === other ? 'keys()' : 'bad keys this');
                        receiver.delete('removed');
                        receiver.add('from-keys');
                        return iterator;
                    };
                }
            };

            var result = receiver.union(other);
            return [
                result instanceof Set,
                Array.from(result).join(','),
                Array.from(receiver).join(','),
                calls.join(',')
            ].join('|');
        })()
    )JS").to_string();

    // `too-late` is added by the first next() call, after the receiver was
    // copied. The other two mutations happen while obtaining the iterator and
    // therefore replace `removed` in the returned Set's receiver prefix.
    assert(answer ==
        "true|kept,from-keys,from-next-getter,other|"
        "kept,from-keys,from-next-getter,too-late|"
        "size,ToNumber,has,keys,keys(),get next,next,next");
}
