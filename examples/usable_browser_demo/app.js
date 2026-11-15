(function () {
  var geomReadout = document.getElementById('geom-readout');
  var netReadout = document.getElementById('net-readout');
  var cardGeom = document.getElementById('card-geom');
  var extraPad = 0;

  function updateGeometry() {
    if (!cardGeom || !geomReadout) return;
    var r = cardGeom.getBoundingClientRect();
    geomReadout.textContent =
      'Measured box: (' + Math.round(r.x) + ', ' + Math.round(r.y) + ') — ' +
      Math.round(r.width) + '×' + Math.round(r.height) + 'px | viewport ' +
      window.innerWidth + '×' + window.innerHeight + 'px';
  }

  updateGeometry();
  window.addEventListener('resize', updateGeometry);

  var btnMeasure = document.getElementById('btn-measure');
  if (btnMeasure) {
    btnMeasure.addEventListener('click', updateGeometry);
  }

  var btnGrow = document.getElementById('btn-grow');
  if (btnGrow && cardGeom) {
    btnGrow.addEventListener('click', function () {
      extraPad = (extraPad + 12) % 48;
      cardGeom.style.paddingBottom = (16 + extraPad) + 'px';
      updateGeometry();
    });
  }

  var btnTheme = document.getElementById('btn-theme');
  if (btnTheme) {
    btnTheme.addEventListener('click', function () {
      document.body.classList.toggle('alt-accent');
      updateGeometry();
    });
  }

  var tabs = document.querySelectorAll('.tab');
  function filterCards(cat, query) {
    var q = (query || '').toLowerCase();
    var items = document.querySelectorAll('#cards-grid > *');
    for (var i = 0; i < items.length; i++) {
      var el = items[i];
      var matchCat = !cat || cat === 'all' || el.getAttribute('data-cat') === cat;
      var matchText = !q || el.textContent.toLowerCase().indexOf(q) !== -1;
      el.style.display = (matchCat && matchText) ? '' : 'none';
    }
    updateGeometry();
  }

  var activeCat = 'all';
  for (var t = 0; t < tabs.length; t++) {
    (function (tab) {
      tab.addEventListener('click', function () {
        for (var j = 0; j < tabs.length; j++) tabs[j].classList.remove('active');
        tab.classList.add('active');
        activeCat = tab.getAttribute('data-cat') || 'all';
        var inp = document.getElementById('filter-input');
        filterCards(activeCat, inp ? inp.value : '');
      });
    })(tabs[t]);
  }

  var filterInput = document.getElementById('filter-input');
  if (filterInput) {
    filterInput.addEventListener('input', function () {
      filterCards(activeCat, filterInput.value);
    });
  }

  function runProbe() {
    if (!netReadout) return;
    if (location.protocol === 'http:' || location.protocol === 'https:') {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', '/demo/api/status.json', true);
      xhr.onload = function () {
        if (xhr.status === 200) {
          var data = JSON.parse(xhr.responseText);
          netReadout.textContent =
            'HTTP 200 OK (' + data.http + ') • Fonts: ' + data.fonts + ' • TLS: ' + data.tls;
          updateGeometry();
        }
      };
      xhr.onerror = function () {
        netReadout.textContent = 'Local bundle mode active (readyState=' + document.readyState + ')';
      };
      xhr.send();
    } else {
      netReadout.textContent =
        'Loaded from ' + location.protocol + ' bundle • External CSS + defer JS active!';
    }
  }

  var btnPing = document.getElementById('btn-ping');
  if (btnPing) btnPing.addEventListener('click', runProbe);
  runProbe();
})();
