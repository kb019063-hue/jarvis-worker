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
// DOMParser with an XML type: an XML document, or one that says what was wrong.
t('xml', function () { var x = new DOMParser().parseFromString('<a xmlns="urn:x"><b id="k">t</b><c/></a>', 'application/xml'); return [x.documentElement.nodeName, x.documentElement.namespaceURI, x.contentType, x.querySelector('b').textContent, new XMLSerializer().serializeToString(x), x.createElement('P').nodeName]; });
t('xml that is not well formed', function () { var x = new DOMParser().parseFromString('<a><b></a>', 'application/xml'); return [x.documentElement.nodeName, x.getElementsByTagName('parsererror').length]; });
t('svg as a document', function () { var x = new DOMParser().parseFromString('<svg xmlns="http://www.w3.org/2000/svg"><path d="M0 0"/></svg>', 'image/svg+xml'); return [x.documentElement.nodeName, x.documentElement.namespaceURI, x.documentElement.firstChild.nodeName, x.contentType]; });
// The SVG DOM.
t('svg elements', function () { var c = document.getElementById('ci'), s = document.getElementById('svg'); return [typeof c.className, c.className && c.className.baseVal, typeof s.viewBox, c.constructor.name, s.constructor.name]; });
console.log(out.join('\n'));
