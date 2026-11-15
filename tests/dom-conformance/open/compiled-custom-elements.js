function _typeof(o) { "@babel/helpers - typeof"; return _typeof = "function" == typeof Symbol && "symbol" == typeof Symbol.iterator ? function (o) { return typeof o; } : function (o) { return o && "function" == typeof Symbol && o.constructor === Symbol && o !== Symbol.prototype ? "symbol" : typeof o; }, _typeof(o); }
function _classCallCheck(instance, Constructor) { if (!(instance instanceof Constructor)) { throw new TypeError("Cannot call a class as a function"); } }
function _defineProperties(target, props) { for (var i = 0; i < props.length; i++) { var descriptor = props[i]; descriptor.enumerable = descriptor.enumerable || false; descriptor.configurable = true; if ("value" in descriptor) descriptor.writable = true; Object.defineProperty(target, _toPropertyKey(descriptor.key), descriptor); } }
function _createClass(Constructor, protoProps, staticProps) { if (protoProps) _defineProperties(Constructor.prototype, protoProps); if (staticProps) _defineProperties(Constructor, staticProps); Object.defineProperty(Constructor, "prototype", { writable: false }); return Constructor; }
function _toPropertyKey(t) { var i = _toPrimitive(t, "string"); return "symbol" == _typeof(i) ? i : String(i); }
function _toPrimitive(t, r) { if ("object" != _typeof(t) || !t) return t; var e = t[Symbol.toPrimitive]; if (void 0 !== e) { var i = e.call(t, r || "default"); if ("object" != _typeof(i)) return i; throw new TypeError("@@toPrimitive must return a primitive value."); } return ("string" === r ? String : Number)(t); }
function _inherits(subClass, superClass) { if (typeof superClass !== "function" && superClass !== null) { throw new TypeError("Super expression must either be null or a function"); } subClass.prototype = Object.create(superClass && superClass.prototype, { constructor: { value: subClass, writable: true, configurable: true } }); Object.defineProperty(subClass, "prototype", { writable: false }); if (superClass) _setPrototypeOf(subClass, superClass); }
function _setPrototypeOf(o, p) { _setPrototypeOf = Object.setPrototypeOf ? Object.setPrototypeOf.bind() : function _setPrototypeOf(o, p) { o.__proto__ = p; return o; }; return _setPrototypeOf(o, p); }
function _createSuper(Derived) { var hasNativeReflectConstruct = _isNativeReflectConstruct(); return function _createSuperInternal() { var Super = _getPrototypeOf(Derived), result; if (hasNativeReflectConstruct) { var NewTarget = _getPrototypeOf(this).constructor; result = Reflect.construct(Super, arguments, NewTarget); } else { result = Super.apply(this, arguments); } return _possibleConstructorReturn(this, result); }; }
function _possibleConstructorReturn(self, call) { if (call && (_typeof(call) === "object" || typeof call === "function")) { return call; } else if (call !== void 0) { throw new TypeError("Derived constructors may only return object or undefined"); } return _assertThisInitialized(self); }
function _assertThisInitialized(self) { if (self === void 0) { throw new ReferenceError("this hasn't been initialised - super() hasn't been called"); } return self; }
function _isNativeReflectConstruct() { if (typeof Reflect === "undefined" || !Reflect.construct) return false; if (Reflect.construct.sham) return false; if (typeof Proxy === "function") return true; try { Boolean.prototype.valueOf.call(Reflect.construct(Boolean, [], function () {})); return true; } catch (e) { return false; } }
function _getPrototypeOf(o) { _getPrototypeOf = Object.setPrototypeOf ? Object.getPrototypeOf.bind() : function _getPrototypeOf(o) { return o.__proto__ || Object.getPrototypeOf(o); }; return _getPrototypeOf(o); }
function _wrapNativeSuper(Class) { var _cache = typeof Map === "function" ? new Map() : undefined; _wrapNativeSuper = function _wrapNativeSuper(Class) { if (Class === null || !_isNativeFunction(Class)) return Class; if (typeof Class !== "function") { throw new TypeError("Super expression must either be null or a function"); } if (typeof _cache !== "undefined") { if (_cache.has(Class)) return _cache.get(Class); _cache.set(Class, Wrapper); } function Wrapper() { return _construct(Class, arguments, _getPrototypeOf(this).constructor); } Wrapper.prototype = Object.create(Class.prototype, { constructor: { value: Wrapper, enumerable: false, writable: true, configurable: true } }); return _setPrototypeOf(Wrapper, Class); }; return _wrapNativeSuper(Class); }
function _construct(Parent, args, Class) { if (_isNativeReflectConstruct()) { _construct = Reflect.construct.bind(); } else { _construct = function _construct(Parent, args, Class) { var a = [null]; a.push.apply(a, args); var Constructor = Function.bind.apply(Parent, a); var instance = new Constructor(); if (Class) _setPrototypeOf(instance, Class.prototype); return instance; }; } return _construct.apply(null, arguments); }
function _isNativeFunction(fn) { try { return Function.toString.call(fn).indexOf("[native code]") !== -1; } catch (e) { return typeof fn === "function"; } }
function _get() { if (typeof Reflect !== "undefined" && Reflect.get) { _get = Reflect.get.bind(); } else { _get = function _get(target, property, receiver) { var base = _superPropBase(target, property); if (!base) return; var desc = Object.getOwnPropertyDescriptor(base, property); if (desc.get) { return desc.get.call(arguments.length < 3 ? target : receiver); } return desc.value; }; } return _get.apply(this, arguments); }
function _superPropBase(object, property) { while (!Object.prototype.hasOwnProperty.call(object, property)) { object = _getPrototypeOf(object); if (object === null) break; } return object; }

var out = [];
function t(name, fn) { var v; try { v = fn(); } catch (e) { v = 'THROWS ' + e.name; } out.push(name + ' = ' + (typeof v === 'string' ? v : JSON.stringify(v))); }

// 1. What Babel 7 makes of `class Greeter extends HTMLElement` for an ES5 target.
var Greeter = /*#__PURE__*/function (_HTMLElement) {
  _inherits(Greeter, _HTMLElement);
  var _super = _createSuper(Greeter);
  function Greeter() { var _this; _classCallCheck(this, Greeter); _this = _super.call(this); _this.made = true; return _this; }
  _createClass(Greeter, [{ key: "connectedCallback", value: function connectedCallback() { this.textContent = 'hello ' + (this.getAttribute('name') || 'nobody'); } }], [{ key: "observedAttributes", get: function get() { return ['name']; } }]);
  return Greeter;
}( /*#__PURE__*/_wrapNativeSuper(HTMLElement));
t('babel: the class', function () { return [typeof Greeter, Greeter.observedAttributes.join(), Greeter.ELEMENT_NODE, Greeter.prototype instanceof HTMLElement]; });
t('babel: define', function () { customElements.define('x-greeter', Greeter); return customElements.get('x-greeter') === Greeter; });
t('babel: createElement', function () { var e = document.createElement('x-greeter'); e.setAttribute('name', 'you'); document.getElementById('host').appendChild(e); return [e instanceof Greeter, e instanceof HTMLElement, e.made, e.textContent, e.constructor === Greeter]; });
t('babel: new', function () { var e = new Greeter(); return [e instanceof Greeter, e.localName, e.made]; });
t('babel: parsed', function () { var h = document.getElementById('host'); h.innerHTML = '<x-greeter name="page"></x-greeter>'; var e = h.firstChild; return [e instanceof Greeter, e.made, e.textContent]; });

// 2. The same by hand: Reflect.construct with the constructor as new.target.
function Old() { var self = Reflect.construct(HTMLElement, [], Old); self.made = true; return self; }
Old.prototype = Object.create(HTMLElement.prototype, { constructor: { value: Old, writable: true, configurable: true } });
Object.setPrototypeOf(Old, HTMLElement);
t('by hand: define and create', function () { customElements.define('x-old', Old); var e = document.createElement('x-old'); return [e instanceof Old, e instanceof HTMLElement, e.made, e.localName, e.constructor === Old]; });
t('by hand: new', function () { var e = new Old(); return [e instanceof Old, e.localName, e.made]; });

// 3. Reflect.construct of an interface with a class as new.target.
t('Reflect.construct(Event, [type], Sub)', function () { class Sub extends Event {} var e = Reflect.construct(Event, ['x'], Sub); return [e instanceof Sub, e instanceof Event, e.type]; });
t('Reflect.construct(EventTarget, [], Sub)', function () { function Sub() {} Sub.prototype = Object.create(EventTarget.prototype); var b = Reflect.construct(EventTarget, [], Sub), n = 0; b.addEventListener('x', function () { n++; }); b.dispatchEvent(new Event('x')); return [b instanceof Sub, b instanceof EventTarget, n]; });
console.log(out.join('\n'));
