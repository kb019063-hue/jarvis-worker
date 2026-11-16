var original = Promise.resolve;
function check(method) {
  var trace = [], count = 0;
  var source = {
    next: function() { trace.push('next'); return count++ ? {value: 8, done: true} : {value: 3, done: false}; },
    return: function() { trace.push('closed'); return {done: true}; }
  };
  source[Symbol.iterator] = function() { return this; };
  Promise.resolve = function(value) { trace.push('resolve:' + value); throw Error('refused'); };
  Promise[method](source).catch(function(e) {
    var line = method + ':' + trace.join('/') + ':' + e.message;
    document.getElementById('log').textContent += line + ';';
    console.log(line);
  });
}
check('all'); check('race'); check('any'); check('allSettled');
Promise.resolve = original;
