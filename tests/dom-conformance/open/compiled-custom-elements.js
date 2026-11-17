// An element that exists is upgraded by running its definition's constructor.
// The standard constructs the function the ordinary way: `this` is a new
// object, the element comes from the HTMLElement constructor, which finds it
// on the definition's construction stack and gives it the class's prototype
// at that moment, and the upgrade fails unless the constructor returns the
// element. Litebird gives the element the prototype first and runs the
// constructor with the element as `this`. The interface hands out the
// element as the standard does (tests/dom-conformance/compiled.dat), so a
// class, and a constructor compiled for older browsers, cannot tell. What
// can tell is below: four of the five lines differ.
var out = [];
function t(name, fn) { var v; try { v = fn(); } catch (e) { v = 'THROWS ' + e.name; } out.push(name + ' = ' + (typeof v === 'string' ? v : JSON.stringify(v))); }
function inherit(C, Parent) { Parent = Parent || HTMLElement; C.prototype = Object.create(Parent.prototype, { constructor: { value: C, writable: true, configurable: true } }); Object.setPrototypeOf(C, Parent); return C; }
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
// Differs: the element keeps the prototype it was given before the
// constructor ran, where the standard had not given it one yet.
t('a constructor that throws before it reaches HTMLElement', function () {
  var Early = inherit(function () { throw new RangeError('early'); });
  customElements.define('x-early', Early);
  return [$('early') instanceof Early, Object.getPrototypeOf($('early')) === HTMLElement.prototype, $('early').matches(':defined')];
});
// Differs, and is the selector's: a customized built-in whose upgrade failed
// still matches `:defined`.
t(':defined after the upgrade of a customized built-in failed', function () {
  var Failed = inherit(function () { throw new RangeError('no'); }, HTMLButtonElement);
  customElements.define('failed-b', Failed, { extends: 'button' });
  return [$('failed').matches(':defined'), document.querySelectorAll('button:defined').length];
});
console.log(out.join('\n'));
