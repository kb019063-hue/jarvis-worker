var out = [];
function t(name, fn) { var v; try { v = fn(); } catch (e) { v = 'THROWS ' + e.name + ': ' + e.message; } out.push(name + ' = ' + (typeof v === 'string' ? v : JSON.stringify(v))); }
function inp(type, value) { var i = document.createElement('input'); i.type = type; i.value = value; return i; }
// Date and time inputs: the value is checked, and is a number and a Date as well as a string.
t('date values', function () { return [inp('date', '2026-02-30').value, inp('date', '2026-03-04').value, inp('time', '25:00').value, inp('time', '12:30').value, inp('month', '2026-13').value, inp('week', '2026-W10').value, inp('datetime-local', '2026-03-04T05:06').value]; });
t('date as a number', function () { return [inp('date', '2026-03-04').valueAsNumber, inp('time', '12:30').valueAsNumber, inp('month', '2026-03').valueAsNumber, inp('datetime-local', '2026-03-04T05:06').valueAsNumber]; });
t('date as a Date', function () { var d = inp('date', '2026-03-04').valueAsDate; return [d && d.toISOString(), String(inp('date', '').valueAsDate)]; });
t('setting valueAsDate', function () { var i = inp('date', ''); i.valueAsDate = new Date(Date.UTC(2020, 0, 2)); var a = [i.value]; i.valueAsNumber = 86400000 * 3; a.push(i.value); return a; });
t('stepping a date', function () { var i = inp('date', '2026-03-04'); i.stepUp(2); var a = [i.value]; i.min = '2026-03-10'; a.push(i.validity.rangeUnderflow); return a; });
t('stepping a time', function () { var i = inp('time', '12:30'); i.stepUp(); return i.value; });
// The SVG DOM.
t('svg elements', function () { var c = document.getElementById('ci'), s = document.getElementById('svg'); return [typeof c.className, c.className && c.className.baseVal, typeof s.viewBox, c.constructor.name, s.constructor.name]; });
// The SVG interfaces, and the three answers one wrong table used to cost
// (Phase 374). The negative lines are the point: SVG 2 §7.7 gives `viewBox` to
// svg/symbol/marker/pattern/view and no other element, so a `g` or a `rect`
// answering `undefined` is the correct reading, not a gap.
t('svg interface names', function () { var NS = 'http://www.w3.org/2000/svg';
  return ['g', 'rect', 'symbol', 'marker', 'pattern', 'view', 'defs'].map(function (tag) {
    return document.createElementNS(NS, tag).constructor.name; }); });
t('svg graphics instanceof', function () { var NS = 'http://www.w3.org/2000/svg';
  return ['g', 'symbol', 'marker'].map(function (tag) {
    return document.createElementNS(NS, tag) instanceof SVGGraphicsElement; }); });
t('svg viewBox on the five, absent on the rest', function () { var NS = 'http://www.w3.org/2000/svg';
  return ['svg', 'symbol', 'marker', 'pattern', 'view', 'g', 'rect', 'defs'].map(function (tag) {
    return typeof document.createElementNS(NS, tag).viewBox; }); });
t('svg symbol viewBox reads its attribute', function () { var NS = 'http://www.w3.org/2000/svg';
  var y = document.createElementNS(NS, 'symbol'); y.setAttribute('viewBox', '1 2 3 4');
  var b = y.viewBox.baseVal; return [b.x, b.y, b.width, b.height]; });
t('getBBox and createSVGRect exist', function () { var NS = 'http://www.w3.org/2000/svg';
  var g = document.createElementNS(NS, 'g'), s = document.getElementById('svg');
  return [typeof g.getBBox, s.createSVGRect().constructor.name, g.getBBox() instanceof SVGRect]; });
// `preserveAspectRatio`, the other half of SVGFitToViewBox (Phase 381). The
// interesting line is the third: setting align to `none` still writes the
// meetOrSlice (`none meet`), because SVG 2 §8.14.4 serializes both parts with
// no exception for `none`.
t('preserveAspectRatio is an animated pair', function () {
  var NS = 'http://www.w3.org/2000/svg', e = document.createElementNS(NS, 'svg');
  var p = e.preserveAspectRatio;
  return [p instanceof SVGAnimatedPreserveAspectRatio, p.baseVal instanceof SVGPreserveAspectRatio,
          p.baseVal.align, p.baseVal.meetOrSlice, p.baseVal === p.animVal]; });
t('preserveAspectRatio reads its attribute', function () {
  var NS = 'http://www.w3.org/2000/svg', e = document.createElementNS(NS, 'svg');
  e.setAttribute('preserveAspectRatio', 'xMinYMax slice');
  return [e.preserveAspectRatio.baseVal.align, e.preserveAspectRatio.baseVal.meetOrSlice]; });
t('preserveAspectRatio writes both parts', function () {
  var NS = 'http://www.w3.org/2000/svg', e = document.createElementNS(NS, 'svg');
  e.preserveAspectRatio.baseVal.align = SVGPreserveAspectRatio.SVG_PRESERVEASPECTRATIO_NONE;
  return [e.getAttribute('preserveAspectRatio'), e.preserveAspectRatio.baseVal.align]; });
t('preserveAspectRatio on the five or nowhere', function () {
  var NS = 'http://www.w3.org/2000/svg';
  return ['svg', 'symbol', 'marker', 'pattern', 'view', 'g', 'rect'].map(function (tag) {
    return typeof document.createElementNS(NS, tag).preserveAspectRatio; }); });
// The length-valued geometry attributes (Phase 395). `5mm` is the line that
// matters: unitType 7 and the number 5 are what was *written*, while `value` is
// the conversion to user units -- a type that resolved on the way in could not
// answer the first two.
t('a geometry attribute is an animated length', function () {
  var NS = 'http://www.w3.org/2000/svg', e = document.createElementNS(NS, 'rect');
  e.setAttribute('x', '10');
  var a = e.x;
  return [a instanceof SVGAnimatedLength, a.baseVal instanceof SVGLength,
          a.baseVal.unitType, a.baseVal.valueInSpecifiedUnits, a.baseVal.valueAsString]; });
t('a length keeps the unit it was written in', function () {
  var NS = 'http://www.w3.org/2000/svg', e = document.createElementNS(NS, 'rect');
  e.setAttribute('height', '5mm'); e.setAttribute('width', '50%');
  return [e.height.baseVal.unitType, e.height.baseVal.valueInSpecifiedUnits,
          Math.round(e.height.baseVal.value * 1000) / 1000,
          e.width.baseVal.unitType, e.width.baseVal.value]; });
t('who has which length attribute', function () {
  var NS = 'http://www.w3.org/2000/svg', mk = function (t) { return document.createElementNS(NS, t); };
  return [typeof mk('rect').rx, typeof mk('circle').rx, typeof mk('line').x1,
          typeof mk('g').x, typeof mk('marker').refX, typeof mk('svg').width]; });
console.log(out.join('\n'));
