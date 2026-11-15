// An element that exists is upgraded by running its definition's constructor.
// The standard constructs the function the ordinary way: `this` is a new
// object, and the element comes from the HTMLElement constructor, which finds
// it on the definition's construction stack. Litebird runs the constructor
// with the element as `this`. For a class the two cannot be told apart. For
// a constructor written as a function they can, in the two lines below that
// differ. (What such a constructor is for works: tests/dom-conformance/compiled.dat.)
var out = [];
function t(name, fn) { var v; try { v = fn(); } catch (e) { v = 'THROWS ' + e.name; } out.push(name + ' = ' + (typeof v === 'string' ? v : JSON.stringify(v))); }
function inherit(C) { C.prototype = Object.create(HTMLElement.prototype, { constructor: { value: C, writable: true, configurable: true } }); Object.setPrototypeOf(C, HTMLElement); return C; }
var $ = function (id) { return document.getElementById(id); };

// The same in both: the element is upgraded.
t('a constructor that asks HTMLElement for its element', function () {
  var Good = inherit(function () { var self = Reflect.construct(HTMLElement, [], new.target); self.made = true; return self; });
  customElements.define('x-good', Good);
  return [$('good') instanceof Good, $('good').made, $('good').matches(':defined')];
});
// Differs: `this` is the element here, and an object of its own in the standard.
t('this in the constructor, during an upgrade', function () {
  var seen;
  var This = inherit(function () { var self = Reflect.construct(HTMLElement, [], new.target); seen = [this === self, this instanceof This, Object.getPrototypeOf(this) === This.prototype]; return self; });
  customElements.define('x-this', This);
  return seen;
});
// Differs: the standard refuses the upgrade (what the constructor returned is
// not the element) and the element stays as it was; here the constructor has
// written on the element and the upgrade counts as done.
t('a constructor that never reaches HTMLElement', function () {
  var Plain = inherit(function () { this.touched = true; });
  customElements.define('x-plain', Plain);
  return [$('plain').touched, $('plain').matches(':defined'), $('plain') instanceof Plain];
});
console.log(out.join('\n'));
