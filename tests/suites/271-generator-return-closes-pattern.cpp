// Phase 271, finding 275: IteratorClose distinguishes a throw from a return.
TEST(generator_return_completion_closes_a_suspended_pattern) {
    TestPage page;
    JsInterpreter& js = page.js;

    // A generator suspended in a destructuring default carries a Return
    // completion through the binding operation. IteratorClose may replace that
    // completion with an error from `return`; an ordinary throw from the
    // pattern, in contrast, remains the winning completion.
    js.eval(R"JS(
        var failures = [];
        function same(label, got, want) {
            if (got !== want) failures.push(label + ': got ' + String(got) + ', wanted ' + String(want));
        }
        function makeSource(onReturn) {
            var counts = { next: 0, close: 0, thisValue: null, argCount: -1 };
            var iterator = {
                next: function () {
                    counts.next += 1;
                    return { done: false, value: undefined };
                },
                return: function () {
                    counts.close += 1;
                    counts.thisValue = this;
                    counts.argCount = arguments.length;
                    return onReturn();
                }
            };
            var source = {};
            source[Symbol.iterator] = function () { return iterator; };
            return { counts: counts, iterator: iterator, source: source };
        }
        var after = 0;
        function* suspended(source) {
            var result;
            result = [ {} = yield ] = source;
            after += 1;
        }

        // A successful close preserves the injected return value, calls the
        // iterator with the right receiver and no arguments, and does not pull
        // another item from the iterator.
        var normal = makeSource(function () { return {}; });
        var first = suspended(normal.source);
        first.next();
        same('one value was read before suspension', normal.counts.next, 1);
        same('return has not run while suspended', normal.counts.close, 0);
        var completion = first.return(777);
        same('successful close preserves the generator return value', completion.value, 777);
        same('successful close completes the generator', completion.done, true);
        same('return ran once', normal.counts.close, 1);
        same('return used the iterator as this', normal.counts.thisValue === normal.iterator, true);
        same('return received no arguments', normal.counts.argCount, 0);
        same('close did not advance the iterator', normal.counts.next, 1);
        same('the assignment did not continue after return', after, 0);

        // For a Return completion, a primitive result from iterator.return is
        // a TypeError, not something to swallow as the pattern's own throw.
        var primitive = makeSource(function () { return null; });
        var second = suspended(primitive.source);
        second.next();
        var primitiveError = 'none';
        try { second.return(888); } catch (error) { primitiveError = error.name; }
        same('a primitive close result overrides return', primitiveError, 'TypeError');
        same('the primitive-return iterator was closed once', primitive.counts.close, 1);
        same('the primitive-return iterator was not advanced', primitive.counts.next, 1);
        same('the assignment stayed suspended', after, 0);

        // An error thrown by iterator.return likewise overrides the pending
        // Return completion.
        var throwing = makeSource(function () { throw new RangeError('close failed'); });
        var third = suspended(throwing.source);
        third.next();
        var closeError = 'none';
        try { third.return(999); } catch (error) { closeError = error.name + ':' + error.message; }
        same('a throwing close overrides return', closeError, 'RangeError:close failed');
        same('the throwing-return iterator was closed once', throwing.counts.close, 1);
        same('the throwing-return iterator was not advanced', throwing.counts.next, 1);

        // Assignment-target evaluation precedes IteratorStep. A yield inside
        // the target suspends before `next` is looked up by a call, and the
        // injected Return completion still closes this iterator.
        var targetSource = makeSource(function () { throw new RangeError('target close failed'); });
        delete targetSource.iterator.next;
        var targetObject = {};
        function* suspendedInTarget(source) {
            [ targetObject[ yield ] ] = source;
            after += 1;
        }
        var fourth = suspendedInTarget(targetSource.source);
        var targetStep = fourth.next();
        same('a target yield suspends before IteratorStep', targetStep.done, false);
        same('next was not called before target evaluation', targetSource.counts.next, 0);
        var targetCloseError = 'none';
        try { fourth.return(1000); }
        catch (error) { targetCloseError = error.name + ':' + error.message; }
        same('a target return completion reports close failure', targetCloseError,
             'RangeError:target close failed');
        same('the target iterator was closed once', targetSource.counts.close, 1);
        same('closing the target did not call next', targetSource.counts.next, 0);
        same('the target pattern did not continue after return', after, 0);

        // A simple AssignmentRest target is also resolved before the iterator
        // is drained. Its yield suspends with zero next calls, then return
        // closes the still-open outer iterator.
        var restSource = makeSource(function () { throw new RangeError('rest close failed'); });
        delete restSource.iterator.next;
        function* suspendedInRestTarget(source) {
            [ ...targetObject[ yield ] ] = source;
            after += 1;
        }
        var fifth = suspendedInRestTarget(restSource.source);
        var restStep = fifth.next();
        same('a rest-target yield suspends before draining', restStep.done, false);
        same('the rest target has not called next', restSource.counts.next, 0);
        var restCloseError = 'none';
        try { fifth.return(1001); }
        catch (error) { restCloseError = error.name + ':' + error.message; }
        same('a rest target return reports close failure', restCloseError,
             'RangeError:rest close failed');
        same('the rest-target iterator was closed once', restSource.counts.close, 1);
        same('closing the rest target did not call next', restSource.counts.next, 0);
        same('the rest pattern did not continue after return', after, 0);

        // IteratorClose for an existing Throw completion still preserves that
        // original error even if the close result is invalid.
        var abrupt = makeSource(function () { return null; });
        function* throwsInDefault(source) {
            var value;
            [ value = (function () { throw new SyntaxError('pattern failed'); })() ] = source;
        }
        var patternError = 'none';
        try { throwsInDefault(abrupt.source).next(); }
        catch (error) { patternError = error.name + ':' + error.message; }
        same('an existing throw still wins over a primitive close result',
             patternError, 'SyntaxError:pattern failed');
        same('the throwing pattern closed once', abrupt.counts.close, 1);
        same('the throwing pattern did not advance again', abrupt.counts.next, 1);

        if (failures.length) console.error(failures.join('\\n'));
    )JS");

    assert(js.eval("failures.length").to_number() == 0.0);
}
