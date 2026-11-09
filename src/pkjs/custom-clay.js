// Blendet die drei Farbwaehler nur beim Schema "Custom" ein.
//
// Clay schickt diese Funktion als Quelltext an die Konfigseite, deshalb muss
// alles, was sie braucht, in ihr selbst stehen (kein require).

module.exports = function() {
  var clayConfig = this;

  var THEME_CUSTOM = '7';   // muss zu THEME_CUSTOM in transit.c passen
  var COLOR_KEYS = ['CUSTOM_SCREEN', 'CUSTOM_PLATE', 'CUSTOM_FG'];

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var theme = clayConfig.getItemByMessageKey('THEME');
    if (!theme) { return; }

    function update() {
      var custom = String(theme.get()) === THEME_CUSTOM;
      COLOR_KEYS.forEach(function(key) {
        var item = clayConfig.getItemByMessageKey(key);
        if (!item) { return; }
        if (custom) { item.show(); } else { item.hide(); }
      });
    }

    theme.on('change', update);
    update();
  });
};
