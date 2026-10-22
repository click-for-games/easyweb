/* animateeasy - helper so one-shot animations can be replayed */
(function () {
  if (typeof EW === 'undefined') return;

  var originalAdd = EW.addClass;
  EW.addClass = function (sel, cls) {
    originalAdd(sel, cls);
    q_all(sel).forEach(function (el) {
      el.addEventListener(
        'animationend',
        function () {
          if (cls.indexOf('-once') >= 0) el.classList.remove(cls);
        },
        { once: true }
      );
    });
  };

  function q_all(sel) {
    try {
      return Array.prototype.slice.call(document.querySelectorAll(sel));
    } catch (e) {
      return [];
    }
  }

  /* scroll to a place, softly */
  EW.scrollTo = function (sel) {
    var el = document.querySelector(sel);
    if (el && el.scrollIntoView) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' });
    }
  };
})();
