/* dataweb - tiny client helpers for talking to a dataweb powered api */
(function () {
  if (typeof EW === 'undefined') return;

  EW.db = {
    list: function (key, cb) {
      return EW.get('/api/' + key, cb);
    },
    add: function (key, item, cb) {
      return EW.post('/api/' + key, item, cb);
    }
  };
})();
